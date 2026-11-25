// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВОЛНЫ 2-Б (06.10.2026): «ДОКЛАД ОБ ОХОТЕ» 56450 (кнопка NPC 30068) и «ПОЖЕРТВОВАНИЕ» (кнопка NPC 30051). [ПОРТ]
//   Отчёт — `Reports/owner_1006/wave2B_hunt_donation.md`.
//
// ЧТО ПРОВЕРЯЕТ (путь игрока — НАСТОЯЩИЕ события Slate `SimulateClickAt` по ЖИВОЙ геометрии, шаг раз в 0,4 с):
//   A. Разговор с Эллой (107) → клик по кнопке меню с кодом 30068 → окно 56450 открыто, разговор закрыт (эталонный 5045);
//      сетка: знаки (`LinkCode 208`) подряд с числом стопки; контроль — предмет не-знак и 60073 в сетку НЕ попали; подписи
//      56494/56493/56492 — loc 110210/110211/110212.
//   B. Клик «Отнести знаки» БЕЗ выбора → 6150 НЕ ушёл (контроль). Клик по ячейке → нумпад режима 9 → ввод 7 → клик «OK» →
//      выбран знак: число 7 в 56451, звание (110214) в 56452, рамка 56491 ровно на ячейке (живые прямоугольники).
//      Контроль: клик по ПУСТОЙ ячейке нумпад не открывает; «Отмена» нумпада выбор не меняет.
//   C. Клик «Отнести знаки» → РОВНО ОДИН 6150, тело 11 б (charId, слот, контейнер 2, кол-во 7) — перехват исходящих.
//   D. Приём 6151 ШТАТНЫМ `DebugFeedPacket` → `HandlePacket`: код 2 → 110215 (предмет, звание), выбор сброшен, стопка
//      обновлена, вес поставлен; код 1 → 110213; 9 → 110405; 6 → 110404 «6» и вес НЕ тронут (контроль); 13 → молча
//      (контроль); кол-во 0 → ячейка сумки очищена, знак ушёл из сетки.
//   E. Наведение на ячейку → тултип владельца HuntReport; курсор ушёл → тултип снят. Крестик → окно закрыто.
//   F. Разговор с NPC 163 → клик по кнопке с кодом 30051 → нумпад режима 6, разговор открыт; максимум = стопка, что уйдёт
//      ([ПОРТ] Д3; контроль ключом 0 — сумма); ввод 4 + «OK» → РОВНО ОДИН 6132 (charId, 60073, контейнер 2, слот последней
//      стопки, 4); уход от NPC с открытым нумпадом → нумпад закрыт, 6132 нет; общий счёт < кол-ва → не шлёт (контроль 2 → шлёт).
//   G. Приём 6133: код 1 → кол-во ячейке, вес; код 1 с кол-вом 0 → ячейка удалена; код 10 → ничего (контроль); нотисов нет.
//   H. Гейты души/одержимости: 6150/6132 не гейтятся (сервер их не гейтит); контроль — 9915 гейтится.
//   Состояние (charId, вес, сумка, очередь окон, ключи) восстанавливается в конце.
//
// Консоль: `asda.Npc.SelfTestWave2B [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки — «САМОТЕСТ-2Б», итог — «САМОТЕСТ-2Б ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
// RefuseDebugWhenOnline — офлайн-гейт самотестов   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaBytes.h"
#include "Npc/AsdaNpcDialogSubsystem.h"        // разговор: OpenForNpcId / DumpWindow / Close
#include "Npc/AsdaDonationSubsystem.h"         // пожертвование
#include "UI/AsdaHuntReportSubsystem.h"        // окно 56450
#include "UI/AsdaWindowSubsystem.h"            // FindWindow / SimulateClickAt / нумпад / очередь окон-сообщений
#include "UI/AsdaInventorySubsystem.h"         // подкладка предметов
#include "UI/AsdaItemTooltipSubsystem.h"       // владелец тултипа
#include "UI/AsdaTipPanel.h"                   // EAsdaTipOwner
#include "UI/AsdaNotice.h"                     // AsdaNotice::Loc — ожидаемые тексты
#include "UI/AsdaUiHelpers.h"                  // WidgetAbsRect / FormatPrintfArgs
#include "Items/AsdaItemTableSubsystem.h"      // имя предмета и звание (ожидания)
#include "Items/AsdaItemDbRows.h"
#include "World/AsdaPossessGate.h"             // AsdaSoul / AsdaPossess::IsGatedOpcode
#include "Blueprint/UserWidget.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaWave2BTest, Log, All);

namespace AsdaWave2BLocal
{
	constexpr int32 kCharId    = 424242;   // свой charId на время прогона
	constexpr int32 kMarkA     = 22647;    // знак охоты (LinkCode 208), звание pricetable.dw05 = 99
	constexpr int32 kMarkB     = 23678;    // знак охоты (LinkCode 208), звание 207
	constexpr int32 kNotMark   = 20571;    // не знак (контроль: в сетку не попадает)
	constexpr int32 kCellA     = 3;        // ячейка сумки знака A (стопка 5)
	constexpr int32 kCellB     = 7;        // ячейка сумки знака B (стопка 12)
	constexpr int32 kCellDon1  = 10;       // 60073 ×4 (первая стопка)
	constexpr int32 kCellDon2  = 15;       // 60073 ×6 (последняя — уходит в 6132)
	constexpr int32 kCellOther = 20;       // не знак ×3

