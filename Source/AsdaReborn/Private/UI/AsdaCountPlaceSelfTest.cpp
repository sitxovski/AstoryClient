// Copyright (c) 2026 @sitxovski (Discord)
// ★★ САМОТЕСТ «МЕСТО ЧИСЛА КОЛИЧЕСТВА В ЯЧЕЙКЕ» ПУТЁМ ИГРОКА (08.11.2026, пачка 16, баг 8). [ОРИГИНАЛ]
//
// ЖАЛОБА. Окно «Report monster hunting results» (56450): число знаков стоит слева посередине большой ячейки 56451 и прилипло
//   к левому краю ячеек сетки 56461…56490. Оригинал при сборке окна ставит числу маску выравнивания КОДОМ
//   (`uiSowelWindow_InitControls@0xECF680`: `CControl_SetField924(…, 9)` = низ + центр), а в .JUI у иконок `TextAlign = 4`
//   (лево + середина). Правка — в ассетах, шаг `Tools/asda_ui_item_count_align.py` (там же список всех 7 окон с тем же дефектом).
//
// ЧТО ДЕЛАЕТ. Кладёт в сумку три знака (22647 ×5, 22648 ×99, 22649 ×999), открывает окно отчёта охоты тем же кодом, что кнопка
//   NPC 30068 (`asda.HuntReport.Open`), настоящим кликом Slate выбирает знак 22648, вводит 5 в нумпад и жмёт OK (число встаёт
//   в большую ячейку), снимает кадр. По ЖИВОЙ геометрии каждого видимого числа (большая ячейка и сетка) считает место заливки
//   цифры (бокс узла + желаемый размер текста по выравниванию) и сверяет с ячейкой: центр по горизонтали (±1,5 px базы),
//   низ цифры у низа ячейки (от −5 до +1 px базы). Затем окна ремонта (57300, `asda.Repair.Open`) и передачи опций (50300,
//   `asda.ItemOption.Open`) — кадр и та же проверка видимых чисел. Для всех 7 окон — проверка УЗЛОВ (и пустых) живого
//   экземпляра: выравнивание CENTER, верх слота = верх ячейки + (высота − 14) при маске 9 / + (высота − 14)/2 при маске 5.
//   Окна, которые путём игрока офлайн не открыть (снятие предела питомца, подтверждение задания, покорение, объединение),
//   проверяются экземпляром класса окна (CreateWidget без показа) — это проверка ассета, не пути игрока.
//
// Консоль: `asda.SelfTest.CountPlace [tag=after] [quit]`. Только офлайн (-AsdaNoNet). Кадры: `Reports/batch16/hunt_marks/
//   <tag>_<окно>.png`. Строки — «САМОТЕСТ-МЕСТО», итог — «САМОТЕСТ-МЕСТО ИТОГ». Сумка и выбор знака возвращаются в конце.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaHuntReportSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableText.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Regex.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCountPlaceTest, Log, All);

namespace AsdaCountPlaceTestLocal
{
	// Окно с узлами-числами: путь класса WBP, regex имени узла, ожидаемое число узлов, маска эталона, путь игрока есть.
	struct FWin { const TCHAR* Wbp; const TCHAR* Path; const TCHAR* Rx; int32 Want; int32 Mask; };
	static const FWin kWins[] = {
		{ TEXT("WBP_Hunt_report_Win"),     TEXT("/Game/AsdaAssets/UI/Player_UI/Character/WBP_Hunt_report_Win.WBP_Hunt_report_Win_C"),
		  TEXT("^Hunt_report_Mark_\\d+$"), 31, 9 },
		{ TEXT("WBP_ItemOptionValue_WIN"), TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_ItemOptionValue_WIN.WBP_ItemOptionValue_WIN_C"),
		  TEXT("^(ItemOptionValue_Mark_1|ItemOptionValue_1_Box_Mark_\\d\\d)$"), 31, 9 },
		{ TEXT("WBP_INVEN_Repair_Win"),    TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_INVEN_Repair_Win.WBP_INVEN_Repair_Win_C"),
		  TEXT("^INVEN_Repair_(Jmark|Imark)_\\d+$"), 41, 9 },
		{ TEXT("WBP_Pet_LimitBreak_Win"),  TEXT("/Game/AsdaAssets/UI/Player_UI/Pet/WBP_Pet_LimitBreak_Win.WBP_Pet_LimitBreak_Win_C"),
		  TEXT("^Pet_LimitBreak_SETMARK[23]?$"), 3, 9 },
		{ TEXT("WBP_Item_Union_Win"),      TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Item_Union_Win.WBP_Item_Union_Win_C"),
		  TEXT("^Item_Union_3_Mark_\\d\\d$"), 30, 9 },
		{ TEXT("WBP_QUEST_Confirm_Win"),   TEXT("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_QUEST_Confirm_Win.WBP_QUEST_Confirm_Win_C"),
		  TEXT("^QUEST_Confirm_Text_T[12]_MARK\\d$"), 8, 5 },
		{ TEXT("WBP_Subjugate_WIN"),       TEXT("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_Subjugate_WIN.WBP_Subjugate_WIN_C"),
		  TEXT("^Subjugate_R_M[1-6]$"), 6, 5 },
	};
	static constexpr float kLinePx = 14.f;   // строка импортёра (`asda_jui_parse.LABEL_LINE_PX`)

