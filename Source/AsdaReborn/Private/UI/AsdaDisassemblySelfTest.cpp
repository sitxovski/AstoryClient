// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ОКНА РАЗБОРКИ 32000 (волна 2-А, 06.10.2026, отчёт `Reports/owner_1006/wave2A_disassembly.md`). [ПОРТ]
//
// ★ ЧТО ПРОВЕРЯЕТ — ПУТЁМ ИГРОКА: входы — хоткей 40 (`RunCommandForCheck`), доп-меню (`DispatchCommand(4)`), кнопка
//   инвентаря 453 НАСТОЯЩИМ кликом Slate по живой геометрии; клики по позициям списка, «Начать», X и двойной клик по
//   32004 — тоже Slate (`UAsdaWindowSubsystem::SimulateClickAt`); приём 6503 — штатным `HandlePacket`
//   (`UAsdaNetSubsystem::DebugFeedPacket`); отправка 6502 — БОЕВЫМ отправителем под перехватом исходящих
//   (`UAsdaPetSubsystem::BeginSendCapture`, в сеть не уходит ничего). Контрольные пары — у каждого исправления (ключи
//   Д-1…Д-4) и у каждого гейта (окно открыто ↔ закрыто).
//   Состояние (сумка 0…29, расходники 0…59, ключи, перехват) восстанавливается в конце.
//
// Консоль: `asda.Craft.SelfTestDisassembly [delay=N] [shot] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки проверок — «САМОТЕСТ-32000 ok/FAIL», итог — «САМОТЕСТ-32000 ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestSeq.h"   // ★ R5-fix-b 13.11.2026 (заключительный заход): шаги по событию «ввод доставлен», а не по часам

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Items/AsdaItemTableSubsystem.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "Pet/AsdaPetSubsystem.h"
#include "UI/AsdaDisassemblySubsystem.h"
#include "UI/AsdaExtraMenuSubsystem.h"
#include "UI/AsdaHighRefineSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDisasmTest, Log, All);