	// Тело 6151 (66 б): @0 charId, @4 запись (код, контейнер @8, слот @9, кол-во @13), @61 код, @62 вес.
	static TArray<uint8> Make6151(int32 Code, int32 ItemCode, int32 Cell, int32 Amount, int32 Weight)
	{
		TArray<uint8> B; B.SetNumZeroed(66);
		AsdaBytes::WrI32(B.GetData() + 0, kCharId);
		AsdaBytes::WrI32(B.GetData() + 4, ItemCode);
		B[8] = 2;
		AsdaBytes::WrI16(B.GetData() + 9, Cell);
		AsdaBytes::WrI32(B.GetData() + 13, Amount);
		B[61] = (uint8)Code;
		AsdaBytes::WrI32(B.GetData() + 62, Weight);
		return B;
	}
	// Тело 6133 (66 б): @0 charId, @4 код, @5 запись (код, контейнер @9, слот @10, кол-во @14), @62 вес.
	static TArray<uint8> Make6133(int32 Code, int32 Cell, int32 Amount, int32 Weight)
	{
		TArray<uint8> B; B.SetNumZeroed(66);
		AsdaBytes::WrI32(B.GetData() + 0, kCharId);
		B[4] = (uint8)Code;
		AsdaBytes::WrI32(B.GetData() + 5, UAsdaDonationSubsystem::kItemCode);
		B[9] = 2;
		AsdaBytes::WrI16(B.GetData() + 10, Cell);
		AsdaBytes::WrI32(B.GetData() + 14, Amount);
		AsdaBytes::WrI32(B.GetData() + 62, Weight);
		return B;
	}
}

struct FAsdaWave2BSelfTest
{
	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено

	// Перехваченный исходящий пакет.
	struct FCap { uint16 Op = 0; TArray<uint8> P; };
	TSharedRef<TArray<FCap>> Caps = MakeShared<TArray<FCap>>();   // перехват (весь трафик)
	uint32 Gen = 0;                                               // поколение перехватчика
	TArray<TFunction<void(UWorld*)>> Steps;                       // шаги (по одному за тик планировщика)
	int32 CharIdWas = 0;                                          // свой charId до прогона
	int32 WeightWas = -1;                                         // вес до прогона
	TArray<TArray<uint8>> BagWas;                                 // записи контейнера 2 до прогона
	FString BtnHunt;                                              // имя кнопки меню Эллы с кодом 30068
	FString BtnDon;                                               // имя кнопки меню NPC 163 с кодом 30051
	FBox2D CellBRect = FBox2D(ForceInit);                         // живой прямоугольник ячейки знака B

	// Одна проверка.
	void Expect(bool b, const FString& Name, const FString& Detail = FString())
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaWave2BTest, Log, TEXT("САМОТЕСТ-2Б %s: %s%s%s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name,
			Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
	}
	// Сколько перехвачено пакетов опкода; последний — в Out.
	int32 Count(uint16 Op, TArray<uint8>* Out = nullptr) const
	{
		int32 N = 0;
		for (const FCap& C : *Caps) { if (C.Op == Op) { ++N; if (Out) { *Out = C.P; } } }
		return N;
	}
	// Подсистемы мира.
	struct FSubs
	{
		UGameInstance* GI = nullptr; UAsdaNetSubsystem* Net = nullptr; UAsdaNpcDialogSubsystem* Dlg = nullptr;
		UAsdaHuntReportSubsystem* Hunt = nullptr; UAsdaDonationSubsystem* Don = nullptr; UAsdaWindowSubsystem* WM = nullptr;
		UAsdaInventorySubsystem* Inv = nullptr; UAsdaItemTableSubsystem* IT = nullptr; UAsdaItemTooltipSubsystem* Tip = nullptr;
		bool Ok() const { return GI && Net && Dlg && Hunt && Don && WM && Inv && IT && Tip; }
	};
	static FSubs Get(UWorld* W)
	{
		FSubs S;
		S.GI = W ? W->GetGameInstance() : nullptr;
		if (!S.GI) { return S; }
		S.Net = S.GI->GetSubsystem<UAsdaNetSubsystem>(); S.Dlg = S.GI->GetSubsystem<UAsdaNpcDialogSubsystem>();
		S.Hunt = S.GI->GetSubsystem<UAsdaHuntReportSubsystem>(); S.Don = S.GI->GetSubsystem<UAsdaDonationSubsystem>();
		S.WM = S.GI->GetSubsystem<UAsdaWindowSubsystem>(); S.Inv = S.GI->GetSubsystem<UAsdaInventorySubsystem>();
		S.IT = S.GI->GetSubsystem<UAsdaItemTableSubsystem>(); S.Tip = S.GI->GetSubsystem<UAsdaItemTooltipSubsystem>();
		return S;
	}
	// Живой прямоугольник узла окна (`GetCachedGeometry`). false — нет узла/геометрии.
	static bool RectOf(UUserWidget* Root, const TCHAR* Name, FBox2D& Out)
	{
		UWidget* W = Root ? Root->GetWidgetFromName(FName(Name)) : nullptr;
		FVector2D Mn, Mx;
		if (!W || !AsdaUi::WidgetAbsRect(W, Mn, Mx) || (Mx - Mn).IsNearlyZero()) { return false; }
		Out = FBox2D(Mn, Mx);
		return true;
	}
	// Клик по узлу окна реестра `WinId` (или окна 56450 при пустом WinId) — настоящими событиями Slate.
	bool Click(UWorld* W, const FString& Name, const TCHAR* WinId = nullptr)
	{
		const FSubs S = Get(W);
		UUserWidget* Root = (S.WM && WinId) ? S.WM->FindWindow(FName(WinId)) : (S.Hunt ? S.Hunt->DebugWidget() : nullptr);
		FBox2D R(ForceInit);
		if (!S.WM || !RectOf(Root, *Name, R)) { Expect(false, FString::Printf(TEXT("клик по %s"), *Name), TEXT("нет живой геометрии")); return false; }
		S.WM->SimulateClickAt(R.GetCenter(), /*HoldFrames*/ 3);
		return true;
	}
	// Курсор в центр узла окна 56450 (наведение без клика).
	bool Hover(UWorld* W, const TCHAR* Name)
	{
		const FSubs S = Get(W);
		FBox2D R(ForceInit);
		if (!S.Hunt || !RectOf(S.Hunt->DebugWidget(), Name, R)) { Expect(false, FString::Printf(TEXT("наведение на %s"), Name), TEXT("нет геометрии")); return false; }
		FSlateApplication::Get().SetCursorPos(R.GetCenter());
		return true;
	}
	// Ввести число в поле нумпада 1412.
	static bool NumPadType(UAsdaWindowSubsystem* WM, int32 Qty)
	{
		UUserWidget* NW = WM ? WM->FindWindow(FName(TEXT("NUMPAD_WIN"))) : nullptr;
		UEditableText* E = NW ? Cast<UEditableText>(NW->GetWidgetFromName(TEXT("NUMPAD_NUMBOX"))) : nullptr;
		if (!E) { return false; }
		E->SetText(FText::FromString(FString::FromInt(Qty)));
		return true;
	}
	// Имя кнопки меню разговора с кодом Code (видимой), пусто — нет.
	static FString FindMenuButton(const FString& Dump, int32 Code)
	{
		TArray<FString> Lines;
		Dump.ParseIntoArrayLines(Lines);
		const FString Want = FString::Printf(TEXT("код=%d"), Code);
		for (const FString& L : Lines)
		{
			const FString T = L.TrimStart();
			if (T.StartsWith(TEXT("NPC_SHOP_BUT")) && T.Contains(Want) && T.Contains(TEXT("видим=1"))) { return T.Left(14); }
		}
		return FString();
	}
	// Текст узла окна 56450.
	static FString TextOf(UAsdaHuntReportSubsystem* Hunt, const TCHAR* Name)
	{
		UTextBlock* Tb = (Hunt && Hunt->DebugWidget()) ? Cast<UTextBlock>(Hunt->DebugWidget()->GetWidgetFromName(FName(Name))) : nullptr;
		return Tb ? Tb->GetText().ToString() : FString(TEXT("<нет узла>"));
	}
	// Имя звания знака по данным (ожидание теста, независимо от окна).
	static FString TitleOf(UAsdaItemTableSubsystem* IT, int32 Code)
	{
		FAsdaPriceRow P; FAsdaTitleRow T;
		return (IT && IT->GetPrice(Code, P) && IT->GetTitle(P.dw05, T)) ? T.Name : FString();
	}
	// Кол-во в ячейке контейнера 2 (−1 — пусто).
	static int32 BagAmount(UAsdaInventorySubsystem* Inv, int32 Cell)
	{
		const FAsdaItemInstance* It = Inv ? Inv->GetCell(2, Cell) : nullptr;
		return (It && !It->IsEmpty()) ? It->Amount : -1;
	}

