// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ИНВЕНТАРЬ ×2» (29.10.2026): `asda.SelfTest.InvX2 [quit]`. [ПОРТ]
//   Задача владельца 29.10: сумка (контейнер 1) 30 → 60, окно инвентаря — сетка 12 × 5 на обеих вкладках, нумерация
//   построчно (`INVEN_SET{r·12+c}` в (126+33c, 320+33r)), прокрутки нет; базовый вес ×2 — вес по проводу u16.
//   План — `Reports/inventory_x2/PLAN.md` §2, §3.4; разбор клиента — `_research/client.md`.
//   Путь игрока (закон `ui-check-game-path-geometry`, `diagnostic-must-use-real-path`), офлайн (-AsdaNoNet):
//     A. геометрия — ЖИВАЯ (`GetCachedGeometry` через `AsdaUi::WidgetAbsRect`) после отрисовки: 60 планов `INVEN_SET*`,
//        шаг 33, ячейка `r·12+c` в столбце c / ряду r, обе вкладки; полоса прокрутки скрыта; рамка окна в экране 1024×768;
//     B. модель — батч 4045 (контейнер 1) со слотами 30..59 штатным приёмом (`SimulateBatch` → Tick): вещи в модели и
//        иконки нарисованы в ячейках 30..59 вкладки «Снаряжение»;
//     C. ячейка 59: щелчок — выделение (`SelCell`, курсор 409 над ячейкой), наведение — подсказка предмета, перетаскивание
//        в ячейку 0 — 5000 (источник: контейнер 1 слот 59, цель: слот 0) под перехватом;
//     D. износ 6570 и итог ремонта 6572 со слотом 45 — прочность записи +13 меняется (прежде `Slot <= 29` выбрасывал);
//     E. ремонт «починить всё» с 50 вещами в сумке → ДВА пакета 6571 (41 + 9) — второй после ответа 6572; колесо над
//        сеткой окна ремонта листает список (ряд 0 → 1);
//     F. вес: 1017 (максимум 60000, текущий 40000) и 5451 (подтип 51, 60000/60000) — хранилище веса u16, цифры процента
//        мини-статуса 66 % и 100 %; ключ `asda.Weight.Unsigned 0` — прежнее знаковое чтение;
//     G. ключ `asda.Inven.ShellX2 0` — ёмкость сумки 30: запись в слот 45 не принимается, перенос в 45 отклонён.
//   Состояние (сумка, расходники, надетое, вес, блок 1017, ключи, перехват) восстанавливается в конце.
//   Итог: «ИНВ-X2 ИТОГ: ok=N fail=M». Запускать на карте мира (`--map world` пакета).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "AsdaWeightWire.h"
#include "Net/NetOpcodes.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"
#include "UI/AsdaRepairSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaInvX2Test, Log, All);

// Доступ к закрытому состоянию инвентаря и окна ремонта (объявлен `friend` в их заголовках).
struct FAsdaInvX2SelfTest
{
	// Полоса прокрутки сетки инвентаря видна.
	static bool ScrollShown(const UAsdaInventorySubsystem* I) { return I && I->IsScrollBarShown(); }
	// Наибольший ряд прокрутки текущей вкладки.
	static int32 MaxRow(const UAsdaInventorySubsystem* I) { return I ? I->GetMaxScrollRow() : -1; }
	// Выбранная ячейка сетки.
	static int32 SelCell(const UAsdaInventorySubsystem* I) { return I ? I->SelCell : -2; }
	// Курсор выбора 409.
	static const UWidget* GridSel(const UAsdaInventorySubsystem* I) { return I ? I->GridSel.Get() : nullptr; }
	// Окно ремонта: список сумки, ряд прокрутки, хвост «починить всё».
	static int32 RepTotal(const UAsdaRepairSubsystem* R) { return R ? R->BagScroll.Total : -1; }
	static int32 RepRow(const UAsdaRepairSubsystem* R) { return R ? R->BagScroll.Row : -1; }
	static int32 RepTail(const UAsdaRepairSubsystem* R) { return R ? R->RepairTailCont.Num() : -1; }
	static int32 RepChunks(const UAsdaRepairSubsystem* R) { return R ? R->RepairChunksSent : -1; }
	static const UWidget* RepBagIcon(const UAsdaRepairSubsystem* R, int32 V) { return (R && R->BagUi.IsValidIndex(V)) ? R->BagUi[V].Icon.Get() : nullptr; }
	// «Починить всё»: режим и отправка — боевой `SendRepair` (как после «ОК» окна подтверждения).
	static void RepSendAll(UAsdaRepairSubsystem* R) { if (R) { R->RebuildAll(); R->SelMode = UAsdaRepairSubsystem::kModeAll; R->SendRepair(); } }
	// Ответ 6572 боевым разбором окна (тот, что зовёт тик из очереди пакетов).
	static void RepResult(UAsdaRepairSubsystem* R, const TArray<uint8>& B) { if (R) { R->OnRepairResult(B); } }
	// Сброс хвоста (финал самотеста).
	static void RepReset(UAsdaRepairSubsystem* R) { if (R) { R->RepairTailCont.Reset(); R->RepairTailSlots.Reset(); R->bPending = false; R->SelMode = 0; } }
};