namespace AsdaDisasmTestLocal
{
	// Счётчики прогона.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;   // пройдено / провалено
		// Одна проверка: печать и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaDisasmTest, Log, TEXT("САМОТЕСТ-32000 ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaDisasmTest, Error, TEXT("САМОТЕСТ-32000 FAIL %s"), *What); }
		}
	};

	// Перехваченная отправка: опкод и тело (payload без конверта).
	using FSent = TArray<TPair<int32, TArray<uint8>>>;

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		FRun R;                                   // счётчики
		TWeakObjectPtr<UWorld> W;                 // мир прогона
		TSharedPtr<AsdaSelfTestSeq::FSeq> Seq;      // очередь шагов `At` (★ R5-fix-b 13.11.2026 (заключительный заход))
		float Delay = 0.f;                        // сдвиг всех шагов
		bool bQuit = false;                       // выйти после итога
		bool bShot = false;                       // снимать кадры
		TArray<TArray<uint8>> SavedBag;           // сумка 0…59 до прогона (сырые записи; пусто — ячейка пуста; ★ 29.10.2026 было 0…29)
		TArray<TArray<uint8>> SavedSpend;         // расходники 0…59 до прогона
		TSharedPtr<FSent> Sent;                   // перехваченные исходящие
		bool bCapture = false;                    // перехват взведён этим прогоном
		int32 CodeA = 0, CodeB = 0, CodeC = 0, CodeE = 0, CodeD = 0;   // A доспех 0 %, B оружие 90 %, C с самоцветом, E доспех 80 %, D не разбирается
		int32 ResCode6 = 0;                       // код превью 6 у A (результат «лучший»)
		int32 WeightWas = -1;                     // вес до прогона
		int32 Sent6502Before = 0;                 // счётчик 6502 до шага
		int32 JournalMark = 0;                    // длина журнала окна до шага
	};

	static constexpr int32 kCellA = 3, kCellB = 7, kCellC = 10, kCellD = 12, kCellE = 15;   // ячейки сумки
	static constexpr int32 kResSpendCell = 40;                                               // ячейка расходников для материала

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Запустить шаг через Sec секунд (+ общий сдвиг).
	//   ★ R5-fix-b 13.11.2026 (заключительный заход) (ревизор третьего захода, зам. 7.2): шаги — общей очередью `AsdaSelfTestSeq` (по порядку расписания,
	//   каждый — после доставки заказанного щелчка и того же промежутка после предыдущего). Прежде мировой таймер ставил
	//   проверку 9 на 8,5 с, а второй щелчок по 32004 мог ждать отпускания первого до 13 с.
	static void At(const TSharedRef<FCtx>& C, float Sec, TFunction<void()> Fn)
	{
		AsdaSelfTestSeq::At(C, Sec + C->Delay, MoveTemp(Fn));
	}

	// Настоящий клик Slate по центру живой геометрии узла.
	static bool ClickNode(const TSharedRef<FCtx>& C, UWidget* Node, const FString& What)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		FVector2D Mn, Mx;
		const bool bGeo = WM && Node && AsdaUi::WidgetAbsRect(Node, Mn, Mx);
		C->R.Expect(bGeo, FString::Printf(TEXT("клик «%s»: живая геометрия есть"), *What));
		if (bGeo) { WM->SimulateClickAt((Mn + Mx) * 0.5f, /*HoldFrames*/ 3); }
		return bGeo;
	}
	// ★ R5-fix-b 13.11.2026 (третий заход): выполнить Fn, когда заказанный симклик доставлен (нажатие, удержание, отпускание идут по кадрам; новый
	//   заказ затёр бы недоставленный). Опрос раз в кадр, потолок 5 с — затем Fn всё равно (проверка покажет, что не так).
	static void AfterSimClick(const TSharedRef<FCtx>& C, TFunction<void()> Fn, double Deadline = 0.0)
	{
		UWorld* W = C->W.Get();
		if (!W) { return; }
		const double Now = FPlatformTime::Seconds();
		if (Deadline <= 0.0) { Deadline = Now + 5.0; }
		const UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		if (WM && WM->IsSimClickPending() && Now < Deadline)
		{
			W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([C, Fn = MoveTemp(Fn), Deadline]() mutable { AfterSimClick(C, MoveTemp(Fn), Deadline); }));
			return;
		}
		Fn();
	}
	// Клик по контролу окна 32000 по IDNum.
	static bool ClickId(const TSharedRef<FCtx>& C, int32 IdNum)
	{
		UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
		return ClickNode(C, S ? S->GetHitWidget(IdNum) : nullptr, FString::Printf(TEXT("%d"), IdNum));
	}

	// Есть ли текст в любом текстовом узле окна WinId (нотис ONEBUT).
	// ★ 31.10.2026: с 09.10 тело ONEBUT после показа — URichTextBlock (`SetMsgBoxBodyFormatted`), а не UTextBlock;
	//   видимый текст обоих видов читает общая точка подсистемы `ReadMsgBoxShownText` (разметку RichText снимает).
	static bool WinHasText(const TSharedRef<FCtx>& C, const TCHAR* WinId, const FString& Needle)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UUserWidget* W = WM ? WM->FindWindow(FName(WinId)) : nullptr;
		if (!W || !W->WidgetTree || Needle.IsEmpty()) { return false; }
		TArray<UWidget*> All;
		W->WidgetTree->GetAllWidgets(All);
		for (UWidget* X : All)
		{
			if (UAsdaWindowSubsystem::ReadMsgBoxShownText(X).Contains(Needle)) { return true; }
		}
		return false;
	}
	// Закрыть однокнопочный нотис, если открыт.
	static void CloseMsgBox(const TSharedRef<FCtx>& C)
	{
		if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX"))); }
	}
	// Есть ли в журнале окна строка после метки.
	static bool JHas(const TSharedRef<FCtx>& C, const TCHAR* What)
	{
		UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
		if (!S) { return false; }
		const TArray<FString>& J = S->GetJournal();
		for (int32 I = FMath::Max(0, C->JournalMark); I < J.Num(); ++I) { if (J[I].Contains(What)) { return true; } }
		return false;
	}
	// Поставить метку журнала (проверки «после шага»).
	static void Mark(const TSharedRef<FCtx>& C)
	{
		if (UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C)) { S->ClearJournal(); }
		C->JournalMark = 0;
	}
	// Сколько перехвачено опкода Op; OutLast — тело последнего.
	using AsdaSelfTestCtx::CountOp;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Положить запись в инвентарь боевой точкой (`ApplyExternalRecord`, как приёмники пакетов).
	static void PutRec(const TSharedRef<FCtx>& C, int32 Cont, int32 Cell, int32 Code, int32 Amount, int32 Pct, int32 Gem)
	{
		UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C);
		if (!Inv) { return; }
		if (Code <= 0) { Inv->ClearCell(Cont, Cell); return; }
		// ★ U17: запись ячейки + гнездо 0 и процент заточки.
		FAsdaItemRecordWriter Rw;
		Rw.Code(Code).Container(Cont).Slot(Cell).Second(-1).Amount(Amount).Sowel(0, Gem).EnchantPercent(Pct);
		const TArray<uint8> R = Rw.ToArray();
		Inv->ApplyExternalRecord(R, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	}
	// Тело 6503: код, вес, запись предмета (код 0 — очищена) и запись материала.
	static TArray<uint8> Body6503(int32 Code, int32 Weight, int32 SrcCell, int32 ResCode, int32 ResCell, int32 Given)
	{
		TArray<uint8> B; B.SetNumZeroed(119);
		B[0] = (uint8)Code; AsdaBytes::WrI16(&B[1], Weight);
		B[3 + 4] = 1; AsdaBytes::WrI16(&B[3 + 5], SrcCell);                  // разобранный: контейнер 1, код 0
		AsdaBytes::WrI32(&B[60], ResCode); B[60 + 4] = 2; AsdaBytes::WrI16(&B[60 + 5], ResCell);
		AsdaBytes::WrI16(&B[60 + 7], -1); AsdaBytes::WrI32(&B[60 + 9], Given);
		AsdaBytes::WrI16(&B[117], Given);
		return B;
	}
	static void Feed(const TSharedRef<FCtx>& C, const TArray<uint8>& B)
	{
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C)) { Net->DebugFeedPacket(6503, B); }
	}
	// Ключ консоли.
	static void SetCVar(const TCHAR* Name, const TCHAR* Val)
	{
		AsdaCVarSave::SetForTest(Name, Val);
	}
	static void Shot(const TSharedRef<FCtx>& C, const TCHAR* Name)
	{
		if (!C->bShot) { return; }
		UE_LOG(LogAsdaDisasmTest, Warning, TEXT("САМОТЕСТ-32000: КАДР → %s.png"), Name);
		FScreenshotRequest::RequestScreenshot(Name, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
	}
	static void Dump(const TSharedRef<FCtx>& C, const TCHAR* Step)
	{
		if (UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C))
		{
			UE_LOG(LogAsdaDisasmTest, Log, TEXT("САМОТЕСТ-32000: %s\n%s"), Step, *S->DumpState());
			for (const FString& L : S->GetJournal()) { UE_LOG(LogAsdaDisasmTest, Log, TEXT("  журнал: %s"), *L); }
		}
	}

	// ── Подбор предметов из таблицы: разбираемые, есть в item_list, тип и иконка ──
	static bool PickItems(const TSharedRef<FCtx>& C)
	{
		UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
		UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(C);
		if (!S || !IT) { return false; }
		const UDataTable* DT = IT->GetDataTable(FName(TEXT("decompositiontable")));
		if (!DT) { return false; }
		TArray<FAsdaDecompositiontableRow*> Rows;
		DT->GetAllRows<FAsdaDecompositiontableRow>(TEXT("DisasmTest"), Rows);
		auto IconOk = [IT](int32 Code) { const FAsdaItemRecord* R = IT->GetItem(Code); const FAsdaImgRecord* I = R ? IT->GetImgRecord(R->ImgIndex) : nullptr; return I && !I->IconAtlasMale.IsEmpty(); };
		for (const FAsdaDecompositiontableRow* Row : Rows)
		{
			if (!Row) { continue; }
			const FAsdaItemRecord* R = IT->GetItem(Row->ItemCode);
			if (!R || !IconOk(Row->ItemCode)) { continue; }
			if (Row->ResultDrawCount != 1)
			{
				if (!C->CodeD) { C->CodeD = Row->ItemCode; }   // есть в таблице, но не разбирается — контроль списка
				continue;
			}
			const int32 Res[6] = { Row->Result0Code, Row->Result1Code, Row->Result2Code, Row->Result3Code, Row->Result4Code, Row->Result5Code };
			bool bDistinct = true;
			for (int32 I = 0; I < 6; ++I) { for (int32 J = 0; J < I; ++J) { if (Res[I] <= 0 || Res[I] == Res[J]) { bDistinct = false; } } }
			if (R->ItemType <= 4 && bDistinct && !C->CodeA) { C->CodeA = Row->ItemCode; C->ResCode6 = Res[5]; continue; }
			if (R->ItemType == 9 && !C->CodeB) { C->CodeB = Row->ItemCode; continue; }
			if (R->ItemType <= 4 && !C->CodeC) { C->CodeC = Row->ItemCode; continue; }
			if ((R->ItemType <= 4 || R->ItemType == 8) && !C->CodeE) { C->CodeE = Row->ItemCode; continue; }
		}
		return C->CodeA && C->CodeB && C->CodeC && C->CodeE && C->CodeD;
	}

	static void Finish(TSharedRef<FCtx> C);

	// ═══════════════════════════ ПРОГОН ═══════════════════════════
	static void Run(TSharedRef<FCtx> C)
	{
		UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
		UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C);
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaPetSubsystem* Pet = Sub<UAsdaPetSubsystem>(C);
		FRun& R = C->R;
		if (!S || !Inv || !Net || !Pet) { R.Expect(false, TEXT("подсистемы окна/сумки/сети/питомцев есть")); Finish(C); return; }
		// Сохранить сумку и расходники.
		for (int32 I = 0; I < UAsdaInventorySubsystem::kShellSlots; ++I) { const FAsdaItemInstance* X = Inv->GetCell(1, I); C->SavedBag.Add(X && !X->IsEmpty() ? X->Raw : TArray<uint8>()); }   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
		for (int32 I = 0; I < 60; ++I) { const FAsdaItemInstance* X = Inv->GetCell(2, I); C->SavedSpend.Add(X && !X->IsEmpty() ? X->Raw : TArray<uint8>()); }
		C->WeightWas = Net->SelfWeightCur;
		const bool bPicked = PickItems(C);   // отдельно: порядок вычисления аргументов `Expect` не задан
		R.Expect(bPicked, FString::Printf(TEXT("подобраны предметы: A=%d B=%d C=%d E=%d, неразбираемый D=%d"), C->CodeA, C->CodeB, C->CodeC, C->CodeE, C->CodeD));
		for (int32 I = 0; I < UAsdaInventorySubsystem::kShellSlots; ++I) { Inv->ClearCell(1, I); }   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
		for (int32 I = 1; I < 60; ++I) { Inv->ClearCell(2, I); }
		PutRec(C, 1, kCellA, C->CodeA, 0, 0, 0);
		PutRec(C, 1, kCellB, C->CodeB, 0, 90, 0);
		PutRec(C, 1, kCellC, C->CodeC, 0, 0, 7001);
		PutRec(C, 1, kCellD, C->CodeD, 0, 0, 0);
		PutRec(C, 1, kCellE, C->CodeE, 0, 80, 0);
		// Перехват исходящих на весь прогон (в сеть не уходит ничего).
		C->Sent = MakeShared<FSent>();
		TSharedPtr<FSent> Sent = C->Sent;
		C->bCapture = Pet->BeginSendCapture([Sent](int32 Op, const TArray<uint8>& Body, bool, TCHAR) { Sent->Add(TPair<int32, TArray<uint8>>(Op, Body)); }, 0);
		R.Expect(C->bCapture, TEXT("перехват исходящих взведён"));
		if (S->IsOpen()) { S->CloseWindow(); }
		Mark(C);

		// ── 1. Хоткей 40 (боевой `ExecuteCommand`) — открыть; разбор контролов; список ──
		At(C, 0.5f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(C->W.IsValid() ? C->W->GetFirstPlayerController() : nullptr);
			FRun& R = C->R;
			R.Expect(PC != nullptr, TEXT("контроллер игрока есть"));
			if (PC) { PC->RunCommandForCheck(40); }
			R.Expect(S->IsOpen() && S->GetState() == 0, FString::Printf(TEXT("1: хоткей 40 открыл окно, [89] = %d"), S->GetState()));
			R.Expect(S->GetTableCount() > 0, FString::Printf(TEXT("1: таблица разборки загружена (%d записей)"), S->GetTableCount()));
			R.Expect(S->GetFoundControls() == S->GetTotalControls() && S->GetTotalControls() > 0,
				FString::Printf(TEXT("1: структурный разбор: найдено %d/%d контролов"), S->GetFoundControls(), S->GetTotalControls()));
			UE_LOG(LogAsdaDisasmTest, Log, TEXT("%s"), *S->DumpControls());
			const TArray<int32>& L = S->GetListCells();
			R.Expect(L.Num() == 4 && L[0] == kCellA && L[1] == kCellB && L[2] == kCellC && L[3] == kCellE,
				FString::Printf(TEXT("1: список = ячейки A/B/C/E по порядку сумки, D (флаг 0) отсутствует (позиций %d)"), L.Num()));
			R.Expect(S->GetButtonText() == AsdaNotice::Loc(GIOf(C), AsdaDisassemblyLoc::kBtnStart), FString::Printf(TEXT("1: кнопка 32002 = 98700 «%s»"), *S->GetButtonText()));
			R.Expect(S->GetLabelText(32003) == AsdaNotice::Loc(GIOf(C), AsdaDisassemblyLoc::kTitle), FString::Printf(TEXT("1: заголовок 32003 = 100000 «%s»"), *S->GetLabelText(32003)));
			R.Expect(S->GetLabelText(32050 + 1) == TEXT("+90%"), FString::Printf(TEXT("1: позиция B подписана «+90%%» («%s»)"), *S->GetLabelText(32051)));
			R.Expect(!S->IsControlShown(32005) && !S->IsControlShown(32031) && !S->IsControlShown(32090) && !S->IsControlShown(32020),
				TEXT("1: 32005/32031/32090/32020 скрыты при открытии"));
			R.Expect(S->GetGaugeSlotSize().Y == 0.f, FString::Printf(TEXT("1: полоса 32030 высотой 0 (%.0f)"), S->GetGaugeSlotSize().Y));
		});
		// Живая геометрия — кадром позже открытия (в кадре открытия раскладка Slate ещё не посчитана).
		At(C, 0.75f, [C]() { GEngine->Exec(C->W.Get(), TEXT("asda.UI.TreeGeom dis_open WBP_Disassembly_Win")); });
		At(C, 0.8f, [C]() { Shot(C, TEXT("asda_disasm_open")); });
		// ── 2. Тумблеры: хоткей 40 закрывает, доп-меню 4 открывает и закрывает ──
		At(C, 1.1f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(C->W->GetFirstPlayerController());
			FRun& R = C->R;
			if (PC) { PC->RunCommandForCheck(40); }
			R.Expect(!S->IsOpen() && S->GetState() == -1, TEXT("2: хоткей 40 повторно — окно закрыто (тумблер), [89] = −1"));
			UAsdaExtraMenuSubsystem* EM = Sub<UAsdaExtraMenuSubsystem>(C);
			if (EM) { EM->DispatchCommand(4, TEXT("самотест")); }
			R.Expect(S->IsOpen(), TEXT("2: доп-меню команда 4 открыла окно"));
			if (EM) { EM->DispatchCommand(4, TEXT("самотест")); }
			R.Expect(!S->IsOpen(), TEXT("2: доп-меню команда 4 повторно закрыла окно"));
			UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C);
			if (Inv && !Inv->IsWindowOpen()) { Inv->OpenWindow(); }
		});
		// ── 3. Кнопка инвентаря 453 — НАСТОЯЩИМ кликом Slate ──
		At(C, 1.6f, [C]()
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			UUserWidget* InvW = WM ? WM->FindWindow(FName(TEXT("INVEN_WIN"))) : nullptr;
			ClickNode(C, InvW ? InvW->GetWidgetFromName(FName(TEXT("Inven_Disassembly_Btn"))) : nullptr, TEXT("кнопка инвентаря 453"));
		});
		At(C, 2.1f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			C->R.Expect(S->IsOpen(), TEXT("3: кнопка инвентаря 453 (клик Slate) открыла окно"));
			ClickId(C, 32001);   // X
		});
		At(C, 2.6f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C);
			C->R.Expect(!S->IsOpen(), TEXT("3: X 32001 (клик Slate) закрыл окно"));
			C->R.Expect(Inv->IsWindowOpen() && JHas(C, TEXT("сумка снова открыта")), TEXT("3: была открыта сумка ([222]) — после закрытия окна она снова открыта"));
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			UUserWidget* InvW = WM ? WM->FindWindow(FName(TEXT("INVEN_WIN"))) : nullptr;
			ClickNode(C, InvW ? InvW->GetWidgetFromName(FName(TEXT("Inven_Disassembly_Btn"))) : nullptr, TEXT("кнопка инвентаря 453 (повтор)"));
		});
		// ── 4. Выбор A кликом Slate: превью = таблица, рамка над позицией, тултип наведением ──
		At(C, 3.1f, [C]()
		{
			C->R.Expect(Sub<UAsdaDisassemblySubsystem>(C)->IsOpen(), TEXT("4: окно снова открыто кнопкой 453"));
			ClickId(C, 32050);
		});
		At(C, 3.6f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			FRun& R = C->R;
			const FAsdaDecompositiontableRow* Row = S->FindRecord(C->CodeA);
			const int32 Want[6] = { Row ? Row->Result0Code : -9, Row ? Row->Result1Code : -9, Row ? Row->Result2Code : -9,
				Row ? Row->Result3Code : -9, Row ? Row->Result4Code : -9, Row ? Row->Result5Code : -9 };
			bool bPrev = true;
			for (int32 K = 0; K < 6; ++K) { if (S->GetPreviewCode(K) != Want[K]) { bPrev = false; } }
			R.Expect(S->GetSelCell() == kCellA && bPrev, FString::Printf(TEXT("4: клик Slate по 32050 → выбран A (ячейка %d), 6 превью = результаты таблицы"), S->GetSelCell()));
			R.Expect(S->IsControlShown(32090) && S->GetSelFramePos().Equals(FVector2D::ZeroVector), FString::Printf(TEXT("4: рамка 32090 видна над позицией 0 (%.0f, %.0f)"), S->GetSelFramePos().X, S->GetSelFramePos().Y));
			Dump(C, TEXT("выбор A"));
			// Наведение на иконку позиции 1 (B) — тултип владельца «разборка».
			FVector2D Mn, Mx;
			if (AsdaUi::WidgetAbsRect(S->GetHitWidget(32051), Mn, Mx)) { FSlateApplication::Get().SetCursorPos((Mn + Mx) * 0.5f); }
		});
		At(C, 3.9f, [C]()
		{
			UAsdaItemTooltipSubsystem* Tip = Sub<UAsdaItemTooltipSubsystem>(C);
			C->R.Expect(Tip && Tip->IsTooltipShown() && Tip->GetTipOwner() == EAsdaTipOwner::Disassembly, TEXT("4: наведение на позицию списка → тултип предмета (владелец Disassembly)"));
			FSlateApplication::Get().SetCursorPos(FVector2D(2.0, 2.0));
			ClickId(C, 32052);   // C — с самоцветом
		});
		At(C, 4.4f, [C]()
		{
			UAsdaItemTooltipSubsystem* Tip = Sub<UAsdaItemTooltipSubsystem>(C);
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			const FString T = AsdaNotice::Loc(GIOf(C), AsdaDisassemblyLoc::kGemWarn);
			C->R.Expect(Tip && Tip->GetTipOwner() != EAsdaTipOwner::Disassembly, TEXT("4: курсор ушёл — тултип окна снят"));
			C->R.Expect(WM->IsOpen(FName(TEXT("ONEBUT_MSGBOX"))) && WinHasText(C, TEXT("ONEBUT_MSGBOX"), T.Left(20)),
				TEXT("5: предмет с самоцветом (+16 > 0) → ONEBUT 100800 «Живой камень уничтожается…»"));
			CloseMsgBox(C);
		});
		At(C, 4.7f, [C]() { ClickId(C, 32051); });   // B — оружие 90 %
		At(C, 5.2f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			bool b401 = true;
			for (int32 K = 0; K < 6; ++K) { if (S->GetPreviewCode(K) != 401 || S->GetPreviewPercent(K) != 90 || S->GetLabelText(32010 + K) != TEXT("+90%")) { b401 = false; } }
			C->R.Expect(b401, TEXT("6: оружие 90 % ≥ 88 → все 6 превью = 401 «+90%» (перенос улучшения)"));
			C->R.Expect(!Sub<UAsdaWindowSubsystem>(C)->IsOpen(FName(TEXT("ONEBUT_MSGBOX"))), TEXT("6: контроль — без самоцветов ONEBUT нет"));
			ClickId(C, 32053);   // E — доспех 80 %
		});
		At(C, 5.7f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			const FAsdaDecompositiontableRow* Row = S->FindRecord(C->CodeE);
			C->R.Expect(Row && S->GetPreviewCode(0) == Row->Result0Code && S->GetPreviewPercent(0) == 0,
				TEXT("7 (Д-1): доспех 80 % < 88 → превью по таблице (как сервер)"));
			SetCVar(TEXT("asda.Disassembly.TransferThreshold"), TEXT("77"));
			ClickId(C, 32053);
		});
		At(C, 6.2f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			C->R.Expect(S->GetPreviewCode(5) == 363 && S->GetPreviewPercent(5) == 80, TEXT("7: контрольная пара — порог 77 (оригинал) → превью 363 «+80%»"));
			SetCVar(TEXT("asda.Disassembly.TransferThreshold"), TEXT("88"));
			ClickId(C, 32050 + 10);   // пустая позиция
		});
		At(C, 6.7f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			C->R.Expect(S->GetSelCell() == -1 && !S->IsControlShown(32090), TEXT("8 (Д-4): клик по пустой позиции — выбор снят, рамка 32090 погасла"));
			SetCVar(TEXT("asda.Disassembly.HideFrameWithoutSelection"), TEXT("0"));
			ClickId(C, 32050);
		});
		At(C, 7.1f, [C]() { ClickId(C, 32050 + 10); });
		At(C, 7.5f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			C->R.Expect(S->GetSelCell() == -1 && S->IsControlShown(32090), TEXT("8: контрольная пара — ключ 0 (оригинал): рамка остаётся при снятом выборе"));
			SetCVar(TEXT("asda.Disassembly.HideFrameWithoutSelection"), TEXT("1"));
			ClickId(C, 32050);
		});
		// ── 9. Двойной клик Slate по 32004 — сброс ──
		At(C, 7.9f, [C]() { ClickId(C, 32004); });
		// ★ R5-fix-b 13.11.2026 (третий заход): второй щелчок — после ОТПУСКАНИЯ первого (под нагрузкой заказ на 8.06 с приходил до отпускания и
		//   затирал первый — двойного щелчка не получалось). Время 8.06 с — самое раннее, как было.
		At(C, 8.06f, [C]() { AfterSimClick(C, [C]() { ClickId(C, 32004); }); });
		At(C, 8.5f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			C->R.Expect(S->GetSelCell() == -1 && S->GetPreviewCode(0) == 0 && !S->IsControlShown(32090) && JHas(C, TEXT("0xD 32004")),
				TEXT("9: двойной клик Slate по 32004 (0xD) → сброс выбора и превью, рамка погасла"));
			ClickId(C, 32050);
		});
		// ── 10. «Начать» без свободной ячейки расходников → ONEBUT 100300, 6502 нет ──
		At(C, 8.9f, [C]()
		{
			for (int32 I = 1; I < 60; ++I) { PutRec(C, 2, I, C->ResCode6, 1, 0, 0); }
			C->Sent6502Before = CountOp(C, 6502);
			ClickId(C, 32002);
		});
		At(C, 9.4f, [C]()
		{
			const FString T = AsdaNotice::Loc(GIOf(C), AsdaDisassemblyLoc::kNoSpace);
			C->R.Expect(Sub<UAsdaWindowSubsystem>(C)->IsOpen(FName(TEXT("ONEBUT_MSGBOX"))) && WinHasText(C, TEXT("ONEBUT_MSGBOX"), T.Left(20)) && CountOp(C, 6502) == C->Sent6502Before,
				TEXT("10: нет свободной ячейки расходников → ONEBUT 100300, 6502 не ушёл"));
			CloseMsgBox(C);
			for (int32 I = 1; I < 60; ++I) { Sub<UAsdaInventorySubsystem>(C)->ClearCell(2, I); }
			Sub<UAsdaInventorySubsystem>(C)->OpenWindow();
			Mark(C);
			ClickId(C, 32002);
		});
		// ── 11. Отправка 6502 боевым отправителем под перехватом; замок Д-2 и его контрольная пара ──
		At(C, 9.9f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			TArray<uint8> B;
			const int32 N = CountOp(C, 6502, &B);
			C->R.Expect(N == C->Sent6502Before + 1 && B.Num() == 3 && B[0] == 1 && AsdaBytes::RdI16(B.GetData() + 1) == kCellA,
				FString::Printf(TEXT("11: 6502 ушёл боевым путём: тело %d б = [контейнер %d, слот %d] (ждали [1, %d])"), B.Num(), B.Num() ? B[0] : -1, B.Num() >= 3 ? AsdaBytes::RdI16(B.GetData() + 1) : -1, kCellA));
			C->R.Expect(S->GetButtonText() == AsdaNotice::Loc(GIOf(C), AsdaDisassemblyLoc::kBtnBusy) && S->IsInFlight(), TEXT("11: кнопка 98701 «идет демонтаж», замок «6502 в полёте»"));
			C->R.Expect(!Sub<UAsdaInventorySubsystem>(C)->IsWindowOpen(), TEXT("11: после 6502 сумка закрыта (`sowelWindowClose`)"));
			C->Sent6502Before = N;
			ClickId(C, 32002);
		});
		At(C, 10.4f, [C]()
		{
			C->R.Expect(CountOp(C, 6502) == C->Sent6502Before, TEXT("11 (Д-2): второе «Начать» до ответа — 6502 НЕ ушёл"));
			SetCVar(TEXT("asda.Disassembly.SingleInFlight"), TEXT("0"));
			ClickId(C, 32002);
		});
		At(C, 10.9f, [C]()
		{
			C->R.Expect(CountOp(C, 6502) == C->Sent6502Before + 1, TEXT("11: контрольная пара — ключ 0 (оригинал): второй 6502 ушёл"));
			SetCVar(TEXT("asda.Disassembly.SingleInFlight"), TEXT("1"));
			Mark(C);
			// ── 12. 6503 код 1 штатным `HandlePacket`: результат = превью 6 (лучший) ──
			Feed(C, Body6503(1, 1234, kCellA, C->ResCode6, kResSpendCell, 3));
		});
		At(C, 11.2f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			FRun& R = C->R;
			R.Expect(S->GetState() == 1 && S->IsControlShown(32005) && S->GetSelSlot() == 6 && !S->IsInFlight(),
				FString::Printf(TEXT("12: 6503 код 1 → [89] = 1, эффект 32005, слот результата %d (ждали 6), замок снят"), S->GetSelSlot()));
			R.Expect(JHas(C, TEXT("snd loop S_e_disjoint_process")), TEXT("12: петля S_e_disjoint_process запущена"));
			R.Expect(Net->SelfWeightCur == 1234, FString::Printf(TEXT("12: вес из 6503 = %d (ждали 1234)"), Net->SelfWeightCur));
			R.Expect(AsdaUi::CheckRecentActivity(GIOf(C)), TEXT("12: общая точка uiCheckRecentActivity — истина при «идёт» (условие 2)"));
			// Закрыть при «идёт» нельзя: X (Slate), ESC, тумблер.
			ClickId(C, 32001);
		});
		At(C, 11.6f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			FRun& R = C->R;
			R.Expect(S->IsOpen(), TEXT("12: X при «идёт» — окно НЕ закрыто"));
			if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->CloseTopmostOnEsc(); }
			R.Expect(S->IsOpen(), TEXT("12: ESC при «идёт» — окно НЕ закрыто"));
			S->Toggle(TEXT("самотест: тумблер при «идёт»"));
			R.Expect(S->IsOpen(), TEXT("12: тумблер при «идёт» — окно НЕ закрыто"));
			// Гейты чужих окон: «ОК» 78200, разговор с NPC, элитный магазин.
			if (UAsdaHighRefineSubsystem* HR = Sub<UAsdaHighRefineSubsystem>(C))
			{
				HR->ClearJournal();
				HR->OnOkClicked();
				bool bGate = false;
				for (const FString& L : HR->GetJournal()) { if (L.Contains(TEXT("идёт разборка"))) { bGate = true; } }
				R.Expect(bGate, TEXT("12: «ОК» окна 78200 при «идёт» разборки — молча (g_bRefineInProgress)"));
			}
			UAsdaNpcDialogSubsystem* Dlg = Sub<UAsdaNpcDialogSubsystem>(C);
			const bool bDlg = Dlg && Dlg->OpenForNpcId(1);
			R.Expect(!bDlg && Dlg && !Dlg->IsDialogOpen() && S->IsOpen(), TEXT("12: окно 32000 видно → разговор с NPC молча не открыт (regionWindow_OpenAndPathMove)"));
			UAsdaPremiumShopSubsystem* PS = Sub<UAsdaPremiumShopSubsystem>(C);
			if (PS) { PS->OpenWindow(false); }
			R.Expect(PS && !PS->IsOpen() && S->IsOpen(), TEXT("12: окно 32000 видно → элитный магазин 6000 молча не открыт (charBeginAutoMove)"));
		});
		At(C, 12.2f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			const float T = S->GetAnimTick();
			const FVector2D P = S->GetGaugeSlotPos(), Z = S->GetGaugeSlotSize();
			C->R.Expect(T > 15.f && T < 50.f && FMath::IsNearlyEqual((float)Z.Y, T) ,
				FString::Printf(TEXT("13 (Д-5): за ≈1.3 с полоса выросла на %.0f px при 30 кадрах эталона/с; высота 32030 = %.0f"), T, Z.Y));
			C->R.Expect(FMath::IsNearlyEqual((float)P.Y + (float)Z.Y, 206.f + 50.f), FString::Printf(TEXT("13: низ полосы на месте (верх %.0f + высота %.0f = база 206 + 50)"), P.Y, Z.Y));
			Shot(C, TEXT("asda_disasm_running"));
		});
		// ── 14. Конец полосы (слот 6 = 150 кадров ≈ 5 с) ──
		At(C, 16.6f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C);
			FRun& R = C->R;
			const FAsdaItemInstance* Src = Inv->GetCell(1, kCellA);
			const FAsdaItemInstance* Dst = Inv->GetCell(2, kResSpendCell);
			R.Expect(S->GetState() == 2 && S->GetAnimTick() == 150.f, FString::Printf(TEXT("14: полоса доиграла: [89] = %d, тик %.0f (ждали 2 / 150)"), S->GetState(), S->GetAnimTick()));
			R.Expect((!Src || Src->IsEmpty()) && Dst && Dst->ItemCode == C->ResCode6 && Dst->Amount == 3,
				TEXT("14: применение: ячейка A в сумке очищена, материал в расходниках 40 (код и количество 3)"));
			R.Expect(S->GetPreviewCode(5) == C->ResCode6 && S->GetResultCode() == C->ResCode6 && S->GetLabelText(32015) == TEXT("3") && S->GetLabelText(32016) == TEXT("3"),
				TEXT("14: превью 6 и 32016 = материал, подписи «3»"));
			bool bNotes = true;
			for (int32 I = 0; I < 6; ++I) { if (S->IsControlShown(32020 + I) != (I == 5)) { bNotes = false; } }
			R.Expect(bNotes && S->IsControlShown(32031) && !S->IsControlShown(32005) && !S->IsControlShown(32090),
				TEXT("14: рамка 32025 видна (только она), блёстки 32031 (слот 6), эффект 32005 и рамка списка погашены"));
			R.Expect(S->GetButtonText() == AsdaNotice::Loc(GIOf(C), AsdaDisassemblyLoc::kBtnDone), FString::Printf(TEXT("14: кнопка 98702 «%s»"), *S->GetButtonText()));
			int32 IStop = -1, IRes = -1;
			const TArray<FString>& J = S->GetJournal();
			for (int32 I = 0; I < J.Num(); ++I)
			{
				if (J[I].Contains(TEXT("snd stop S_e_disjoint_process (конец полосы)"))) { IStop = I; }
				if (J[I].Contains(TEXT("snd S_e_disjoint_result"))) { IRes = I; }
			}
			R.Expect(IStop >= 0 && IRes > IStop, TEXT("14: порядок звуков: петля заглушена → S_e_disjoint_result"));
			R.Expect(S->GetListCells().Num() == 3, FString::Printf(TEXT("14: список перестроен — A ушёл (позиций %d)"), S->GetListCells().Num()));
			R.Expect(AsdaUi::CheckRecentActivity(GIOf(C)), TEXT("14: < 1 с после применения — uiCheckRecentActivity истина (условие 4)"));
			Dump(C, TEXT("конец полосы"));
			Shot(C, TEXT("asda_disasm_done"));
		});
		At(C, 17.9f, [C]()
		{
			FRun& R = C->R;
			R.Expect(!AsdaUi::CheckRecentActivity(GIOf(C)), TEXT("14: контрольная пара — > 1 с после применения uiCheckRecentActivity ложна"));
			ClickId(C, 32002);   // «выполнено» без выбора — закрыть
		});
		At(C, 18.4f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			C->R.Expect(!S->IsOpen() && S->GetState() == -1, TEXT("15: «выполнено» без выбора ([89] = 2) закрыло окно"));
			// [222] эталон перезаписывает при КАЖДОМ `upgradeUiOnOpen`, даже когда окно не закрывается: тумблер шага 12
			//   («идёт») застал сумку закрытой (её закрыла отправка 6502) — значит, после закрытия окна сумка НЕ открывается.
			C->R.Expect(!Sub<UAsdaInventorySubsystem>(C)->IsWindowOpen() && !JHas(C, TEXT("сумка снова открыта")),
				TEXT("15: [222] перезаписан тумблером при «идёт» (сумка тогда была закрыта) — сумка не открывается, как у эталона"));
			if (UAsdaExtraMenuSubsystem* EM = Sub<UAsdaExtraMenuSubsystem>(C)) { EM->DispatchCommand(4, TEXT("самотест")); }
		});
		// ── 16. Коды отказа 6503: 0/2…9 → ONEBUT + чат 98600+(код−2) и сброс; код 0 — молча ──
		At(C, 18.8f, [C]() { ClickId(C, 32050); });   // B — первая позиция теперь
		{
			const int32 Codes[] = { 0, 2, 3, 4, 5, 6, 7, 8, 9 };
			for (int32 I = 0; I < UE_ARRAY_COUNT(Codes); ++I)
			{
				const int32 K = Codes[I];
				At(C, 19.2f + 0.4f * I, [C, K]() { Mark(C); Feed(C, Body6503(K, 777, kCellB, 0, 0, 0)); });
				At(C, 19.45f + 0.4f * I, [C, K]()
				{
					UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
					UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
					FRun& R = C->R;
					if (K == 0)
					{
						R.Expect(!WM->IsOpen(FName(TEXT("ONEBUT_MSGBOX"))) && !JHas(C, TEXT("msgbox+chat")) && S->GetState() == 0 && S->GetSelCell() == -1,
							TEXT("16: код 0 — молча, окно сброшено"));
					}
					else
					{
						const int32 Loc = AsdaDisassemblyLoc::kResBase + K - 2;
						const FString T = AsdaNotice::Loc(GIOf(C), Loc);
						R.Expect(WM->IsOpen(FName(TEXT("ONEBUT_MSGBOX"))) && WinHasText(C, TEXT("ONEBUT_MSGBOX"), T) && JHas(C, *FString::Printf(TEXT("msgbox+chat %d"), Loc)) && S->GetState() == 0,
							FString::Printf(TEXT("16: код %d → ONEBUT + чат %d «%s», окно сброшено"), K, Loc, *T));
						if (K == 8) { R.Expect(Sub<UAsdaNetSubsystem>(C)->SelfWeightCur == 777, TEXT("16: код 8 — вес обновлён")); }
						if (K == 2) { R.Expect(Sub<UAsdaNetSubsystem>(C)->SelfWeightCur != 777, TEXT("16: контроль — код 2 вес не трогает")); }
					}
					CloseMsgBox(C);
				});
			}
		}
		// ── 17. Закрытие посреди полосы «закрыть все окна» (Д-3) и контрольная пара ──
		At(C, 23.0f, [C]() { ClickId(C, 32050); });   // B (90 %, превью 401)
		At(C, 23.4f, [C]()
		{
			Mark(C);
			Feed(C, Body6503(1, 1300, kCellB, 401, kResSpendCell + 1, 1));
		});
		At(C, 23.7f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			C->R.Expect(S->GetState() == 1 && S->GetSelSlot() == 1, TEXT("17: 6503 код 1, результат 401 → слот 1"));
			Sub<UAsdaWindowSubsystem>(C)->CloseAllPlayerWindows();
			const FAsdaItemInstance* Src = Sub<UAsdaInventorySubsystem>(C)->GetCell(1, kCellB);
			C->R.Expect(!S->IsOpen() && S->GetState() == -1 && (!Src || Src->IsEmpty()) && JHas(C, TEXT("snd stop S_e_disjoint_process (закрытие окна (Д-3))")),
				TEXT("17 (Д-3): «закрыть все окна» посреди полосы — окно закрыто, результат применён, петля заглушена"));
			SetCVar(TEXT("asda.Disassembly.StopLoopOnClose"), TEXT("0"));
			S->Toggle(TEXT("самотест"));
		});
		At(C, 24.0f, [C]() { ClickId(C, 32050); });   // C (с самоцветом → ONEBUT 100800)
		At(C, 24.4f, [C]()
		{
			CloseMsgBox(C);
			Mark(C);
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			Feed(C, Body6503(1, 1300, kCellC, S->GetPreviewCode(0), kResSpendCell + 2, 1));
		});
		At(C, 24.7f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			C->R.Expect(S->GetState() == 1, TEXT("17: контроль — полоса идёт"));
			Sub<UAsdaWindowSubsystem>(C)->CloseAllPlayerWindows();
			C->R.Expect(!S->IsOpen() && !JHas(C, TEXT("snd stop")), TEXT("17: контрольная пара — ключ 0 (оригинал): петля НЕ заглушена при закрытии"));
			SetCVar(TEXT("asda.Disassembly.StopLoopOnClose"), TEXT("1"));
			S->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			C->R.Expect(JHas(C, TEXT("snd stop")), TEXT("17: сброс сессии глушит оставшуюся петлю"));
		});
		// ── 18а. msg 8 по 32000: курсор вошёл в окно — список пересобран; ESC при «не идёт» закрывает ──
		At(C, 25.0f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			FSlateApplication::Get().SetCursorPos(FVector2D(2.0, 2.0));
			S->Toggle(TEXT("самотест"));
			C->CodeD = S->GetListCells().Num();   // запомнить длину списка (поле больше не нужно)
			PutRec(C, 1, 20, C->CodeA, 0, 0, 0);   // новый разбираемый предмет в сумке
		});
		At(C, 25.3f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			C->R.Expect(S->GetListCells().Num() == C->CodeD, TEXT("18а: контроль — без входа курсора в окно список не пересобирается (как эталон)"));
			FVector2D Mn, Mx;
			if (AsdaUi::WidgetAbsRect(S->GetHitWidget(32004), Mn, Mx)) { FSlateApplication::Get().SetCursorPos(Mn + FVector2D(2.0, 2.0)); }
		});
		At(C, 25.6f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			C->R.Expect(S->GetListCells().Num() == C->CodeD + 1 && S->GetListCells().Contains(20), TEXT("18а: курсор вошёл в окно (msg 8 по 32000) — список пересобран, новый предмет в нём"));
			FSlateApplication::Get().SetCursorPos(FVector2D(2.0, 2.0));
			if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->CloseTopmostOnEsc(); }
			C->R.Expect(!S->IsOpen(), TEXT("18а: ESC при «не идёт» закрыл окно (0x15)"));
		});
		// ── 18. Снимок окон, слот 6: снимок → «закрыть все» → восстановление открывает окно ──
		At(C, 25.9f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			FRun& R = C->R;
			S->Toggle(TEXT("самотест"));
			WM->ClearWindowSnapshot(TEXT("самотест 32000"));
			WM->CaptureOpenWindows(UAsdaWindowSubsystem::EWindowSnapshotOwner::NpcDialog, TEXT("самотест 32000"));
			R.Expect(WM->IsWindowSlotCaptured(6), TEXT("18: открытое окно 32000 попало в снимок окон (слот 6)"));
			WM->CloseAllPlayerWindows();
			R.Expect(!S->IsOpen(), TEXT("18: «закрыть все окна» закрыло 32000 (роспись uiCloseAllWindows)"));
			WM->ReopenCapturedWindows(TEXT("самотест 32000"));
			R.Expect(S->IsOpen(), TEXT("18: восстановление снимка (uiDispatchWindowToggle case 6) открыло 32000"));
			// Контроль гейта разговора: окно ЗАКРЫТО → разговор открывается.
			S->CloseWindow();
			UAsdaNpcDialogSubsystem* Dlg = Sub<UAsdaNpcDialogSubsystem>(C);
			const bool bDlg = Dlg && Dlg->OpenForNpcId(1);
			R.Expect(bDlg && Dlg->IsDialogOpen(), TEXT("18: контрольная пара гейта — окно 32000 закрыто → разговор с NPC открывается"));
			if (Dlg && Dlg->IsDialogOpen()) { Dlg->Close(); }
		});
		// ── 19 (Д-6, ревизия С-1). Выбор → окно закрыто до ответа → 6503 код 1 при скрытом окне → открыть до конца полосы ──
		auto SelectCell = [](const TSharedRef<FCtx>& C, int32 Cell)
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			const int32 Idx = S->GetListCells().IndexOfByKey(Cell);
			C->R.Expect(Idx >= 0, FString::Printf(TEXT("19: ячейка %d в списке (позиция %d)"), Cell, Idx));
			if (Idx >= 0) { ClickId(C, 32050 + Idx); }
		};
		At(C, 26.5f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			if (S->IsOpen()) { S->CloseWindow(); }
			PutRec(C, 1, 21, C->CodeA, 0, 0, 0);
			PutRec(C, 1, 22, C->CodeA, 0, 0, 0);
			S->Toggle(TEXT("самотест Д-6"));
		});
		At(C, 26.8f, [C, SelectCell]() { SelectCell(C, 21); });
		At(C, 27.2f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			C->R.Expect(S->GetSelCell() == 21 && S->GetPreviewCode(5) == C->ResCode6, TEXT("19: выбран предмет ячейки 21"));
			S->CloseWindow();
			Mark(C);
			Feed(C, Body6503(1, 1400, 21, C->ResCode6, kResSpendCell + 3, 2));
		});
		At(C, 27.5f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			FRun& R = C->R;
			R.Expect(!S->IsOpen() && S->GetState() == 1, TEXT("19: 6503 код 1 при закрытом окне — полоса идёт скрыто"));
			S->Toggle(TEXT("самотест Д-6"));
			const FAsdaItemInstance* Src = Sub<UAsdaInventorySubsystem>(C)->GetCell(1, 21);
			const FAsdaItemInstance* Dst = Sub<UAsdaInventorySubsystem>(C)->GetCell(2, kResSpendCell + 3);
			R.Expect(S->IsOpen() && S->GetState() == 0 && (!Src || Src->IsEmpty()) && Dst && Dst->ItemCode == C->ResCode6 && Dst->Amount == 2
				&& JHas(C, TEXT("snd stop S_e_disjoint_process (открытие при идущей полосе (Д-6))")),
				TEXT("19 (Д-6): открытие посреди скрытой полосы — результат применён (предмет убран, материал положен), петля заглушена"));
			SetCVar(TEXT("asda.Disassembly.FinishOnReopen"), TEXT("0"));
		});
		At(C, 27.8f, [C, SelectCell]() { SelectCell(C, 22); });
		At(C, 28.2f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			S->CloseWindow();
			Mark(C);
			Feed(C, Body6503(1, 1400, 22, C->ResCode6, kResSpendCell + 4, 2));
		});
		At(C, 28.5f, [C]()
		{
			UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
			FRun& R = C->R;
			R.Expect(S->GetState() == 1, TEXT("19: контроль — полоса идёт скрыто"));
			S->Toggle(TEXT("самотест Д-6 контроль"));
			const FAsdaItemInstance* Src = Sub<UAsdaInventorySubsystem>(C)->GetCell(1, 22);
			R.Expect(S->GetState() == 0 && Src && !Src->IsEmpty() && !JHas(C, TEXT("snd stop")),
				TEXT("19: контрольная пара — ключ 0 (оригинал): результат не применён, петля не заглушена"));
			SetCVar(TEXT("asda.Disassembly.FinishOnReopen"), TEXT("1"));
			S->ResetSessionState(EAsdaSessionResetReason::LeftWorld);   // заглушить оставшуюся петлю
		});
		At(C, 28.8f, [C]() { Finish(C); });
	}

	static void Finish(TSharedRef<FCtx> C)
	{
		UAsdaDisassemblySubsystem* S = Sub<UAsdaDisassemblySubsystem>(C);
		UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C);
		UAsdaPetSubsystem* Pet = Sub<UAsdaPetSubsystem>(C);
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		if (S) { S->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		// ★ 11.10.2026 (В3 S1, загрязнение пакета): окно 32000 после контрольного шага 19 (Toggle) оставалось открытым — следующий
		//   самотест получал отказ элитного магазина 6000 «открыто окно разборки» (гейт charBeginAutoMove, В5 падал 7 проверками).
		if (S && S->IsOpen()) { S->CloseWindow(); }
		CloseMsgBox(C);
		if (C->bCapture && Pet) { Pet->EndSendCapture(); }
		if (Inv && C->SavedBag.Num() == UAsdaInventorySubsystem::kShellSlots)   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
		{
			for (int32 I = 0; I < UAsdaInventorySubsystem::kShellSlots; ++I)
			{
				if (C->SavedBag[I].Num() >= 57) { Inv->ApplyExternalRecord(C->SavedBag[I], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); } else { Inv->ClearCell(1, I); }
			}
			for (int32 I = 1; I < 60; ++I)
			{
				if (C->SavedSpend[I].Num() >= 57) { Inv->ApplyExternalRecord(C->SavedSpend[I], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); } else { Inv->ClearCell(2, I); }
			}
		}
		if (Net && C->WeightWas >= 0) { Net->SetSelfWeight(C->WeightWas, -1, TEXT("самотест 32000: возврат")); }
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи прогона (Disassembly.TransferThreshold/HideFrameWithoutSelection/SingleInFlight/
		//   StopLoopOnClose/FinishOnReopen) — к исходному значению И приоритету SetBy (прежде — константы «умолчаний» 88/1).
		AsdaCVarSave::RestoreAll(TEXT("asda.Craft.SelfTestDisassembly"));
		UE_LOG(LogAsdaDisasmTest, Log, TEXT("САМОТЕСТ-32000 ИТОГ: ok=%d fail=%d"), C->R.Ok, C->R.Fail);
		if (C->bQuit && C->W.IsValid())
		{
			UWorld* W = C->W.Get();
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([W]() { GEngine->Exec(W, TEXT("quit")); }), 1.0f, false);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GCmdSelfTestDisassembly(
		TEXT("asda.Craft.SelfTestDisassembly"),
		TEXT("Окно разборки 32000: три входа (хоткей 40, кнопка 453, доп-меню), список, превью, перенос улучшения, самоцветы, "
		     "свободные ячейки, 6502 под перехватом, 6503 все коды, полоса, закрытие, снимок окон, гейты. `delay=N` `shot` `quit`."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			if (!W) { return; }
			TSharedRef<FCtx> C = MakeShared<FCtx>();
			C->W = W;
			for (const FString& A : Args)
			{
				if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { C->bQuit = true; }
				if (A.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { C->bShot = true; }
			}
			C->Delay = AsdaSelfTestKit::DelayArgOr(Args, C->Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
			// ТОЛЬКО ОФЛАЙН: «Начать» шлёт 6502 — при живом соединении это настоящая разборка предмета.
			static const uint16 kGuardOps[] = { 6502 };
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Craft.SelfTestDisassembly"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
			{
				if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), TEXT("asda.Craft.SelfTestDisassembly"), C->bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
				Run(C);
			}), FMath::Max(0.01f, C->Delay), false);
			C->Delay = 0.f;   // шаги внутри `Run` отсчитываются от его запуска
		}));
}

#endif // !UE_BUILD_SHIPPING