	// ═══ Подготовка ═══
	void Setup(UWorld* W)
	{
		using namespace AsdaWave2BLocal;
		FSubs S = Get(W);
		if (!S.Ok()) { Expect(false, TEXT("нет подсистем")); return; }
		CharIdWas = S.Net->SelectedCharId; WeightWas = S.Net->SelfWeightCur;
		S.Net->SelectedCharId = kCharId;
		BagWas.Reset();
		for (int32 i = 0; i < 60; ++i) { const FAsdaItemInstance* It = S.Inv->GetCell(2, i); BagWas.Add(It ? It->Raw : TArray<uint8>()); }
		for (int32 i = 0; i < 60; ++i) { S.Inv->DebugPutItem(2, i, 0, 0); }
		S.Inv->DebugPutItem(2, kCellA, kMarkA, 5);
		S.Inv->DebugPutItem(2, kCellB, kMarkB, 12);
		S.Inv->DebugPutItem(2, kCellDon1, UAsdaDonationSubsystem::kItemCode, 4);
		S.Inv->DebugPutItem(2, kCellDon2, UAsdaDonationSubsystem::kItemCode, 6);
		S.Inv->DebugPutItem(2, kCellOther, kNotMark, 3);
		S.Hunt->ResetSessionState(EAsdaSessionResetReason::EnteringWorld);   // выбор знака — с нуля
		S.WM->ClearMsgBoxQueue(TEXT("самотест 2-Б: старт"));
		Caps->Reset();
		TSharedRef<TArray<FCap>> C = Caps;
		Gen = AsdaSelfTestKit::ArmCapture(S.Net, [C](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			FCap X; X.Op = Op; if (P && Len > 0) { X.P.Append(P, Len); } C->Add(MoveTemp(X));
		}, 0, TEXT("asda.Npc.SelfTestWave2B"));
		// Данные: оба знака с LinkCode 208 и званием, 60073 — подтип 149.
		const FAsdaItemRecord* RA = S.IT->GetItem(kMarkA);
		const FAsdaItemRecord* RD = S.IT->GetItem(UAsdaDonationSubsystem::kItemCode);
		Expect(RA && RA->LinkCode == 208 && !TitleOf(S.IT, kMarkA).IsEmpty() && !TitleOf(S.IT, kMarkB).IsEmpty(),
			TEXT("данные: знаки 22647/23678 — LinkCode 208, звания в pricetable.dw05/title_list"),
			FString::Printf(TEXT("звания «%s» / «%s»"), *TitleOf(S.IT, kMarkA), *TitleOf(S.IT, kMarkB)));
		Expect(RD && RD->ItemSubType == 149, TEXT("данные: 60073 — подтип 149 (гейт сервера +27 == 149)"));

		// Разговор с Эллой.
		Expect(S.Dlg->OpenForNpcId(107), TEXT("разговор с NPC 107 открыт"));
		BtnHunt = FindMenuButton(S.Dlg->DumpWindow(), 30068);
		Expect(!BtnHunt.IsEmpty(), TEXT("у Эллы видна кнопка меню с кодом 30068"), BtnHunt);
	}

