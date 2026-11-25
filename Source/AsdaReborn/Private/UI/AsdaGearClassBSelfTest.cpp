// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ КЛАССА B ПРАВКИ СНАРЯЖЕНИЯ (07.11.2026): `asda.SelfTest.GearClassB [quit]`.
//   Вещи Starry Swim, Celestial Swim, Indian и Spooky Pumpkin (в стоке предел 0) после правки данных: подсказка в сумке,
//   подсказка в окне 50300 и панели опций. Отчёт и цели — `Reports/gear_fix_20261107/`, кадры — там же в `frames/`.
//   Путь игрока: запись кладётся в ячейку сумки боевой точкой приёма, наведение и щелчок — событиями мыши Slate по живой
//   геометрии. Предел записи +26 = pricetable.dw03 (так его поднимает сервер, `sub_4C97C0`), опции записи — первые строки
//   групп OptionGroup0/1 самой вещи. Карта мира, без сети (-AsdaNoNet), язык — `--lang ru|en`.
//   Итог: «КЛАСС-B ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemOptionSubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGearClassBTest, Log, All);

namespace AsdaGearClassBTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.GearClassB");   // имя команды
	static const TCHAR* kPfx = TEXT("КЛАСС-B");                    // префикс строк и итога
	static constexpr int32 kLocUpgradesLeft = 50024;               // «Доступно улучшений:»
	static constexpr int32 kLocLevelLabel = 2330;                  // «Уровень»
	static constexpr int32 kLocStoneKind = 8584;                   // «Живой камень»
	static constexpr int32 kLocLevelRefused = 2030;                // отказ надевания по уровню (код 14)
	static constexpr int32 kCell = 0;                              // ячейка сумки и окна 50300
	static constexpr float kCaseSec = 5.4f;                        // длительность одного предмета

	// Проверяемая вещь: значения — цели target_B.json.
	struct FCase
	{
		int32 Code;    // код предмета
		int32 G0;      // OptionGroup0
		int32 G1;      // OptionGroup1
		int32 Limit;   // предел улучшения: запись +26 и pricetable
		int32 Max;     // pricetable.dw03, если отличается от предела сервера (0 — равен)
		int32 Level;   // уровень сервера для проверки подсказки и отказа (0 — не проверять)
		int32 Stone;   // камень в первом гнезде записи (0 — нет)
		int32 StoneLine;   // уровень камня, ожидаемый в подсказке (0 — строки быть не должно)
	};
	static const FCase kCases[] = { {73197, 5983, 5984, 8, 0, 0, 9968, 60}, {73236, 5983, 5984, 8}, {73305, 5933, 5934, 7, 0, 0, 9932, 0}, {73560, 5933, 5934, 7},
		{77190, 12920, 12921, 7}, {78105, 12920, 12921, 7}, {78162, 12920, 12921, 7}, {79189, 12720, 12721, 7}, {78076, 12912, 12913, 7},
		// ряды правки слотов (target_S.json): по вещи семейства
		{73371, 5933, 5934, 7}, {73453, 5953, 5954, 7}, {73515, 5933, 5934, 7}, {31428, 5983, 5984, 8}, {76210, 5933, 5934, 7}, {75602, 5933, 5934, 7},
		{75563, 5933, 5934, 7}, {76102, 5933, 5934, 7}, {75870, 12720, 12721, 7}, {75888, 12750, 12751, 8, 0, 60}, {71659, 6001, 6002, 8},
		// итоговая волна: похожие вещи и «только слот», предел сервера выше pricetable у Dutch, Veteran, Diablo's
		{76035, 12720, 12721, 8, 7, 40}, {73416, 5933, 5934, 7}, {73488, 5933, 5934, 7}, {73722, 5991, 5992, 8}, {75798, 5983, 5984, 8}, {75806, 5989, 5990, 8},
		{73355, 5907, 5908, 7}, {75990, 5933, 5934, 8, 7, 40}, {76156, 5933, 5934, 8, 7, 40}, {75963, 12734, 12735, 8, 7} };   // по вещи семейства: классы B и C

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                     // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                   // мир прогона
		bool bQuit = false;                         // выйти после итога
		TArray<uint8> SavedCell;                    // ячейка до прогона (пусто — была пустой)
		int32 SavedTab = 1;                         // вкладка до прогона
		FVector2D WorldPt = FVector2D::ZeroVector;  // точка мира без интерфейса
		FString FrameDir;                           // куда класть кадры

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaGearClassBTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaGearClassBTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд от старта.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Центр живого прямоугольника узла (false — не отрисован или скрыт).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Курсор в точку и событие движения.
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Весь видимый текст плашки подсказки.
	static FString TipText(const FCtxRef& C)
	{
		UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		UAsdaTipPanel* P = T ? T->GetMainPanel() : nullptr;
		UUserWidget* Wd = P ? P->GetWidget() : nullptr;
		if (!T || !T->IsTooltipShown() || !Wd || !Wd->WidgetTree) { return FString(); }
		TArray<UWidget*> All;
		Wd->WidgetTree->GetAllWidgets(All);
		FString Out;
		for (UWidget* X : All)
		{
			if (!X || !AsdaUi::IsShown(X)) { continue; }
			if (const UTextBlock* TB = Cast<UTextBlock>(X))              { Out += TB->GetText().ToString() + TEXT("\n"); }
			else if (const URichTextBlock* RB = Cast<URichTextBlock>(X)) { Out += RB->GetText().ToString() + TEXT("\n"); }
		}
		Out.ReplaceInline(TEXT("&amp;"), TEXT("&"));   // RichText хранит & сущностью
		return Out;
	}
	// Подсказка показала имя вещи и строку «Доступно улучшений: Limit/Limit».
	static void CheckTip(const FCtxRef& C, const FCase& K, const TCHAR* Where)
	{
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		const FString Txt = TipText(C);
		const FString Name = IT ? IT->GetItemName(K.Code) : FString();
		const FString Label = AsdaNotice::LocRaw(C->GI(), kLocUpgradesLeft, /*bDebugFallback*/false);   // без обрезки: EN-строка с пробелами в конце, подсказка печатает её как есть
		const FString Line = FString::Printf(TEXT("%s %d/%d"), *Label, K.Limit, K.Max ? K.Max : K.Limit);
		C->Expect(!Name.IsEmpty() && Txt.Contains(Name) && !Label.TrimStartAndEnd().IsEmpty() && Txt.Contains(Line), FString::Printf(
			TEXT("%s %d «%s»: в подсказке строка «%s»"), Where, K.Code, *Name, *Line));
		UE_LOG(LogAsdaGearClassBTest, Display, TEXT("%s текст подсказки %d:\n%s"), kPfx, K.Code, *Txt);
		{
			const FString Kind = AsdaNotice::LocRaw(C->GI(), kLocStoneKind, /*bDebugFallback*/false);
			const FString LevelWord = AsdaNotice::LocRaw(C->GI(), kLocLevelLabel, /*bDebugFallback*/false);
			const FString Stone = K.StoneLine > 0 ? FString::Printf(TEXT("%s - %s : %d"), *Kind, *LevelWord, K.StoneLine) : FString::Printf(TEXT("%s - %s :"), *Kind, *LevelWord);
			C->Expect(Txt.Contains(Stone) == (K.StoneLine > 0), FString::Printf(TEXT("%s %d: строка уровня камня «%s» %s"), Where, K.Code, *Stone, K.StoneLine > 0 ? TEXT("есть") : TEXT("отсутствует")));
		}
		if (K.Level > 0)
		{
			const FAsdaItemRecord* R = IT ? IT->GetItem(K.Code) : nullptr;
			const FString LevelText = FString::Printf(TEXT("%s : %d"), *AsdaNotice::LocRaw(C->GI(), kLocLevelLabel, /*bDebugFallback*/false), K.Level);
			C->Expect(R && R->RequiredLevel == K.Level && Txt.Contains(LevelText), FString::Printf(
				TEXT("%s %d: уровень таблицы %d, в подсказке «%s» (ждём уровень сервера %d)"), Where, K.Code, R ? R->RequiredLevel : -1, *LevelText, K.Level));
		}
	}
	// Запись вещи: предел +26 и опции из групп самой вещи.
	static TArray<uint8> Record(const FCtxRef& C, const FCase& K)
	{
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		FAsdaItemRecordWriter Wr;
		Wr.Code(K.Code).Container(1).Slot(kCell).Second(-1).Amount(1).Durability(100).Byte26(K.Limit);
		if (K.Stone > 0) { Wr.Sowel(0, K.Stone); }
		const int32 Groups[] = { K.G0, K.G1 };
		for (int32 J = 0; J < 2; ++J)
		{
			TArray<FAsdaItemoptionrangeRow> Rows;
			if (IT) { IT->GetItemOptionRangesByGroup(Groups[J], Rows); }
			if (Rows.Num() > 0) { Wr.EnchantPair(J, Rows[0].OptionType, Rows[0].MinValue); }
		}
		return Wr.ToArray();
	}
	// Положить запись в ячейку (пусто — очистить).
	static void Put(UAsdaInventorySubsystem* Inv, const TArray<uint8>& Rec)
	{
		Inv->ApplyExternalRecord(Rec.Num() == FAsdaItemRecordWriter::kSize ? Rec : FAsdaItemRecordWriter::ClearCell(1, kCell),
			UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	}
	// Снимок экрана.
	static void Shot(const FCtxRef& C, const FString& Tag)
	{
		const FString File = C->FrameDir / FString::Printf(TEXT("%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), *Tag);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogAsdaGearClassBTest, Display, TEXT("%s кадр заказан → %s"), kPfx, *File);
	}
	// Вернуть окна и ячейку, напечатать итог.
	static void Finish(const FCtxRef& C)
	{
		if (UAsdaItemOptionSubsystem* O = C->Sub<UAsdaItemOptionSubsystem>()) { O->CloseWindow(); }
		if (UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>()) { T->HideTooltip(); }
		if (UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>())
		{
			Put(I, C->SavedCell);
			I->SetTab(C->SavedTab);
		}
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaGearClassBTest, kPfx, C->Ok, C->Fail);
	}

	// Один предмет: сумка → подсказка → окно 50300 → подсказка → щелчок → панели опций.
	static void RunCase(const FCtxRef& C, const FCase K, float T0)
	{
		At(C, T0, [C, K]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			if (!I) { return; }
			Put(I, Record(C, K));
			if (!I->IsWindowOpen()) { I->OpenWindow(); }
			I->SetTab(1);
			Hover(C->WorldPt);
		});
		At(C, T0 + 0.3f, [C, K]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D P = FVector2D::ZeroVector;
			const bool bGeo = I && Center(I->DebugGetGridIcon(kCell), P);
			C->Expect(bGeo, FString::Printf(TEXT("A0 %d: ячейка сумки на экране (%.0f,%.0f)"), K.Code, P.X, P.Y));
			if (bGeo) { Hover(P); }
		});
		At(C, T0 + 0.8f, [C, K]()
		{
			CheckTip(C, K, TEXT("A1 сумка"));
			if (K.Level > 0)
			{
				// Отказ сервера кодом 14: тело 5001 подаётся штатным путём, как в asda.SelfTest.EquipJobNotice.
				UAsdaNetSubsystem* N = C->Sub<UAsdaNetSubsystem>();
				UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
				const UAsdaChatSubsystem* Ch = C->Sub<UAsdaChatSubsystem>();
				const FString Needle = AsdaNotice::LocRaw(C->GI(), kLocLevelRefused, /*bDebugFallback*/false).TrimStartAndEnd();
				const int32 Before = Ch ? Ch->DebugCountSystemLines(Needle) : -1;
				TArray<uint8> B;
				B.SetNumZeroed(24);
				B[0] = 14;
				AsdaBytes::WrI16(B.GetData() + 2, kCell);
				B[6] = 1;
				AsdaBytes::WrI16(B.GetData() + 13, 0);
				B[17] = 3;
				if (N) { N->DebugFeedPacket(5001, B); }
				if (I) { I->DebugPumpPendingPackets(); }
				const int32 After = Ch ? Ch->DebugCountSystemLines(Needle) : -1;
				C->Expect(After == Before + 1, FString::Printf(TEXT("A2 %d: отказ кодом 14 даёт строку 2030 (было %d, стало %d)"), K.Code, Before, After));
			}
			Shot(C, FString::Printf(TEXT("%d_bag_tip"), K.Code));
		});
		At(C, T0 + 1.2f, [C]()
		{
			Hover(C->WorldPt);
			if (UAsdaItemOptionSubsystem* O = C->Sub<UAsdaItemOptionSubsystem>()) { O->OpenWindow(); }
		});
		At(C, T0 + 1.6f, [C, K]()
		{
			UAsdaItemOptionSubsystem* O = C->Sub<UAsdaItemOptionSubsystem>();
			FVector2D P = FVector2D::ZeroVector;
			const bool bGeo = O && O->IsOpen() && Center(O->DebugCellIcon(kCell), P);
			C->Expect(bGeo, FString::Printf(TEXT("B0 %d: окно 50300 открыто, ячейка на экране (%.0f,%.0f)"), K.Code, P.X, P.Y));
			if (bGeo) { Hover(P); }
		});
		At(C, T0 + 2.1f, [C, K]()
		{
			CheckTip(C, K, TEXT("B1 окно 50300"));
			Shot(C, FString::Printf(TEXT("%d_opt_tip"), K.Code));
		});
		At(C, T0 + 2.5f, [C]()
		{
			if (!FSlateApplication::IsInitialized()) { return; }
			const FVector2D P = FSlateApplication::Get().GetCursorPos();
			FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, P, P, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState()));
		});
		At(C, T0 + 2.7f, [C]()
		{
			if (!FSlateApplication::IsInitialized()) { return; }
			const FVector2D P = FSlateApplication::Get().GetCursorPos();
			FSlateApplication::Get().ProcessMouseButtonUpEvent(FPointerEvent(0, P, P, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState()));
		});
		At(C, T0 + 2.9f, [C]() { Hover(C->WorldPt); });
		At(C, T0 + 3.3f, [C, K]()
		{
			UAsdaItemOptionSubsystem* O = C->Sub<UAsdaItemOptionSubsystem>();
			UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
			C->Expect(O && O->DebugSelectedCode() == K.Code, FString::Printf(TEXT("B2 %d: щелчок выбрал вещь (выбрано %d)"), K.Code, O ? O->DebugSelectedCode() : -1));
			const int32 Groups[] = { K.G0, K.G1 };
			for (int32 J = 0; O && IT && J < 2; ++J)
			{
				TArray<FAsdaItemoptionrangeRow> Want;
				IT->GetItemOptionRangesByGroup(Groups[J], Want);
				const FString First = O->DebugRangeText(J, 0);
				C->Expect(Want.Num() > 0 && O->DebugRangeRows(J) == Want.Num() && !First.IsEmpty(), FString::Printf(
					TEXT("B3 %d: панель %d, группа %d, строк %d (ждём %d), первая «%s»"), K.Code, J, Groups[J], O->DebugRangeRows(J), Want.Num(), *First));
			}
			Shot(C, FString::Printf(TEXT("%d_opt_panels"), K.Code));
		});
		At(C, T0 + 3.8f, [C]()
		{
			if (UAsdaItemOptionSubsystem* O = C->Sub<UAsdaItemOptionSubsystem>()) { O->CloseWindow(); }
		});
		// Надетая вещь: запись в контейнер 3, слот = тип вещи, окно инвентаря показывает её в ячейке снаряжения.
		At(C, T0 + 4.1f, [C, K]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
			const FAsdaItemRecord* R = IT ? IT->GetItem(K.Code) : nullptr;
			if (!I || !R) { return; }
			const int32 Slot = (int32)R->ItemType;
			FAsdaItemRecordWriter Wr;
			Wr.Code(K.Code).Container(3).Slot(Slot).Second(-1).Amount(1).Durability(100).Byte26(K.Limit);
			I->ApplyExternalRecord(Wr.ToArray(), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			if (!I->IsWindowOpen()) { I->OpenWindow(); }
			I->SetTab(1);
		});
		At(C, T0 + 4.6f, [C, K]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
			const FAsdaItemRecord* R = IT ? IT->GetItem(K.Code) : nullptr;
			const int32 Slot = R ? (int32)R->ItemType : -1;
			const FAsdaItemInstance* W = I ? I->GetCell(3, Slot) : nullptr;
			C->Expect(W && W->ItemCode == K.Code, FString::Printf(TEXT("C1 %d: вещь в слоте снаряжения %d"), K.Code, Slot));
			Shot(C, FString::Printf(TEXT("%d_worn"), K.Code));
		});
		At(C, T0 + 5.0f, [C, K]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
			const FAsdaItemRecord* R = IT ? IT->GetItem(K.Code) : nullptr;
			if (I && R) { I->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(3, (int32)R->ItemType), UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
		});
	}

	// Подготовка и расписание всех предметов.
	static void Run(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		if (!Inv || !WM || !C->Sub<UAsdaItemOptionSubsystem>() || !C->Sub<UAsdaItemTableSubsystem>())
		{
			C->Expect(false, TEXT("подсистемы есть"));
			Finish(C);
			return;
		}
		UE_LOG(LogAsdaGearClassBTest, Log, TEXT("%s старт, язык %s"), kPfx, FAsdaLang::Code(FAsdaLang::Current()));
		C->SavedTab = Inv->GetTab();
		const FAsdaItemInstance* It = Inv->GetCell(1, kCell);
		C->SavedCell = (It && !It->IsEmpty()) ? It->Raw : TArray<uint8>();
		C->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/gear_fix_20261107/frames"));
		IFileManager::Get().MakeDirectory(*C->FrameDir, /*Tree*/ true);
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetGameViewportWidget().IsValid())
		{
			const FGeometry G = GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();
			for (const FVector2D F : { FVector2D(0.5f, 0.22f), FVector2D(0.3f, 0.3f), FVector2D(0.7f, 0.3f), FVector2D(0.5f, 0.4f) })
			{
				const FVector2D P = G.LocalToAbsolute(G.GetLocalSize() * F);
				FName Zone;
				if (!WM->IsScreenPointOverAnyUiWindow(P, Zone)) { C->WorldPt = P; break; }
			}
		}
		// Indian 75798–75815: грейд клиента = грейд-классу сервера (ведомость 07.11).
		if (const UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>())
		{
			int32 Good = 0;
			for (int32 Code = 75798; Code <= 75815; ++Code)
			{
				const FAsdaItemRecord* R = IT->GetItem(Code);
				Good += (R && R->ItemGrade == 3) ? 1 : 0;
			}
			C->Expect(Good == 18, FString::Printf(TEXT("D1 Indian 75798–75815: грейд 3 у %d из 18"), Good));
		}
		float T = 8.f;   // надпись локации успевает погаснуть и не закрывает окно на кадрах
		for (const FCase& K : kCases) { RunCase(C, K, T); T += kCaseSec; }
		At(C, T + 0.3f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestGearClassB(
	TEXT("asda.SelfTest.GearClassB"),
	TEXT("Самотест класса B правки снаряжения: подсказка в сумке и в окне 50300, панели опций, кадры в Reports/gear_fix_20261107/frames. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaGearClassBTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
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