	// Знаки охоты (LinkCode 208) и их количество в сумке; выбирается второй (22648) с количеством kPick.
	static const int32 kMarks[][2] = { { 22647, 5 }, { 22648, 99 }, { 22649, 999 } };
	static constexpr int32 kPick = 5;

	// План иконки контрола: узел импортёра `{Idc}_{N}_{M}` того же родителя, что узел-число.
	static UWidget* IconPlane(UUserWidget* Root, const UTextBlock* Tb)
	{
		UWidget* Found = nullptr;
		if (!Root || !Root->WidgetTree || !Tb) { return nullptr; }
		const FString Idc = Tb->GetName();
		Root->WidgetTree->ForEachWidget([&](UWidget* W)
		{
			if (Found || !W || W == Tb || W->GetParent() != Tb->GetParent()) { return; }
			FString I; int32 J = 0, P = 0;
			if (AsdaUi::ParsePlaneName(W->GetName(), I, J, P) && I == Idc) { Found = W; }
		});
		return Found;
	}

	// Состояние прогона.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false;
		FString Tag = TEXT("after");
		FString Dir;
		int32 Ok = 0, Fail = 0;
		TArray<TFunction<float()>> Steps;   // шаг возвращает паузу до следующего, с; < 0 — снимок (ждать кадра)
		int32 Index = 0;
		double NextTime = 0.0;
		bool bAwaitShot = false;
		int32 FramesAfterShot = 0;
		TArray<TArray<uint8>> Saved2;       // сумка до теста (сырые записи; пусто — ячейка была пуста)
		TUniquePtr<FAsdaNetSelfScope> Self;
		TArray<TStrongObjectPtr<UUserWidget>> Probes;   // экземпляры окон без показа (проверка ассета)