	void BuildSteps()
	{
		using namespace AsdaWave2BLocal;
		// A. Клик по кнопке 30068.
		Steps.Add([this](UWorld* W) { if (!BtnHunt.IsEmpty()) { Click(W, BtnHunt, TEXT("NPC_LINE_NPC")); } });
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.Hunt->IsOpen() && !S.Dlg->IsDialogOpen(), TEXT("A: клик по кнопке 30068 → окно 56450 открыто, разговор закрыт (5045)"),
				FString::Printf(TEXT("окно=%d разговор=%d"), S.Hunt->IsOpen() ? 1 : 0, S.Dlg->IsDialogOpen() ? 1 : 0));
			Expect(S.Hunt->DebugCellCode(0) == kMarkA && S.Hunt->DebugCellText(0) == TEXT("5")
				&& S.Hunt->DebugCellCode(1) == kMarkB && S.Hunt->DebugCellText(1) == TEXT("12") && S.Hunt->DebugCellCode(2) == 0,
				TEXT("A: сетка — знаки подряд с числом стопки (22647×5, 23678×12), третья ячейка пуста"),
				FString::Printf(TEXT("[0]=%d «%s» [1]=%d «%s» [2]=%d"), S.Hunt->DebugCellCode(0), *S.Hunt->DebugCellText(0),
					S.Hunt->DebugCellCode(1), *S.Hunt->DebugCellText(1), S.Hunt->DebugCellCode(2)));
			bool bLeak = false;
			for (int32 i = 0; i < UAsdaHuntReportSubsystem::kCells; ++i)
			{
				const int32 Cd = S.Hunt->DebugCellCode(i);
				bLeak |= (Cd == kNotMark || Cd == UAsdaDonationSubsystem::kItemCode);
			}
			Expect(!bLeak, TEXT("A: контроль — не-знак 20571 и 60073 в сетку НЕ попали"));
			Expect(TextOf(S.Hunt, TEXT("Hunt_report_Text_Move")) == AsdaNotice::Loc(S.GI, 110210)
				&& TextOf(S.Hunt, TEXT("Hunt_report_Ok_Text")) == AsdaNotice::Loc(S.GI, 110211)
				&& TextOf(S.Hunt, TEXT("Hunt_report_No_Text")) == AsdaNotice::Loc(S.GI, 110212),
				TEXT("A: подписи 56494/56493/56492 = loc 110210/110211/110212"),
				FString::Printf(TEXT("«%s» / «%s» / «%s»"), *TextOf(S.Hunt, TEXT("Hunt_report_Text_Move")),
					*TextOf(S.Hunt, TEXT("Hunt_report_Ok_Text")), *TextOf(S.Hunt, TEXT("Hunt_report_No_Text"))));
			UE_LOG(LogAsdaWave2BTest, Log, TEXT("САМОТЕСТ-2Б структура окна:\n%s"), *S.Hunt->DumpControls());
			FBox2D Win(ForceInit), Ok(ForceInit), Cell(ForceInit);
			const bool bGeo = RectOf(S.Hunt->DebugWidget(), TEXT("Hunt_report_Win_0_4"), Win) && RectOf(S.Hunt->DebugWidget(), TEXT("Hunt_report_Ok"), Ok)
				&& RectOf(S.Hunt->DebugWidget(), TEXT("Hunt_report_Mark_1_53_0"), Cell);
			Expect(bGeo && Ok.GetSize().X > 50 && Cell.GetSize().X > 20, TEXT("A: живая геометрия окна — тело, кнопка 56493, ячейка 56461 непустые"),
				FString::Printf(TEXT("тело %.0fx%.0f, кнопка %.0fx%.0f, ячейка %.0fx%.0f"), Win.GetSize().X, Win.GetSize().Y, Ok.GetSize().X, Ok.GetSize().Y, Cell.GetSize().X, Cell.GetSize().Y));
			Caps->Reset();
			Click(W, TEXT("Hunt_report_Ok"));   // без выбора
		});
		// B. Контроль: без выбора 6150 нет; клик по ячейке знака B.
		Steps.Add([this](UWorld* W)
		{
			Expect(Count(6150) == 0, TEXT("B: контроль — «Отнести знаки» без выбора: 6150 НЕ ушёл"), FString::Printf(TEXT("6150=%d"), Count(6150)));
			Click(W, TEXT("Hunt_report_Mark_2_54_0"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.WM->IsNumPadOpen(), TEXT("B: ЛКМ по ячейке знака → нумпад режима 9 открыт"));
			Expect(NumPadType(S.WM, 7), TEXT("B: ввод 7 в поле нумпада"));
			Click(W, TEXT("NUMPAD_OK"), TEXT("NUMPAD_WIN"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			const FString WantTitle = AsdaUi::FormatPrintfArgs(AsdaNotice::Loc(S.GI, 110214), { TitleOf(S.IT, kMarkB) });
			Expect(!S.WM->IsNumPadOpen() && S.Hunt->DebugSelectedBagCell() == kCellB && S.Hunt->DebugSelectedQty() == 7
				&& S.Hunt->DebugResultQtyText() == TEXT("7") && S.Hunt->DebugResultText() == WantTitle,
				TEXT("B: «OK» нумпада → выбран знак B ×7: число в 56451, звание 110214 в 56452"),
				FString::Printf(TEXT("сумка=%d кол-во=%d 56451=«%s» 56452=«%s» (ждём «%s»)"), S.Hunt->DebugSelectedBagCell(), S.Hunt->DebugSelectedQty(),
					*S.Hunt->DebugResultQtyText(), *S.Hunt->DebugResultText(), *WantTitle));
			FBox2D Fr(ForceInit), Ce(ForceInit);
			const bool bFr = RectOf(S.Hunt->DebugWidget(), TEXT("Hunt_report_Select_83_0"), Fr);
			const bool bCe = RectOf(S.Hunt->DebugWidget(), TEXT("Hunt_report_Mark_2_54_0"), Ce);
			CellBRect = Ce;
			Expect(S.Hunt->DebugSelectFrameShown() && bFr && bCe && (Fr.Min - Ce.Min).GetAbsMax() <= 1.5 && (Fr.Max - Ce.Max).GetAbsMax() <= 1.5,
				TEXT("B: рамка 56491 видна и стоит РОВНО на ячейке знака (живые прямоугольники)"),
				FString::Printf(TEXT("рамка (%.0f,%.0f)-(%.0f,%.0f), ячейка (%.0f,%.0f)-(%.0f,%.0f)"), Fr.Min.X, Fr.Min.Y, Fr.Max.X, Fr.Max.Y, Ce.Min.X, Ce.Min.Y, Ce.Max.X, Ce.Max.Y));
			Click(W, TEXT("Hunt_report_Mark_4_56_0"));   // пустая ячейка
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(!S.WM->IsNumPadOpen() && S.Hunt->DebugSelectedBagCell() == kCellB, TEXT("B: контроль — клик по ПУСТОЙ ячейке нумпад не открывает, выбор цел"));
			Click(W, TEXT("Hunt_report_Mark_1_53_0"));   // знак A → нумпад
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.WM->IsNumPadOpen(), TEXT("B: клик по знаку A → нумпад"));
			NumPadType(S.WM, 3);
			Click(W, TEXT("NUMPAD_CLOSED"), TEXT("NUMPAD_WIN"));   // «Отмена»
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(!S.WM->IsNumPadOpen() && S.Hunt->DebugSelectedBagCell() == kCellB && S.Hunt->DebugSelectedQty() == 7,
				TEXT("B: контроль — «Отмена» нумпада выбор не меняет (знак B ×7)"),
				FString::Printf(TEXT("сумка=%d кол-во=%d"), S.Hunt->DebugSelectedBagCell(), S.Hunt->DebugSelectedQty()));
			Click(W, TEXT("Hunt_report_X"));   // ревизия М3: закрыть → открыть — выбор цел
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(!S.Hunt->IsOpen() && S.Hunt->DebugSelectedBagCell() == kCellB && S.Hunt->DebugSelectedQty() == 7,
				TEXT("B2: крестик → окно закрыто, выбор НЕ сброшен (эталонный uiSowelWindowToggle)"));
			S.Hunt->OpenWindow();   // тот же вход, что у кнопки 30068
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.Hunt->IsOpen() && S.Hunt->DebugSelectedBagCell() == kCellB && S.Hunt->DebugResultQtyText() == TEXT("7")
				&& S.Hunt->DebugSelectFrameShown() && !S.Hunt->DebugResultText().IsEmpty(),
				TEXT("B2: повторное открытие → выбранный знак, число 7, звание и рамка на месте"),
				FString::Printf(TEXT("56451=«%s» 56452=«%s»"), *S.Hunt->DebugResultQtyText(), *S.Hunt->DebugResultText()));
			// ESC: окна 56450 нет в эталонной таблице (`kEscCloseWindows`) — проход ESC его не закрывает; контроль — окно ремонта в таблице есть.
			const bool bInTable = S.WM->IsEscCloseWindow(FName(TEXT("Hunt_report_Win")));
			const bool bCtl = S.WM->IsEscCloseWindow(FName(TEXT("INVEN_Repair_Win")));
			// ★ 07.11.2026: проверка оригинала идёт с выключенным списком порта; с ключом 1 окно закрывается (самотест `asda.UI.SelfTestEscExtra`).
			IConsoleVariable* EscExtra = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.UI.EscExtraWindows"));
			const int32 EscExtraWas = EscExtra ? EscExtra->GetInt() : 0;
			if (EscExtra) { EscExtra->Set(0, ECVF_SetByConsole); }
			S.WM->CloseTopmostOnEsc();
			if (EscExtra) { EscExtra->Set(EscExtraWas, ECVF_SetByConsole); }
			Expect(!bInTable && bCtl && S.Hunt->IsOpen(), TEXT("B2: ESC окно 56450 НЕ закрывает (нет в kEscCloseWindows); контроль — 57300 в таблице есть"),
				FString::Printf(TEXT("в таблице=%d контроль=%d открыто=%d"), bInTable ? 1 : 0, bCtl ? 1 : 0, S.Hunt->IsOpen() ? 1 : 0));
			if (S.WM->IsOpen(FName(TEXT("SYSTEM_MenuWIN_NEW")))) { S.WM->CloseTopmostOnEsc(); }   // ESC-меню, если проход его открыл
			Caps->Reset();
			Click(W, TEXT("Hunt_report_Ok"));
		});
		// C. 6150.
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			TArray<uint8> P; const int32 N = Count(6150, &P);
			Expect(N == 1 && P.Num() == 11 && AsdaBytes::RdI32(P.GetData()) == kCharId && AsdaBytes::RdI16(P.GetData() + 4) == kCellB
				&& P[6] == 2 && AsdaBytes::RdI32(P.GetData() + 7) == 7,
				TEXT("C: клик «Отнести знаки» → ровно один 6150, тело 11 б: charId, слот 7, контейнер 2, кол-во 7"),
				FString::Printf(TEXT("6150×%d длина=%d @0=%d @4=%d @6=%d @7=%d"), N, P.Num(), P.Num() >= 4 ? AsdaBytes::RdI32(P.GetData()) : 0,
					P.Num() >= 6 ? AsdaBytes::RdI16(P.GetData() + 4) : 0, P.Num() >= 7 ? (int32)P[6] : 0, P.Num() >= 11 ? AsdaBytes::RdI32(P.GetData() + 7) : 0));
			// D. Приём 6151 штатно.
			const FString ItemB = S.IT->GetItemName(kMarkB), TitleB = TitleOf(S.IT, kMarkB);
			S.WM->ClearMsgBoxQueue(TEXT("2Б: 6151"));
			S.Net->DebugFeedPacket(6151, Make6151(2, kMarkB, kCellB, 5, 123)); S.Hunt->PumpPending();
			const FString W2 = AsdaUi::FormatPrintfArgs(AsdaNotice::Loc(S.GI, 110215), { ItemB, TitleB });
			Expect(S.WM->GetMsgBoxQueueTopText() == W2 && S.Hunt->DebugSelectedBagCell() == -1 && BagAmount(S.Inv, kCellB) == 5
				&& S.Hunt->DebugCellText(1) == TEXT("5") && S.Net->SelfWeightCur == 123 && !S.Hunt->DebugSelectFrameShown() && S.Hunt->DebugResultText().IsEmpty(),
				TEXT("D: 6151 код 2 → окно 110215 (предмет, звание), выбор сброшен, стопка 5, вес 123, рамка и звание погашены"),
				FString::Printf(TEXT("«%s» выбор=%d стопка=%d ячейка=«%s» вес=%d"), *S.WM->GetMsgBoxQueueTopText(), S.Hunt->DebugSelectedBagCell(),
					BagAmount(S.Inv, kCellB), *S.Hunt->DebugCellText(1), S.Net->SelfWeightCur));
			S.WM->ClearMsgBoxQueue(TEXT("2Б: 6151/1"));
			S.Net->DebugFeedPacket(6151, Make6151(1, kMarkB, kCellB, 3, 150)); S.Hunt->PumpPending();
			const FString W1 = AsdaUi::FormatPrintfArgs(AsdaNotice::Loc(S.GI, 110213), { ItemB });
			Expect(S.WM->GetMsgBoxQueueTopText() == W1 && BagAmount(S.Inv, kCellB) == 3 && S.Net->SelfWeightCur == 150,
				TEXT("D: 6151 код 1 → окно 110213 (имя предмета), стопка 3, вес 150"), S.WM->GetMsgBoxQueueTopText());
			S.WM->ClearMsgBoxQueue(TEXT("2Б: 6151/9"));
			S.Net->DebugFeedPacket(6151, Make6151(9, 0, 0, 0, 999)); S.Hunt->PumpPending();
			Expect(S.WM->GetMsgBoxQueueTopText() == AsdaNotice::Loc(S.GI, 110405) && S.Net->SelfWeightCur == 150,
				TEXT("D: 6151 код 9 → окно 110405 «Звание уже получено», вес НЕ тронут"), S.WM->GetMsgBoxQueueTopText());
			S.WM->ClearMsgBoxQueue(TEXT("2Б: 6151/6"));
			S.Net->DebugFeedPacket(6151, Make6151(6, 0, 0, 0, 999)); S.Hunt->PumpPending();
			const FString W6 = AsdaUi::FormatPrintfArgs(AsdaNotice::Loc(S.GI, 110404), { TEXT("6") });
			Expect(S.WM->GetMsgBoxQueueTopText() == W6 && S.Net->SelfWeightCur == 150 && BagAmount(S.Inv, kCellB) == 3,
				TEXT("D: 6151 код 6 → окно 110404 «…6…», вес и сумка не тронуты (контроль к 1/2)"), S.WM->GetMsgBoxQueueTopText());
			S.WM->ClearMsgBoxQueue(TEXT("2Б: 6151/13"));
			S.Net->DebugFeedPacket(6151, Make6151(13, 0, 0, 0, 999)); S.Hunt->PumpPending();
			Expect(S.WM->GetMsgBoxQueueNum() == 0, TEXT("D: контроль — 6151 код 13 (ветки нет) → молча"));
			S.Net->DebugFeedPacket(6151, Make6151(1, kMarkA, kCellA, 0, 140)); S.Hunt->PumpPending();
			Expect(BagAmount(S.Inv, kCellA) == -1 && S.Hunt->DebugCellCode(0) == kMarkB && S.Hunt->DebugCellCode(1) == 0,
				TEXT("D: 6151 код 1 с кол-вом 0 → ячейка сумки очищена, знак A ушёл из сетки (B сдвинулся в ячейку 0)"),
				FString::Printf(TEXT("сумка[3]=%d сетка[0]=%d сетка[1]=%d"), BagAmount(S.Inv, kCellA), S.Hunt->DebugCellCode(0), S.Hunt->DebugCellCode(1)));
			S.WM->ClearMsgBoxQueue(TEXT("2Б: после 6151"));
			// E. Наведение.
			Hover(W, TEXT("Hunt_report_Mark_1_53_0"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.Hunt->DebugTipCell() == 0 && S.Tip->GetTipOwner() == EAsdaTipOwner::HuntReport,
				TEXT("E: наведение на ячейку знака → тултип предмета (владелец HuntReport)"),
				FString::Printf(TEXT("ячейка=%d владелец=%d"), S.Hunt->DebugTipCell(), (int32)S.Tip->GetTipOwner()));
			Hover(W, TEXT("Hunt_report_Text_Move"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.Hunt->DebugTipCell() == -1 && S.Tip->GetTipOwner() != EAsdaTipOwner::HuntReport,
				TEXT("E: курсор ушёл с ячейки → тултип снят (контроль к наведению)"));
			// ревизия М3: [ПОРТ] Д1 — ключ 0 (оригинал): изменение сумки извне окно НЕ перерисовывает.
			AsdaCVarSave::SetForTest(TEXT("asda.HuntReport.LiveRefresh"), 0);   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): запомнить исходное (прежде Set(0) с SetByCode)
			S.Inv->DebugPutItem(2, 30, kMarkA, 9);
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.Hunt->DebugCellCode(1) == 0, TEXT("E2: контроль Д1 — ключ LiveRefresh 0: знак, положенный в сумку, в сетке НЕ появился (как оригинал)"),
				FString::Printf(TEXT("сетка[1]=%d"), S.Hunt->DebugCellCode(1)));
			AsdaCVarSave::SetForTest(TEXT("asda.HuntReport.LiveRefresh"), 1);
			S.Inv->DebugPutItem(2, 31, kMarkB, 4);
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.Hunt->DebugCellCode(1) == kMarkA && S.Hunt->DebugCellText(1) == TEXT("9") && S.Hunt->DebugCellCode(2) == kMarkB,
				TEXT("E2: [ПОРТ] Д1 — ключ LiveRefresh 1: сетка перерисована по изменению сумки (оба знака видны)"),
				FString::Printf(TEXT("сетка[1]=%d «%s» сетка[2]=%d"), S.Hunt->DebugCellCode(1), *S.Hunt->DebugCellText(1), S.Hunt->DebugCellCode(2)));
			// Д2: 29 знаков в сумке (контроль) — ячейка 29 пуста.
			for (int32 i = 0; i < 60; ++i) { S.Inv->DebugPutItem(2, i, 0, 0); }
			for (int32 i = 0; i < 29; ++i) { S.Inv->DebugPutItem(2, i, kMarkA, 1); }
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.Hunt->DebugCellCode(28) == kMarkA && S.Hunt->DebugCellCode(29) == 0, TEXT("E3: контроль Д2 — 29 знаков → ячейка 29 пуста"));
			for (int32 i = 29; i < 35; ++i) { S.Inv->DebugPutItem(2, i, kMarkA, 1); }   // 35 знаков
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.Hunt->DebugCellCode(29) == kMarkA, TEXT("E3: [ПОРТ] Д2 — 35 знаков → заполнено ровно 30 ячеек, переполнения нет"));
			const FString Dump = S.Hunt->DumpControls();
			Expect(!Dump.Contains(TEXT(" НЕТ ")), TEXT("E3: все контролы окна целы (рамка 56491, кнопка 56493 — у оригинала 31-й знак их затирал)"));
			Click(W, TEXT("Hunt_report_Mark_30_82_0"));   // 30-я ячейка → нумпад
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.WM->IsNumPadOpen(), TEXT("E3: клик по 30-й ячейке → нумпад"));
			NumPadType(S.WM, 1);
			Click(W, TEXT("NUMPAD_OK"), TEXT("NUMPAD_WIN"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			FBox2D Fr(ForceInit), Ce(ForceInit);
			const bool bG = RectOf(S.Hunt->DebugWidget(), TEXT("Hunt_report_Select_83_0"), Fr) && RectOf(S.Hunt->DebugWidget(), TEXT("Hunt_report_Mark_30_82_0"), Ce);
			Expect(S.Hunt->DebugSelectedBagCell() == 29 && bG && (Fr.Min - Ce.Min).GetAbsMax() <= 1.5,
				TEXT("E3: выбор 30-го знака — рамка 56491 ровно на ячейке 56490"),
				FString::Printf(TEXT("выбор=%d рамка (%.0f,%.0f) ячейка (%.0f,%.0f)"), S.Hunt->DebugSelectedBagCell(), Fr.Min.X, Fr.Min.Y, Ce.Min.X, Ce.Min.Y));
			// ревизия М2: нумпад режима 9 закрывается уходом от NPC (эталонный 5045 закрывает ЛЮБОЙ нумпад без колбэка).
			Expect(S.Dlg->OpenForNpcId(107), TEXT("E4: разговор с NPC 107 открыт поверх окна 56450"));
			S.Hunt->OnCellClicked(0);   // ЛКМ по ячейке — тот же обработчик, что у мыши (окно может быть под разговором)
			const bool bOpen = S.WM->IsNumPadOpen();
			S.Dlg->Close();
			Expect(bOpen && !S.WM->IsNumPadOpen() && S.Hunt->DebugSelectedBagCell() == 29,
				TEXT("E4: нумпад режима 9 открыт → уход от NPC закрыл его без колбэка, выбор не изменился"),
				FString::Printf(TEXT("до=%d после=%d выбор=%d"), bOpen ? 1 : 0, S.WM->IsNumPadOpen() ? 1 : 0, S.Hunt->DebugSelectedBagCell()));
			// Возврат сумки к раскладке для F (пожертвование): 60073 ×4 и ×6.
			for (int32 i = 0; i < 60; ++i) { S.Inv->DebugPutItem(2, i, 0, 0); }
			S.Inv->DebugPutItem(2, kCellB, kMarkB, 3);
			S.Inv->DebugPutItem(2, kCellDon1, UAsdaDonationSubsystem::kItemCode, 4);
			S.Inv->DebugPutItem(2, kCellDon2, UAsdaDonationSubsystem::kItemCode, 6);
			if (!S.Hunt->IsOpen()) { S.Hunt->OpenWindow(); }
		});
		Steps.Add([this](UWorld* W) { Click(W, TEXT("Hunt_report_X")); });
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(!S.Hunt->IsOpen(), TEXT("E: крестик 56499 → окно закрыто"));
			// F. Пожертвование: разговор с NPC 163.
			Expect(S.Dlg->OpenForNpcId(163), TEXT("F: разговор с NPC 163 открыт"));
			BtnDon = FindMenuButton(S.Dlg->DumpWindow(), 30051);
			Expect(!BtnDon.IsEmpty(), TEXT("F: у NPC 163 видна кнопка меню с кодом 30051"), BtnDon);
			IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Npc.DonationStackMax"));
			const int32 MaxFix = S.Don->ComputeMax();
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Npc.DonationStackMax"), 0); }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): запомнить исходное (прежде Set(0) с SetByCode)
			const int32 MaxOrig = S.Don->ComputeMax();
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Npc.DonationStackMax"), 1); }
			Expect(MaxFix == 6 && MaxOrig == 10, TEXT("F: максимум нумпада — стопка, что уйдёт (6) [ПОРТ] Д3; контроль ключом 0 — сумма (10)"),
				FString::Printf(TEXT("исправл.=%d оригинал=%d"), MaxFix, MaxOrig));
			Caps->Reset();
		});
		// Клик — следующим шагом: окну разговора нужен кадр раскладки, иначе у кнопки ещё нет живой геометрии.
		Steps.Add([this](UWorld* W) { if (!BtnDon.IsEmpty()) { Click(W, BtnDon, TEXT("NPC_LINE_NPC")); } });
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.WM->IsNumPadOpen() && S.Don->IsNumPadOwned() && S.Dlg->IsDialogOpen(),
				TEXT("F: клик по кнопке 30051 → нумпад режима 6 открыт, разговор остаётся"));
			NumPadType(S.WM, 4);
			Click(W, TEXT("NUMPAD_OK"), TEXT("NUMPAD_WIN"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			TArray<uint8> P; const int32 N = Count(6132, &P);
			Expect(N == 1 && P.Num() == 13 && AsdaBytes::RdI32(P.GetData()) == kCharId && AsdaBytes::RdI32(P.GetData() + 4) == UAsdaDonationSubsystem::kItemCode
				&& P[8] == 2 && AsdaBytes::RdI16(P.GetData() + 9) == kCellDon2 && AsdaBytes::RdI16(P.GetData() + 11) == 4 && S.Dlg->IsDialogOpen(),
				TEXT("F: ввод 4 + «OK» → ровно один 6132: charId, 60073, контейнер 2, слот ПОСЛЕДНЕЙ стопки (15), кол-во 4; разговор открыт"),
				FString::Printf(TEXT("6132×%d длина=%d слот=%d кол-во=%d"), N, P.Num(), P.Num() >= 11 ? AsdaBytes::RdI16(P.GetData() + 9) : 0, P.Num() >= 13 ? AsdaBytes::RdI16(P.GetData() + 11) : 0));
			Click(W, BtnDon, TEXT("NPC_LINE_NPC"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.WM->IsNumPadOpen(), TEXT("F: повторный клик 30051 → нумпад снова открыт"));
			S.Dlg->Close();   // уход от NPC
			Expect(!S.WM->IsNumPadOpen() && !S.Don->IsNumPadOwned() && Count(6132) == 1,
				TEXT("F: уход от NPC с открытым нумпадом → нумпад закрыт без отправки (эталонный 5045)"),
				FString::Printf(TEXT("нумпад=%d 6132×%d"), S.WM->IsNumPadOpen() ? 1 : 0, Count(6132)));
			const bool bNo = S.Don->OnNumPadAccepted(11);
			const bool bYes = S.Don->OnNumPadAccepted(2);
			Expect(!bNo && bYes && Count(6132) == 2, TEXT("F: общий счёт 10 < 11 → не шлёт; контроль 2 → шлёт"),
				FString::Printf(TEXT("11→%d 2→%d 6132×%d"), bNo ? 1 : 0, bYes ? 1 : 0, Count(6132)));
			// G. Приём 6133.
			S.WM->ClearMsgBoxQueue(TEXT("2Б: 6133"));
			S.Net->DebugFeedPacket(6133, Make6133(1, kCellDon2, 2, 77)); S.Don->PumpPending();
			Expect(BagAmount(S.Inv, kCellDon2) == 2 && S.Net->SelfWeightCur == 77 && S.WM->GetMsgBoxQueueNum() == 0,
				TEXT("G: 6133 код 1 → кол-во ячейке 2, вес 77, нотисов нет"),
				FString::Printf(TEXT("кол-во=%d вес=%d окон=%d"), BagAmount(S.Inv, kCellDon2), S.Net->SelfWeightCur, S.WM->GetMsgBoxQueueNum()));
			S.Net->DebugFeedPacket(6133, Make6133(1, kCellDon1, 0, 70)); S.Don->PumpPending();
			Expect(BagAmount(S.Inv, kCellDon1) == -1, TEXT("G: 6133 код 1 с кол-вом 0 → ячейка удалена"));
			S.Net->DebugFeedPacket(6133, Make6133(10, kCellDon2, 99, 999)); S.Don->PumpPending();
			Expect(BagAmount(S.Inv, kCellDon2) == 2 && S.Net->SelfWeightCur == 70 && S.WM->GetMsgBoxQueueNum() == 0,
				TEXT("G: контроль — 6133 код 10 → ни сумки, ни веса, ни нотиса"));
			// H. Гейты.
			Expect(!AsdaSoul::IsGatedOpcode(6150) && !AsdaSoul::IsGatedOpcode(6132) && !AsdaPossess::IsGatedOpcode(6150) && !AsdaPossess::IsGatedOpcode(6132)
				&& AsdaSoul::IsGatedOpcode(9915),
				TEXT("H: 6150/6132 не гейтятся ни душой, ни одержимостью (сервер: case 0x1806/0x17F4 без sub_52EEA0); контроль — 9915 гейтится"));
		});
	}

	// Восстановление.
	void Finish(UWorld* W)
	{
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи прогона (HuntReport.LiveRefresh, Npc.DonationStackMax) — к исходному значению И приоритету
		//   SetBy (прежде шаги возвращали константу 1 с SetByCode). Первым — чтобы сработало и при раннем выходе ниже.
		AsdaCVarSave::RestoreAll(TEXT("asda.Npc.SelfTestWave2B"));
		FSubs S = Get(W);
		if (!S.Ok()) { return; }
		AsdaSelfTestKit::DisarmCapture(S.Net, Gen);
		if (S.WM->IsNumPadOpen()) { S.WM->CloseNumPad(false); }
		if (S.Dlg->IsDialogOpen()) { S.Dlg->Close(); }
		S.Hunt->ResetSessionState(EAsdaSessionResetReason::EnteringWorld);
		for (int32 i = 0; i < 60; ++i)
		{
			S.Inv->DebugPutItem(2, i, 0, 0);
			if (FAsdaItemInstance* It = S.Inv->GetCellMutable(2, i)) { if (BagWas.IsValidIndex(i) && BagWas[i].Num() >= 57) { It->SetRaw(BagWas[i].GetData(), BagWas[i].Num()); } }
		}
		S.Net->SelectedCharId = CharIdWas;
		S.Net->SetSelfWeight(WeightWas, -1, TEXT("самотест 2-Б: восстановление"));
		S.WM->ClearMsgBoxQueue(TEXT("самотест 2-Б: конец"));
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaNpcSelfTestWave2B(
		TEXT("asda.Npc.SelfTestWave2B"),
		TEXT("asda.Npc.SelfTestWave2B [delay=N] [quit] — ОФЛАЙН-самотест волны 2-Б: Элла 30068 → окно 56450 (нумпад 9, 6150/6151) и "
		     "NPC 163 30051 → пожертвование (нумпад 6, 6132/6133), путём игрока (клики по живой геометрии) + штатный приём"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Npc.SelfTestWave2B"), KitArgs)) { return; }
			float Delay = 0.f;
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (был отдельный цикл по аргументам с одной этой веткой)
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(W2, TEXT("asda.Npc.SelfTestWave2B"), bQuit)) { return false; }   // Р-2: офлайн повторно — на отложенном старте
				TSharedRef<FAsdaWave2BSelfTest> T = MakeShared<FAsdaWave2BSelfTest>();
				T->Setup(W2);
				T->BuildSteps();
				TSharedRef<int32> Idx = MakeShared<int32>(0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, bQuit](float) -> bool
				{
					UWorld* W3 = WW.Get();
					if (!W3) { AsdaCVarSave::RestoreAll(TEXT("asda.Npc.SelfTestWave2B")); return false; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — и при уходе мира
					if (*Idx < T->Steps.Num()) { T->Steps[(*Idx)++](W3); return true; }
					T->Finish(W3);
					UE_LOG(LogAsdaWave2BTest, Log, TEXT("САМОТЕСТ-2Б ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.4f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaWave2BTest, Log, TEXT("САМОТЕСТ-2Б: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