namespace AsdaInvX2Test
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.InvX2");   // имя команды

	// Перехваченная отправка.
	struct FSent { int32 Op = 0; TArray<uint8> Body; };

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		bool bQuit = false;                        // выйти после итога
		TArray<TArray<uint8>> SavedBag, SavedSpend, SavedEquip;   // контейнеры 1/2/3 до прогона
		int32 SavedWCur = -1, SavedWMax = -1;      // вес до прогона
		TArray<uint8> SavedBlock;                  // блок 1017 до прогона (68 б; пусто — не было)
		int32 SavedCharId = 0, SavedClass = -1, SavedSubJob = -1, SavedJob = -1, SavedSp = -1, SavedStoreMax = -1, SavedStoreUsed = -1;
		uint8 SavedLevel = 0, SavedGender = 0, SavedSelectMode = 0;
		uint32 CapGen = 0;                         // поколение перехвата
		TArray<FSent> Sent;                        // перехваченные отправки
		int32 Mark = 0;                            // число отправок на начало шага
		int32 Weapon = 0;                          // ремонтируемое оружие (item_list, тип 9, прочность > 0)
		int32 Func51 = 0;                          // спец-предмет подтипа 51 (расширение веса)

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaInvX2Test, Log,   TEXT("ИНВ-X2 ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaInvX2Test, Error, TEXT("ИНВ-X2 FAIL %s"), *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд от старта.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Живой прямоугольник узла (false — не отрисован / скрыт).
	static bool Rect(const UWidget* Wd, FVector2D& Mn, FVector2D& Mx)
	{
		return Wd && AsdaUi::IsShown(Wd) && AsdaUi::WidgetAbsRect(Wd, Mn, Mx) && (Mx - Mn).X >= 2.0 && (Mx - Mn).Y >= 2.0;
	}
	// Центр живого прямоугольника.
	static bool Center(const UWidget* Wd, FVector2D& Out)
	{
		FVector2D Mn = FVector2D::ZeroVector, Mx = FVector2D::ZeroVector;
		if (!Rect(Wd, Mn, Mx)) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}
	// Иконка нарисована (кисть с ресурсом).
	static bool IconDrawn(const UWidget* Wd)
	{
		const UImage* Img = Cast<UImage>(Wd);
		if (!Img || !Img->IsVisible()) { return false; }
		const FSlateBrush& B = Img->GetBrush();
		return B.GetResourceObject() != nullptr && B.DrawAs != ESlateBrushDrawType::NoDrawType;
	}
	// Курсор в точку + событие движения (наведение без щелчка).
	static void Hover(const FVector2D& P)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		FSlateApplication& App = FSlateApplication::Get();
		App.SetCursorPos(P);
		const FPointerEvent Mv(0, P, P, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState());
		App.ProcessMouseMoveEvent(Mv);
	}
	// Код в ячейке (0 — пусто).
	static int32 CodeAt(const FCtxRef& C, int32 Cont, int32 Cell)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		const FAsdaItemInstance* I = Inv ? Inv->GetCell(Cont, Cell) : nullptr;
		return (I && !I->IsEmpty()) ? I->ItemCode : 0;
	}
	// Положить запись боевой точкой (`ApplyExternalRecord`, как приёмники пакетов).
	static void Put(const FCtxRef& C, int32 Cont, int32 Cell, int32 Code, int32 Qty)
	{
		if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>())
		{
			Inv->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(Code, Cont, Cell, Qty), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
		}
	}
	// Отправки опкода с шага (последняя — в OutLast).
	static int32 SentSince(const FCtxRef& C, int32 Op, TArray<uint8>* OutLast = nullptr)
	{
		int32 N = 0;
		for (int32 i = C->Mark; i < C->Sent.Num(); ++i) { if (C->Sent[i].Op == Op) { ++N; if (OutLast) { *OutLast = C->Sent[i].Body; } } }
		return N;
	}
	// Тело 6571 (возможно с конвертом — тело 123 б в хвосте): число записей со слотом ≠ −1.
	static int32 Count6571(const TArray<uint8>& B)
	{
		if (B.Num() < 123) { return -1; }
		const uint8* P = B.GetData() + (B.Num() - 123);
		int32 N = 0;
		for (int32 i = 0; i < 41; ++i) { if (AsdaBytes::RdI16(P + 41 + 2 * i) != -1) { ++N; } }
		return N;
	}
	// Цифры процента веса мини-статуса (строка дампа `DumpControls`: «видимы [Е6 Д6 ]»).
	static FString WeightDigits(const FCtxRef& C)
	{
		UAsdaMiniStateSubsystem* M = C->Sub<UAsdaMiniStateSubsystem>();
		if (!M) { return TEXT("нет подсистемы"); }
		const FString D = M->DumpControls();
		const int32 P = D.Find(TEXT("цифры веса"));
		if (P == INDEX_NONE) { return TEXT("нет строки"); }
		const int32 L = D.Find(TEXT("["), ESearchCase::CaseSensitive, ESearchDir::FromStart, P);
		const int32 R = D.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, L);
		return (L != INDEX_NONE && R != INDEX_NONE) ? D.Mid(L + 1, R - L - 1).TrimStartAndEnd() : TEXT("?");
	}
	// Блок 1017 (68 б) с подменённым весом; источник — сохранённый блок либо поля сети.
	static TArray<uint8> Build1017(const FCtxRef& C, int32 MaxW, int32 CurW)
	{
		TArray<uint8> Blk;
		if (C->SavedBlock.Num() == 68) { Blk = C->SavedBlock; }
		else
		{
			Blk.SetNumZeroed(68);
			AsdaBytes::WrI32(Blk.GetData(), C->SavedCharId);
			Blk[27] = C->SavedGender; Blk[28] = (uint8)FMath::Max(0, C->SavedClass); Blk[29] = (uint8)FMath::Max(0, C->SavedSubJob);
			Blk[30] = C->SavedLevel; AsdaBytes::WrI16(Blk.GetData() + 39, FMath::Max(0, C->SavedSp)); Blk[41] = (uint8)FMath::Max(0, C->SavedJob);
		}
		AsdaBytes::WrI16(Blk.GetData() + 44, MaxW);   // u16 битовым образом в слове (так шлёт сервер P96)
		AsdaBytes::WrI16(Blk.GetData() + 46, CurW);
		TArray<uint8> B; B.SetNumZeroed(72);
		FMemory::Memcpy(B.GetData() + 4, Blk.GetData(), 68);
		return B;
	}

	// Данные из таблиц (оракул — сами записи таблиц).
	static bool PickData(const FCtxRef& C)
	{
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		if (!IT) { return false; }
		for (const FAsdaItemRecord& R : IT->GetAllItems())
		{
			if (R.ItemType == 9 && R.DurabilityMax > 0 && R.Price > 0 && !(R.ItemSubType == 10 && (R.ItemVariant == 1 || R.ItemVariant == 2)))
			{
				C->Weapon = R.ItemCode; break;
			}
		}
		for (int32 Code = 1; Code < 200000 && !C->Func51; ++Code)
		{
			FAsdaSpecialItemlistRow Sp;
			if (IT->GetSpecialItem(Code, Sp) && Sp.SubType == 51) { C->Func51 = Code; }
		}
		return C->Weapon > 0;
	}

	static void Finish(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaRepairSubsystem* Rep = C->Sub<UAsdaRepairSubsystem>();
		AsdaCVarSave::SetForTest(TEXT("asda.Inven.ShellX2"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Weight.Unsigned"), 1);
		if (Rep) { FAsdaInvX2SelfTest::RepReset(Rep); if (Rep->IsOpen()) { Rep->CloseWindow(); } }
		if (Inv)
		{
			auto Restore = [Inv](const TArray<TArray<uint8>>& Saved, int32 Cont)
			{
				for (int32 I = 0; I < Saved.Num(); ++I)
				{
					if (Saved[I].Num() >= 57) { Inv->ApplyExternalRecord(Saved[I], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
					else { Inv->ClearCell(Cont, I); }
				}
			};
			Restore(C->SavedBag, 1);
			Restore(C->SavedSpend, 2);
			Restore(C->SavedEquip, 3);
			if (Inv->IsWindowOpen()) { Inv->CloseWindow(); }
		}
		if (Net)
		{
			Net->CharSelectData = C->SavedBlock;
			Net->SelectedCharId = C->SavedCharId; Net->SelfClassId = C->SavedClass; Net->SelfSubJob = C->SavedSubJob; Net->SelfJob = C->SavedJob;
			Net->SelfSkillPoints = C->SavedSp; Net->SelfStoreSlotsMax = C->SavedStoreMax; Net->SelfStoreSlotsUsed = C->SavedStoreUsed;
			Net->SelfLevel = C->SavedLevel; Net->SelfGender = C->SavedGender; Net->LastSelectMode = C->SavedSelectMode;
			Net->SetSelfWeight(C->SavedWCur, C->SavedWMax, TEXT("самотест ИНВ-X2: возврат"));
			Net->SelfWeightCur = C->SavedWCur; Net->SelfWeightMax = C->SavedWMax;
			if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); C->CapGen = 0; }
		}
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaInvX2Test, TEXT("ИНВ-X2"), C->Ok, C->Fail);
	}

	// A. Геометрия сетки текущей вкладки путём игрока.
	static void CheckGrid(const FCtxRef& C, const TCHAR* Tag)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		if (!Inv) { C->Expect(false, TEXT("A подсистема инвентаря есть")); return; }
		FVector2D A0 = FVector2D::ZeroVector, B0 = FVector2D::ZeroVector;
		const bool b0 = Rect(Inv->DebugGetGridIcon(0), A0, B0);
		C->Expect(b0, FString::Printf(TEXT("%s ячейка 0 отрисована"), Tag));
		if (!b0) { return; }
		const double S = (B0 - A0).X / 32.0;   // масштаб: ячейка 32 px базы
		// ★ Ревизия 30.10 (симметрия полей подложки 8/8): ячейка 0 в базе 1024×768 — (132, 320) при окне в (116, 56),
		//   т.е. от левого-верха рамки окна (планы `INVEN_WIN_0_*`) — (16, 264). Значит INVEN_SET{r·12+c} в (132+33c, 320+33r) (сдвиг +6: поля 10/10 до белой рамки подложки).
		{
			FVector2D FMn(1e9, 1e9);
			UUserWidget* Wd = Inv->GetWindowWidget();
			TArray<UWidget*> All;
			if (Wd && Wd->WidgetTree) { Wd->WidgetTree->GetAllWidgets(All); }
			for (UWidget* X : All)
			{
				FVector2D A = FVector2D::ZeroVector, B = FVector2D::ZeroVector;
				if (X && X->GetName().StartsWith(TEXT("INVEN_WIN_0_")) && Rect(X, A, B)) { FMn = FVector2D(FMath::Min(FMn.X, A.X), FMath::Min(FMn.Y, A.Y)); }
			}
			const double Ox = (A0.X - FMn.X) / S, Oy = (A0.Y - FMn.Y) / S;
			C->Expect(FMath::Abs(Ox - 16.0) <= 1.0 && FMath::Abs(Oy - 264.0) <= 1.0,
				FString::Printf(TEXT("%s ячейка 0 от рамки окна (%.1f, %.1f) — ждали (16, 264): абсолют (132, 320) при окне (116, 56)"), Tag, Ox, Oy));
		}
		int32 Shown = 0, BadPos = 0; FString Bad;
		for (int32 N = 0; N < UAsdaInventorySubsystem::kGridCells; ++N)
		{
			FVector2D A = FVector2D::ZeroVector, B = FVector2D::ZeroVector;
			if (!Rect(Inv->DebugGetGridIcon(N), A, B)) { Bad += FString::Printf(TEXT(" %d:нет"), N); ++BadPos; continue; }
			++Shown;
			const int32 Col = N % 12, Row = N / 12;
			const double Dx = (A.X - A0.X) / S, Dy = (A.Y - A0.Y) / S;
			if (FMath::Abs(Dx - 33.0 * Col) > 1.5 || FMath::Abs(Dy - 33.0 * Row) > 1.5)
			{
				++BadPos; if (Bad.Len() < 400) { Bad += FString::Printf(TEXT(" %d:(%.1f,%.1f)≠(%d,%d)"), N, Dx, Dy, 33 * Col, 33 * Row); }
			}
		}
		C->Expect(Shown == 60 && BadPos == 0, FString::Printf(TEXT("%s 60 планов INVEN_SET* шаг 33, INVEN_SET{r·12+c} в (c,r): отрисовано %d, мимо %d%s"),
			Tag, Shown, BadPos, *Bad));
		C->Expect(!FAsdaInvX2SelfTest::ScrollShown(Inv) && FAsdaInvX2SelfTest::MaxRow(Inv) == 0,
			FString::Printf(TEXT("%s полоса прокрутки скрыта, рядов прокрутки 0 (макс. ряд %d)"), Tag, FAsdaInvX2SelfTest::MaxRow(Inv)));
	}

	// A. Рамка окна (планы `INVEN_WIN_0_*`) внутри вьюпорта.
	static void CheckWindowInScreen(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UUserWidget* Wd = Inv ? Inv->GetWindowWidget() : nullptr;
		if (!Wd || !Wd->WidgetTree || !GEngine || !GEngine->GameViewport) { C->Expect(false, TEXT("A окно и вьюпорт есть")); return; }
		const TSharedPtr<SViewport> Vp = GEngine->GameViewport->GetGameViewportWidget();
		if (!Vp.IsValid()) { C->Expect(false, TEXT("A вьюпорт есть")); return; }
		const FGeometry& G = Vp->GetCachedGeometry();
		const FVector2D VMn = G.GetAbsolutePosition(), VMx = VMn + G.GetAbsoluteSize();
		FVector2D Mn(1e9, 1e9), Mx(-1e9, -1e9); int32 N = 0;
		TArray<UWidget*> All; Wd->WidgetTree->GetAllWidgets(All);
		for (UWidget* X : All)
		{
			if (!X || !X->GetName().StartsWith(TEXT("INVEN_WIN_0_"))) { continue; }
			FVector2D A = FVector2D::ZeroVector, B = FVector2D::ZeroVector;
			if (!Rect(X, A, B)) { continue; }
			Mn = FVector2D(FMath::Min(Mn.X, A.X), FMath::Min(Mn.Y, A.Y)); Mx = FVector2D(FMath::Max(Mx.X, B.X), FMath::Max(Mx.Y, B.Y)); ++N;
		}
		const double S = G.Scale > 0.f ? G.Scale : 1.0;
		// ★ 30.10 (просьба владельца «кнопки красиво по новому месту»): нижний блок — клетка питомца слева, справа 2×2
		//   (449 452 / 453 10082). Кнопки отрисованы, внутри рамки, ниже подложки сетки, не перекрываются; зазоры между
		//   колонками и между рядами равны (±1 px базы).
		{
			const TCHAR* Names[] = { TEXT("Inven_PetCageBtn"), TEXT("Inven_ItemUpgrade_Btn"), TEXT("Inven_Smelting_Btn"),
				TEXT("Inven_Disassembly_Btn"), TEXT("Inven_primium_item_btn") };
			FVector2D BMn[5], BMx[5]; bool bAll = true; FString Miss;
			for (int32 i = 0; i < 5; ++i)
			{
				BMn[i] = BMx[i] = FVector2D::ZeroVector;
				if (!Rect(Wd->GetWidgetFromName(FName(Names[i])), BMn[i], BMx[i])) { bAll = false; Miss += FString(Names[i]) + TEXT(" "); }
			}
			FVector2D TMn = FVector2D::ZeroVector, TMx = FVector2D::ZeroVector;
			const bool bTray = Rect(Wd->GetWidgetFromName(FName(TEXT("INVEN_ITEMTRAY1_grp"))), TMn, TMx);
			bool bInside = bAll && bTray, bNoOverlap = bAll;
			for (int32 i = 0; bAll && i < 5; ++i)
			{
				bInside &= BMn[i].X >= Mn.X && BMx[i].X <= Mx.X && BMx[i].Y <= Mx.Y && BMn[i].Y >= TMx.Y - 0.5;
				for (int32 j = i + 1; j < 5; ++j)
				{
					const bool bX = BMn[i].X < BMx[j].X - 0.5 && BMn[j].X < BMx[i].X - 0.5;
					const bool bY = BMn[i].Y < BMx[j].Y - 0.5 && BMn[j].Y < BMx[i].Y - 0.5;
					if (bX && bY) { bNoOverlap = false; }
				}
			}
			// Зазоры: колонки 449→452 и 453→10082 по X, ряды 449→453 по Y.
			const double GapCol = bAll ? (BMn[2].X - BMx[1].X) / S : -1.0;
			const double GapCol2 = bAll ? (BMn[4].X - BMx[3].X) / S : -1.0;
			const double GapRow = bAll ? (BMn[3].Y - BMx[1].Y) / S : -1.0;
			C->Expect(bAll && bInside && bNoOverlap,
				FString::Printf(TEXT("A нижние кнопки (клетка, 449, 452, 453, 10082) отрисованы, внутри окна под сеткой, без перекрытий%s%s"),
					Miss.IsEmpty() ? TEXT("") : TEXT(" — нет: "), *Miss));
			C->Expect(bAll && GapCol > 0.0 && FMath::Abs(GapCol - GapCol2) <= 1.0 && FMath::Abs(GapCol - GapRow) <= 1.0,
				FString::Printf(TEXT("A нижние кнопки 2×2: зазоры колонки %.1f / %.1f, ряды %.1f px базы — равны"), GapCol, GapCol2, GapRow));
		}
		C->Expect(N >= 9 && Mn.X >= VMn.X - 0.5 && Mn.Y >= VMn.Y - 0.5 && Mx.X <= VMx.X + 0.5 && Mx.Y <= VMx.Y + 0.5,
			FString::Printf(TEXT("A рамка окна (%d планов) (%.0f,%.0f)-(%.0f,%.0f) внутри вьюпорта (%.0f,%.0f)-(%.0f,%.0f); ширина окна %.0f px базы"),
				N, Mn.X, Mn.Y, Mx.X, Mx.Y, VMn.X, VMn.Y, VMx.X, VMx.Y, (Mx.X - Mn.X) / S));
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!Inv || !WM || !Net) { C->Expect(false, TEXT("подсистемы инвентаря/окон/сети есть")); Finish(C); return; }
		if (!PickData(C)) { C->Expect(false, TEXT("данные: ремонтируемое оружие в item_list")); Finish(C); return; }
		C->Expect(true, FString::Printf(TEXT("данные: оружие %d, спец-предмет подтипа 51: %d"), C->Weapon, C->Func51));
		TWeakPtr<FCtx> WC(C);
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WC](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> X = WC.Pin()) { FSent S; S.Op = (int32)Op; if (P && Len > 0) { S.Body.Append(P, Len); } X->Sent.Add(MoveTemp(S)); }
		}, 0, kCmd);
		if (!C->CapGen) { C->Expect(false, TEXT("перехват отправок свободен")); Finish(C); return; }

		// Снимок состояния.
		AsdaCVarSave::SetForTest(TEXT("asda.Inven.ShellX2"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Weight.Unsigned"), 1);
		auto Snap = [Inv](int32 Cont, TArray<TArray<uint8>>& Out)
		{
			const int32 N = (Cont == 1) ? UAsdaInventorySubsystem::kShellSlots : FMath::Max(0, Inv->GetContainerSize(Cont));
			for (int32 I = 0; I < N; ++I) { const FAsdaItemInstance* X = Inv->GetCell(Cont, I); Out.Add(X && !X->IsEmpty() ? X->Raw : TArray<uint8>()); }
		};
		Snap(1, C->SavedBag); Snap(2, C->SavedSpend); Snap(3, C->SavedEquip);
		C->SavedWCur = Net->SelfWeightCur; C->SavedWMax = Net->SelfWeightMax;
		C->SavedBlock = Net->CharSelectData; C->SavedCharId = Net->SelectedCharId; C->SavedClass = Net->SelfClassId;
		C->SavedSubJob = Net->SelfSubJob; C->SavedJob = Net->SelfJob; C->SavedSp = Net->SelfSkillPoints;
		C->SavedStoreMax = Net->SelfStoreSlotsMax; C->SavedStoreUsed = Net->SelfStoreSlotsUsed;
		C->SavedLevel = Net->SelfLevel; C->SavedGender = Net->SelfGender; C->SavedSelectMode = Net->LastSelectMode;
		for (int32 I = 0; I < C->SavedBag.Num(); ++I)   { Inv->ClearCell(1, I); }
		for (int32 I = 0; I < C->SavedSpend.Num(); ++I) { Inv->ClearCell(2, I); }
		for (int32 I = 0; I < C->SavedEquip.Num(); ++I) { Inv->ClearCell(3, I); }

		C->Expect(UAsdaInventorySubsystem::GetShellCapacity() == 60 && Inv->GetContainerSize(1) == 60 && Inv->GetContainerSize(2) == 60,
			FString::Printf(TEXT("ёмкость: сумка %d (доступно %d), расходники %d"), UAsdaInventorySubsystem::GetShellCapacity(), Inv->GetContainerSize(1), Inv->GetContainerSize(2)));

		// ═══ B. Батч 4045 контейнера 1 со слотами 30..59 штатным приёмом ═══
		{
			TArray<uint8> Raw;
			for (int32 S = 30; S < 60; ++S) { Raw.Append(FAsdaItemRecordWriter::Cell(C->Weapon, 1, S, 1)); }
			Inv->SimulateBatch(1, Raw);
		}
		if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
		WM->BringWindowToFront(FName(TEXT("INVEN_WIN")));
		Inv->SetTab(0);

		// ═══ A. Геометрия: вкладка «Расходники» ═══
		At(C, 1.0f, [C]()
		{
			CheckGrid(C, TEXT("A вкладка 0:"));
			CheckWindowInScreen(C);
			if (UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>()) { I->SetTab(1); }
		});
		// ═══ A/B. Вкладка «Снаряжение»: геометрия, батч 30..59 в модели и на экране ═══
		At(C, 1.8f, [C]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			CheckGrid(C, TEXT("A вкладка 1:"));
			int32 InModel = 0, Drawn = 0;
			for (int32 S = 30; S < 60; ++S)
			{
				if (CodeAt(C, 1, S) == C->Weapon) { ++InModel; }
				if (I && IconDrawn(I->DebugGetGridIcon(S))) { ++Drawn; }
			}
			C->Expect(InModel == 30, FString::Printf(TEXT("B батч 4045: в модели слоты 30..59 заняты %d из 30"), InModel));
			C->Expect(Drawn == 30, FString::Printf(TEXT("B иконки ячеек 30..59 нарисованы: %d из 30"), Drawn));
			C->Expect(I && !IconDrawn(I->DebugGetGridIcon(0)), TEXT("B ячейка 0 пуста и не нарисована"));
		});
		// ═══ C. Ячейка 59: щелчок → выделение и курсор 409 ═══
		At(C, 2.2f, [C]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D P = FVector2D::ZeroVector;
			const bool bGeo = I && Center(I->DebugGetGridIcon(59), P);
			C->Expect(bGeo, TEXT("C ячейка 59 на экране"));
			if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, /*HoldFrames*/ 2); }
		});
		At(C, 2.8f, [C]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D PC = FVector2D::ZeroVector, PS = FVector2D::ZeroVector;
			const bool bCell = I && Center(I->DebugGetGridIcon(59), PC);
			const bool bSel = I && Center(FAsdaInvX2SelfTest::GridSel(I), PS);
			C->Expect(FAsdaInvX2SelfTest::SelCell(I) == 59, FString::Printf(TEXT("C щелчок по ячейке 59 выделил её (SelCell=%d)"), FAsdaInvX2SelfTest::SelCell(I)));
			C->Expect(bCell && bSel && FVector2D::Distance(PC, PS) < 6.0,
				FString::Printf(TEXT("C курсор выбора 409 над ячейкой 59 (центр ячейки (%.0f,%.0f), курсора (%.0f,%.0f))"), PC.X, PC.Y, PS.X, PS.Y));
			// Наведение — подсказка предмета. Курсор уже над 59 после щелчка (наведение «не сменилось») — сначала уводим
			//   его на пустую ячейку 0, затем возвращаем на 59: подсказка всплывает на СМЕНЕ ячейки под курсором.
			FVector2D P0 = FVector2D::ZeroVector;
			if (I && Center(I->DebugGetGridIcon(0), P0)) { Hover(P0); }
		});
		At(C, 3.1f, [C]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D PC = FVector2D::ZeroVector;
			if (I && Center(I->DebugGetGridIcon(59), PC)) { Hover(PC); }
		});
		At(C, 3.7f, [C]()
		{
			UAsdaItemTooltipSubsystem* Tip = C->Sub<UAsdaItemTooltipSubsystem>();
			C->Expect(Tip && Tip->IsTooltipShown() && Tip->GetTipOwner() == EAsdaTipOwner::Item,
				FString::Printf(TEXT("C наведение на ячейку 59 → подсказка предмета (показана=%d, владелец=%d)"),
					Tip && Tip->IsTooltipShown() ? 1 : 0, Tip ? (int32)Tip->GetTipOwner() : -1));
		});
		// ═══ C. Перетаскивание ячейки 59 в ячейку 0 → 5000 ═══
		At(C, 4.0f, [C]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D From = FVector2D::ZeroVector, To = FVector2D::ZeroVector;
			const bool bGeo = I && Center(I->DebugGetGridIcon(59), From) && Center(I->DebugGetGridIcon(0), To);
			C->Expect(bGeo, TEXT("C ячейки 59 и 0 на экране"));
			C->Mark = C->Sent.Num();
			if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateDrag(From, To, /*HoldFrames*/ 30); }
		});
		At(C, 5.6f, [C]()
		{
			TArray<uint8> B;
			const int32 N = SentSince(C, (int32)OP_CHAR_ITEM_MOVE, &B);
			bool bOk = false; int32 SSlot = -9, SCont = -9, DSlot = -9, DType = -9;
			if (N == 1 && B.Num() >= 22)
			{
				const uint8* P = B.GetData() + (B.Num() - 22);
				SSlot = AsdaBytes::RdI16(P + 0); SCont = P[4]; DSlot = AsdaBytes::RdI16(P + 11); DType = P[15];
				bOk = SSlot == 59 && SCont == 1 && DSlot == 0 && DType == 1;
			}
			C->Expect(bOk, FString::Printf(TEXT("C перенос 59 → 0: ушёл 5000 (%d шт.) источник конт %d слот %d → цель тип %d слот %d"), N, SCont, SSlot, DType, DSlot));
			if (UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>()) { I->DebugPumpPendingPackets(); }
		});
		// ═══ D. Износ 6570 и итог ремонта 6572 со слотом 45 ═══
		At(C, 6.0f, [C]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			if (!I) { return; }
			TArray<uint8> B4; B4.SetNumZeroed(4); B4[0] = 1; AsdaBytes::WrI16(B4.GetData() + 1, 45); B4[3] = 7;
			I->ApplySlotDurability6570(B4);
			const FAsdaItemInstance* It = I->GetCell(1, 45);
			C->Expect(It && It->Raw.Num() >= 57 && It->Raw[13] == 7, FString::Printf(TEXT("D 6570 сумка слот 45: прочность записи +13 = %d (ждали 7)"), It && It->Raw.Num() > 13 ? (int32)It->Raw[13] : -1));
			TArray<uint8> B; B.SetNumZeroed(170);
			AsdaBytes::WrI16(B.GetData(), 1);
			for (int32 i = 0; i < 41; ++i) { AsdaBytes::WrI16(B.GetData() + 43 + 2 * i, -1); }
			B[2] = 1; AsdaBytes::WrI16(B.GetData() + 43, 45); B[125] = 9;
			I->ApplyEquipRepairResult6572(B);
			It = I->GetCell(1, 45);
			C->Expect(It && It->Raw.Num() >= 57 && It->Raw[13] == 9, FString::Printf(TEXT("D 6572 сумка слот 45: прочность записи +13 = %d (ждали 9)"), It && It->Raw.Num() > 13 ? (int32)It->Raw[13] : -1));
		});
		// ═══ E. Ремонт «починить всё» с 50 вещами → два 6571 (41 + 9) ═══
		At(C, 6.4f, [C]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			UAsdaRepairSubsystem* R = C->Sub<UAsdaRepairSubsystem>();
			if (!I || !R) { C->Expect(false, TEXT("E подсистема ремонта есть")); return; }
			for (int32 S = 0; S < 60; ++S) { I->ClearCell(1, S); }
			for (int32 S = 0; S < 50; ++S) { Put(C, 1, S, C->Weapon, 1); }
			R->OpenWindow();
		});
		At(C, 7.2f, [C]()
		{
			UAsdaRepairSubsystem* R = C->Sub<UAsdaRepairSubsystem>();
			UAsdaWindowSubsystem* WMx = C->Sub<UAsdaWindowSubsystem>();
			C->Expect(FAsdaInvX2SelfTest::RepTotal(R) == 50, FString::Printf(TEXT("E окно ремонта: в списке сумки %d вещей (ждали 50)"), FAsdaInvX2SelfTest::RepTotal(R)));
			FVector2D P = FVector2D::ZeroVector;
			const bool bGeo = R && Center(FAsdaInvX2SelfTest::RepBagIcon(R, 0), P);
			C->Expect(bGeo, TEXT("E ячейка 0 сетки сумки окна ремонта на экране"));
			if (bGeo && WMx)
			{
				Hover(P);
				const bool bEaten = WMx->HandleMouseWheel(-1);
				C->Expect(bEaten && FAsdaInvX2SelfTest::RepRow(R) == 1,
					FString::Printf(TEXT("E колесо над сеткой ремонта: съедено=%d, ряд 0 → %d (ждали 1)"), bEaten ? 1 : 0, FAsdaInvX2SelfTest::RepRow(R)));
				WMx->HandleMouseWheel(+1);
			}
			C->Mark = C->Sent.Num();
			FAsdaInvX2SelfTest::RepSendAll(R);
			TArray<uint8> B;
			const int32 N = SentSince(C, (int32)OP_RECV_USER_DURA_REGEN, &B);
			C->Expect(N == 1 && Count6571(B) == 41 && FAsdaInvX2SelfTest::RepTail(R) == 9,
				FString::Printf(TEXT("E «починить всё»: первый 6571 (%d шт.) — записей %d (ждали 41), хвост %d (ждали 9)"), N, Count6571(B), FAsdaInvX2SelfTest::RepTail(R)));
			// Ответ 6572 «успех» на первый пакет → второй пакет.
			TArray<uint8> Ans; Ans.SetNumZeroed(170);
			AsdaBytes::WrI16(Ans.GetData(), 1);
			for (int32 i = 0; i < 41; ++i) { AsdaBytes::WrI16(Ans.GetData() + 43 + 2 * i, -1); }
			C->Mark = C->Sent.Num();
			FAsdaInvX2SelfTest::RepResult(R, Ans);
			B.Reset();
			const int32 N2 = SentSince(C, (int32)OP_RECV_USER_DURA_REGEN, &B);
			C->Expect(N2 == 1 && Count6571(B) == 9 && FAsdaInvX2SelfTest::RepTail(R) == 0 && FAsdaInvX2SelfTest::RepChunks(R) == 2,
				FString::Printf(TEXT("E ответ 6572 → второй 6571 (%d шт.) — записей %d (ждали 9), хвост %d, пакетов %d"), N2, Count6571(B), FAsdaInvX2SelfTest::RepTail(R), FAsdaInvX2SelfTest::RepChunks(R)));
			C->Mark = C->Sent.Num();
			FAsdaInvX2SelfTest::RepResult(R, Ans);
			C->Expect(SentSince(C, (int32)OP_RECV_USER_DURA_REGEN) == 0, TEXT("E ответ на второй — третьего 6571 нет"));
			if (UAsdaWindowSubsystem* W2 = C->Sub<UAsdaWindowSubsystem>()) { W2->CloseWindow(FName(TEXT("ONEBUT_MSGBOX"))); }
			if (R) { R->CloseWindow(); }
		});
		// ═══ F. Вес u16: 1017 (60000 / 40000) и 5451 подтип 51 (60000 / 60000) ═══
		At(C, 8.0f, [C]()
		{
			UAsdaNetSubsystem* N = C->Sub<UAsdaNetSubsystem>();
			if (!N) { return; }
			N->LastSelectMode = N->SelfSoulState;   // признак одержимости 1017 не меняет
			N->DebugFeedPacket(1017, Build1017(C, 60000, 40000));
			C->Expect(N->SelfWeightMax == 60000 && N->SelfWeightCur == 40000,
				FString::Printf(TEXT("F 1017 вес u16: %d / %d (ждали 40000 / 60000)"), N->SelfWeightCur, N->SelfWeightMax));
		});
		At(C, 8.6f, [C]()
		{
			const FString D = WeightDigits(C);
			C->Expect(D.Contains(TEXT("Е6")) && D.Contains(TEXT("Д6")) && !D.Contains(TEXT("С")),
				FString::Printf(TEXT("F полоса веса мини-статуса: 66 %% — цифры [%s]"), *D));
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			if (!I || C->Func51 <= 0) { C->Expect(C->Func51 > 0, TEXT("F спец-предмет подтипа 51 найден")); return; }
			TArray<uint8> B; B.SetNumZeroed(68);
			B[0] = 1;
			const TArray<uint8> Rec = FAsdaItemRecordWriter::Cell(C->Func51, 1, 45, 1);
			FMemory::Memcpy(B.GetData() + 3, Rec.GetData(), FMath::Min(57, Rec.Num()));
			AsdaBytes::WrI16(B.GetData() + 60, 60000);
			AsdaBytes::WrI16(B.GetData() + 66, 60000);
			I->SimulateFunctionResult(B);
			I->DebugPumpPendingPackets();
		});
		At(C, 9.2f, [C]()
		{
			UAsdaNetSubsystem* N = C->Sub<UAsdaNetSubsystem>();
			C->Expect(N && N->SelfWeightMax == 60000 && N->SelfWeightCur == 60000,
				FString::Printf(TEXT("F 5451 подтип 51 вес u16: %d / %d (ждали 60000 / 60000)"), N ? N->SelfWeightCur : -1, N ? N->SelfWeightMax : -1));
			const FString D = WeightDigits(C);
			C->Expect(D.Contains(TEXT("С1")) && D.Contains(TEXT("Д0")) && D.Contains(TEXT("Е0")),
				FString::Printf(TEXT("F полоса веса мини-статуса: 100 %% — цифры [%s]"), *D));
			// Ключ 0 — прежнее знаковое чтение (40000 по проводу = −25536).
			AsdaCVarSave::SetForTest(TEXT("asda.Weight.Unsigned"), 0);
			const int32 Old = AsdaWeightFromWire(AsdaBytes::RdI16(reinterpret_cast<const uint8*>("\x40\x9C")));
			AsdaCVarSave::SetForTest(TEXT("asda.Weight.Unsigned"), 1);
			const int32 New = AsdaWeightFromWire(AsdaBytes::RdI16(reinterpret_cast<const uint8*>("\x40\x9C")));
			const int32 FromI32 = AsdaWeightFromWire((int32)0xFFFF9C40);   // старый сервер: `movsx` в i32-поле
			C->Expect(Old == -25536 && New == 40000 && FromI32 == 40000,
				FString::Printf(TEXT("F ключ asda.Weight.Unsigned: 0 → %d (прежнее i16), 1 → %d, i32 со знаковым расширением → %d"), Old, New, FromI32));
		});
		// ═══ G. Ключ asda.Inven.ShellX2 0 — ёмкость 30 ═══
		At(C, 9.6f, [C]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			if (!I) { return; }
			for (int32 S = 0; S < 60; ++S) { I->ClearCell(1, S); }
			AsdaCVarSave::SetForTest(TEXT("asda.Inven.ShellX2"), 0);
			Put(C, 1, 0, C->Weapon, 1);
			Put(C, 1, 45, C->Weapon, 1);
			const bool bMove = I->MoveItem(1, 0, 1, 45);
			C->Expect(UAsdaInventorySubsystem::GetShellCapacity() == 30 && I->GetContainerSize(1) == 30 && CodeAt(C, 1, 0) == C->Weapon
				&& CodeAt(C, 1, 45) == 0 && !bMove && I->FindFreeSlot(1) == 1,
				FString::Printf(TEXT("G ключ ShellX2=0: ёмкость %d, слот 45 принят=%d, перенос 0→45=%d, свободная ячейка %d"),
					I->GetContainerSize(1), CodeAt(C, 1, 45) != 0 ? 1 : 0, bMove ? 1 : 0, I->FindFreeSlot(1)));
			AsdaCVarSave::SetForTest(TEXT("asda.Inven.ShellX2"), 1);
			Put(C, 1, 45, C->Weapon, 1);
			C->Expect(I->GetContainerSize(1) == 60 && CodeAt(C, 1, 45) == C->Weapon, TEXT("G ключ ShellX2=1: слот 45 принят, ёмкость 60"));
		});
		At(C, 10.0f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestInvX2(
	TEXT("asda.SelfTest.InvX2"),
	TEXT("Самотест «инвентарь x2»: сетка 12x5 путём игрока (60 ячеек, шаг 33, нумерация r*12+c, обе вкладки, полоса скрыта, окно в экране), "
	     "батч 4045 со слотами 30..59, ячейка 59 (выделение, подсказка, перенос 5000), 6570/6572 слот 45, ремонт >41 двумя 6571, "
	     "вес u16 1017/5451 = 60000, ключи ShellX2/Weight.Unsigned. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaInvX2Test;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Свой персонаж нужен гейтам мира — офлайн его нет: штатный фейковый спавн и повторный запуск через 2 с.
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