		void Expect(bool b, const FString& What, const FString& Det = FString())
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaCountPlaceTest, Log, TEXT("САМОТЕСТ-МЕСТО %s: %s — %s"), b ? TEXT("OK  ") : TEXT("FAIL"), *What, *Det);
		}
		template<class T> T* Sub() const
		{
			UWorld* Wd = W.Get();
			UGameInstance* GI = Wd ? Wd->GetGameInstance() : nullptr;
			return GI ? GI->GetSubsystem<T>() : nullptr;
		}
		void Exec(const FString& Cmd) const
		{
			UE_LOG(LogAsdaCountPlaceTest, Log, TEXT("САМОТЕСТ-МЕСТО exec: %s"), *Cmd);
			if (GEngine && W.IsValid()) { GEngine->Exec(W.Get(), *Cmd); }
		}
		// Живой показанный экземпляр окна класса `Wbp`.
		UUserWidget* LiveWindow(const TCHAR* Wbp) const
		{
			const FString Cls = FString(Wbp) + TEXT("_C");
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* UW = *It;
				if (UW && UW->GetWorld() == W.Get() && UW->WidgetTree && UW->IsInViewport() && UW->IsVisible() && UW->GetClass()->GetName() == Cls)
				{
					return UW;
				}
			}
			return nullptr;
		}
		// Клик настоящими событиями Slate в центр узла `Name` окна `Root`.
		bool ClickNode(UUserWidget* Root, const FString& Name)
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>();
			UWidget* Wg = Root ? Root->GetWidgetFromName(FName(*Name)) : nullptr;
			FVector2D Mn, Mx;
			if (!WM || !Wg || !AsdaUi::WidgetAbsRect(Wg, Mn, Mx)) { Expect(false, FString::Printf(TEXT("клик по %s"), *Name), TEXT("нет живой геометрии")); return false; }
			WM->SimulateClickAt((Mn + Mx) * 0.5, /*HoldFrames*/ 3);
			return true;
		}
		// Проверка УЗЛОВ окна (и пустых): выравнивание и слот относительно плана иконки — по маске эталона.
		void CheckNodes(const TCHAR* Stage, UUserWidget* Root, const FWin& Win)
		{
			int32 Found = 0, Good = 0;
			FString Bad;
			const FRegexPattern Pat(Win.Rx);
			if (Root && Root->WidgetTree)
			{
				Root->WidgetTree->ForEachWidget([&](UWidget* Wg)
				{
					UTextBlock* Tb = Cast<UTextBlock>(Wg);
					if (!Tb) { return; }
					FRegexMatcher M(Pat, Tb->GetName());
					if (!M.FindNext()) { return; }
					++Found;
					const UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Tb->Slot);
					const UWidget* Icon = IconPlane(Root, Tb);
					const UCanvasPanelSlot* IS = Icon ? Cast<UCanvasPanelSlot>(Icon->Slot) : nullptr;
					if (!S || !IS) { Bad += Tb->GetName() + TEXT("(нет слота/иконки) "); return; }
					const FMargin O = S->GetOffsets(), IO = IS->GetOffsets();
					const float Vy = (Win.Mask & 0x8) ? (IO.Bottom - kLinePx) : (Win.Mask & 0x4) ? (IO.Bottom - kLinePx) * 0.5f : 0.f;
					const float WantTop = IO.Top + FMath::Max(Vy, 0.f);
					const bool bJ = AsdaUi::ReadJustification(Tb) == ETextJustify::Center;
					const bool bOk = bJ && FMath::IsNearlyEqual(O.Top, WantTop, 0.01f) && FMath::IsNearlyEqual(O.Left, IO.Left, 0.01f)
						&& FMath::IsNearlyEqual(O.Right, IO.Right, 0.01f)
						&& (!(Win.Mask & 0x8) || O.Top + O.Bottom <= IO.Top + IO.Bottom + 0.01f);   // маска 9: поле числа не ниже ячейки
					if (bOk) { ++Good; return; }
					Bad += FString::Printf(TEXT("%s(%s верх %.0f≠%.0f) "), *Tb->GetName(), bJ ? TEXT("центр") : TEXT("НЕ центр"), O.Top, WantTop);
				});
			}
			Expect(Found == Win.Want && Good == Found,
				FString::Printf(TEXT("[%s] %s: все узлы-числа — по маске эталона %d (%s)"), Stage, Win.Wbp, Win.Mask,
					Win.Mask == 9 ? TEXT("низ + центр") : TEXT("середина + центр")),
				FString::Printf(TEXT("узлов %d (ждём %d), верных %d; не так: %s"), Found, Win.Want, Good, Bad.IsEmpty() ? TEXT("—") : *Bad));
		}
		// Место ЗАЛИВКИ каждой видимой цифры по живой геометрии против её ячейки. Возврат — число проверенных.
		int32 CheckShown(const TCHAR* Stage, UUserWidget* Root, const FWin& Win, bool bRequire = true)
		{
			int32 Shown = 0, Good = 0;
			FString Bad;
			const FRegexPattern Pat(Win.Rx);
			if (!Root || !Root->WidgetTree) { return 0; }
			Root->WidgetTree->ForEachWidget([&](UWidget* Wg)
			{
				UTextBlock* Tb = Cast<UTextBlock>(Wg);
				if (!Tb || Tb->GetText().IsEmpty()) { return; }
				FRegexMatcher M(Pat, Tb->GetName());
				if (!M.FindNext()) { return; }
				for (const UWidget* P = Tb; P; P = P->GetParent()) { if (!P->IsVisible()) { return; } }
				UWidget* Icon = IconPlane(Root, Tb);
				FVector2D BMn, BMx, CMn, CMx;
				if (!Icon || !AsdaUi::WidgetAbsRect(Tb, BMn, BMx) || !AsdaUi::WidgetAbsRect(Icon, CMn, CMx)) { return; }
				const UCanvasPanelSlot* IS = Cast<UCanvasPanelSlot>(Icon->Slot);
				const float Scale = (IS && IS->GetOffsets().Right > 0.f) ? (CMx.X - CMn.X) / IS->GetOffsets().Right : 1.f;
				const FVector2D D = Tb->GetDesiredSize() * Scale;   // заливка + обводка, абсолютные px
				const ETextJustify::Type J = AsdaUi::ReadJustification(Tb);
				const double Gx0 = (J == ETextJustify::Center) ? BMn.X + ((BMx.X - BMn.X) - D.X) * 0.5 : (J == ETextJustify::Right) ? BMx.X - D.X : BMn.X;
				const double Gy0 = BMn.Y;
				const double DxC = (Gx0 + D.X * 0.5) - (CMn.X + CMx.X) * 0.5;   // + вправо от центра ячейки
				const double DyB = (Gy0 + D.Y) - CMx.Y;                          // + ниже низа ячейки
				const double DyM = (Gy0 + D.Y * 0.5) - (CMn.Y + CMx.Y) * 0.5;   // + ниже середины ячейки
				const bool bBottom = (Win.Mask & 0x8) != 0;
				const bool bOk = FMath::Abs(DxC) <= 1.5 * Scale
					&& (bBottom ? (DyB >= -5.0 * Scale && DyB <= 1.0 * Scale) : FMath::Abs(DyM) <= 3.0 * Scale);
				++Shown;
				if (bOk) { ++Good; }
				else { Bad += FString::Printf(TEXT("%s «%s» (dx=%.1f, низ %.1f) "), *Tb->GetName(), *Tb->GetText().ToString(), DxC / Scale, DyB / Scale); }
				UE_LOG(LogAsdaCountPlaceTest, Log,
					TEXT("САМОТЕСТ-МЕСТО узел [%s] %s/%s «%s» ячейка=(%.1f,%.1f)-(%.1f,%.1f) цифра=(%.1f,%.1f)-(%.1f,%.1f) от центра %.1f px, от низа %.1f px (база)"),
					Stage, Win.Wbp, *Tb->GetName(), *Tb->GetText().ToString(), CMn.X, CMn.Y, CMx.X, CMx.Y, Gx0, Gy0, Gx0 + D.X, Gy0 + D.Y,
					DxC / Scale, DyB / Scale);
			});
			if (!bRequire && Shown == 0)   // окно без предметов: чисел нет — проверяются только узлы (`CheckNodes`)
			{
				UE_LOG(LogAsdaCountPlaceTest, Log, TEXT("САМОТЕСТ-МЕСТО [%s] %s: видимых чисел нет (окно без предметов)"), Stage, Win.Wbp);
				return 0;
			}
			Expect(Shown > 0 && Good == Shown,
				FString::Printf(TEXT("[%s] %s: каждая видимая цифра — %s ячейки"), Stage, Win.Wbp,
					(Win.Mask & 0x8) ? TEXT("по центру у низа") : TEXT("по центру")),
				FString::Printf(TEXT("видимых %d, верно %d; не так: %s"), Shown, Good, Bad.IsEmpty() ? TEXT("—") : *Bad));
			return Shown;
		}
		void AddShot(const TCHAR* Name)
		{
			const FString N = Name;
			Steps.Add([this, N]() -> float
			{
				const FString File = Dir / FString::Printf(TEXT("%s_%s.png"), *Tag, *N);
				FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
				UE_LOG(LogAsdaCountPlaceTest, Log, TEXT("САМОТЕСТ-МЕСТО кадр → %s"), *File);
				return -1.f;
			});
		}
	};

	static void Finish(TSharedRef<FCtx> C)
	{
		if (UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>()) { WM->DismissNumPad(TEXT("самотест места числа")); WM->CloseAll(); }
		if (UAsdaHuntReportSubsystem* H = C->Sub<UAsdaHuntReportSubsystem>()) { H->CloseWindow(); H->ResetSessionState(EAsdaSessionResetReason::EnteringWorld); }
		if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>())
		{
			for (int32 i = 0; i < C->Saved2.Num(); ++i)
			{
				if (C->Saved2[i].Num() >= 57) { Inv->ApplyExternalRecord(C->Saved2[i], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
				else { Inv->ClearCell(2, i); }
			}
		}
		C->Probes.Reset();
		C->Self.Reset();
		AsdaSelfTestKit::Finish(C->W.Get(), TEXT("asda.SelfTest.CountPlace"), C->bQuit, LogAsdaCountPlaceTest, TEXT("САМОТЕСТ-МЕСТО"),
			C->Ok, C->Fail, *FString::Printf(TEXT(" (%s)"), *C->Tag));
	}

	static void Build(TSharedRef<FCtx> C)
	{
		FCtx* P = &C.Get();
		// ── 0. свой персонаж в мире ──
		P->Steps.Add([P]() -> float
		{
			if (AsdaSelfTestKit::NeedSelfSpawn(P->W.Get()))
			{
				P->Exec(TEXT("Asda.Load.CurrentMap 3"));
				P->Exec(TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
			return 3.0f;
		});
		// ── 1. знаки в сумку, окно отчёта охоты тем же кодом, что кнопка NPC 30068 ──
		P->Steps.Add([P]() -> float
		{
			UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
			UAsdaNetSubsystem* Net = P->Sub<UAsdaNetSubsystem>();
			UAsdaHuntReportSubsystem* H = P->Sub<UAsdaHuntReportSubsystem>();
			UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>();
			if (!Inv || !Net || !H || !WM) { P->Expect(false, TEXT("подсистемы сумки/сети/окна охоты/окон"), TEXT("нет")); return 0.f; }
			P->Self = MakeUnique<FAsdaNetSelfScope>(Net, TEXT("asda.SelfTest.CountPlace"));
			Net->SelectedCharId = 424242;
			Net->SetSelfUserNumForDebug(500);
			P->Saved2.Reset();
			for (int32 i = 0; i < Inv->GetContainerSize(2); ++i)
			{
				const FAsdaItemInstance* I = Inv->GetCell(2, i);
				P->Saved2.Add((I && !I->IsEmpty()) ? I->Raw : TArray<uint8>());
				Inv->DebugPutItem(2, i, 0, 0);
			}
			for (int32 k = 0; k < (int32)UE_ARRAY_COUNT(kMarks); ++k) { Inv->DebugPutItem(2, k, kMarks[k][0], kMarks[k][1]); }
			WM->CloseAll();
			H->ResetSessionState(EAsdaSessionResetReason::EnteringWorld);
			P->Exec(TEXT("asda.HuntReport.Open"));
			return 1.5f;
		});
		// ── 2. клик по ячейке знака 22648 (вторая ячейка сетки) → нумпад ──
		P->Steps.Add([P]() -> float
		{
			UAsdaHuntReportSubsystem* H = P->Sub<UAsdaHuntReportSubsystem>();
			P->Expect(H && H->IsOpen() && H->DebugCellCode(1) == kMarks[1][0], TEXT("окно 56450 открыто, знак 22648 во второй ячейке"),
				H ? FString::Printf(TEXT("[1]=%d «%s»"), H->DebugCellCode(1), *H->DebugCellText(1)) : FString(TEXT("нет")));
			UUserWidget* Root = H ? H->DebugWidget() : nullptr;
			UTextBlock* Tb = Root ? Cast<UTextBlock>(Root->GetWidgetFromName(TEXT("Hunt_report_Mark_2"))) : nullptr;
			UWidget* Icon = IconPlane(Root, Tb);
			if (Icon) { P->ClickNode(Root, Icon->GetName()); }
			else { P->Expect(false, TEXT("план иконки ячейки 56462"), TEXT("нет")); }
			return 0.8f;
		});
		// ── 3. ввод количества и OK нумпада ──
		P->Steps.Add([P]() -> float
		{
			UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>();
			UUserWidget* NW = WM ? WM->FindWindow(FName(TEXT("NUMPAD_WIN"))) : nullptr;
			UEditableText* E = NW ? Cast<UEditableText>(NW->GetWidgetFromName(TEXT("NUMPAD_NUMBOX"))) : nullptr;
			P->Expect(WM && WM->IsNumPadOpen() && E, TEXT("ЛКМ по ячейке знака → нумпад открыт"));
			if (E) { E->SetText(FText::FromString(FString::FromInt(kPick))); }
			if (NW) { P->ClickNode(NW, TEXT("NUMPAD_OK")); }
			return 1.0f;
		});
		P->AddShot(TEXT("huntreport"));
		// ── 4. отчёт охоты: число в большой ячейке и в сетке ──
		P->Steps.Add([P]() -> float
		{
			UAsdaHuntReportSubsystem* H = P->Sub<UAsdaHuntReportSubsystem>();
			P->Expect(H && H->DebugResultQtyText() == FString::FromInt(kPick), TEXT("OK нумпада → число в большой ячейке 56451"),
				H ? H->DebugResultQtyText() : FString());
			UUserWidget* Root = H ? H->DebugWidget() : nullptr;
			UE_LOG(LogAsdaCountPlaceTest, Log, TEXT("САМОТЕСТ-МЕСТО структура окна:\n%s"), H ? *H->DumpControls() : TEXT("—"));
			const int32 N = P->CheckShown(TEXT("huntreport"), Root, kWins[0]);
			P->Expect(N == 4, TEXT("[huntreport] видимых чисел 4 (большая ячейка + 3 знака)"), FString::FromInt(N));
			P->CheckNodes(TEXT("huntreport"), Root, kWins[0]);
			if (H) { H->CloseWindow(); }
			if (UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>()) { WM->CloseAll(); }
			P->Exec(TEXT("asda.Repair.Open"));
			return 1.5f;
		});
		// ── 5. ремонт 57300 ──
		P->AddShot(TEXT("repair"));
		P->Steps.Add([P]() -> float
		{
			UUserWidget* Root = P->LiveWindow(kWins[2].Wbp);
			P->Expect(Root != nullptr, TEXT("[repair] окно 57300 открыто"));
			P->CheckShown(TEXT("repair"), Root, kWins[2], false);
			P->CheckNodes(TEXT("repair"), Root, kWins[2]);
			P->Exec(TEXT("asda.Repair.Close"));
			if (UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>()) { WM->CloseAll(); }
			P->Exec(TEXT("asda.ItemOption.Open"));
			return 1.5f;
		});
		// ── 6. передача опций 50300 ──
		P->AddShot(TEXT("itemoption"));
		P->Steps.Add([P]() -> float
		{
			UUserWidget* Root = P->LiveWindow(kWins[1].Wbp);
			P->Expect(Root != nullptr, TEXT("[itemoption] окно 50300 открыто"));
			P->CheckShown(TEXT("itemoption"), Root, kWins[1], false);
			P->CheckNodes(TEXT("itemoption"), Root, kWins[1]);
			P->Exec(TEXT("asda.ItemOption.Close"));
			if (UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>()) { WM->CloseAll(); }
			return 0.5f;
		});
		// ── 7. окна без офлайн-пути игрока: экземпляр класса (проверка ассета) ──
		P->Steps.Add([P]() -> float
		{
			for (int32 i = 3; i < (int32)UE_ARRAY_COUNT(kWins); ++i)
			{
				UClass* Cls = LoadClass<UUserWidget>(nullptr, kWins[i].Path);
				UUserWidget* UW = (Cls && P->W.IsValid()) ? CreateWidget<UUserWidget>(P->W.Get(), Cls) : nullptr;
				if (!UW) { P->Expect(false, FString::Printf(TEXT("[ассет] класс %s"), kWins[i].Wbp), TEXT("не загружен")); continue; }
				P->Probes.Emplace(UW);
				P->CheckNodes(TEXT("ассет"), UW, kWins[i]);
			}
			return 0.2f;
		});
	}

	static FAutoConsoleCommandWithWorldAndArgs GCmdCountPlace(
		TEXT("asda.SelfTest.CountPlace"),
		TEXT("Место числа количества в ячейке путём игрока: отчёт охоты 56450 (большая ячейка и сетка), ремонт 57300, передача опций "
		     "50300 — кадры в Reports/batch16/hunt_marks и живая геометрия цифры; узлы всех 7 окон с маской числа из кода эталона. "
		     "`tag=after` `quit`. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			if (!W) { return; }
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.SelfTest.CountPlace"), KitArgs)) { return; }
			TSharedRef<FCtx> C = MakeShared<FCtx>();
			C->W = W;
			C->bQuit = KitArgs.bQuit;
			C->Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/batch16/hunt_marks"));
			for (const FString& A : KitArgs.Pos)
			{
				if (A.StartsWith(TEXT("tag="))) { C->Tag = A.Mid(4); }
			}
			Build(C);
			C->NextTime = FPlatformTime::Seconds() + FMath::Max(0.f, KitArgs.Delay);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) -> bool
			{
				if (!C->W.IsValid()) { return false; }
				if (C->bAwaitShot)
				{
					if (FScreenshotRequest::IsScreenshotRequested()) { return true; }
					if (C->FramesAfterShot++ < 2) { return true; }
					C->bAwaitShot = false;
				}
				if (FPlatformTime::Seconds() < C->NextTime) { return true; }
				if (!C->Steps.IsValidIndex(C->Index))
				{
					Finish(C);
					return false;
				}
				const float Wait = C->Steps[C->Index++]();
				if (Wait < 0.f) { C->bAwaitShot = true; C->FramesAfterShot = 0; C->NextTime = 0.0; }
				else { C->NextTime = FPlatformTime::Seconds() + Wait; }
				return true;
			}));
		}));
}

#endif // !UE_BUILD_SHIPPING
