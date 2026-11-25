// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ПОДСКАЗКА БУСТЕРА/НАБОРА В СУМКЕ» (01.11.2026, баг 11 владельца): `asda.SelfTest.BagBoosterTip [quit]`. [ИСПРАВЛЕНИЕ]
//   Баг: у «Reindeer Costume» (76321, бустер 11/66, категория 3) на вкладке «Снаряжение/Премиум» подсказка снаряжения —
//   «Эффект ЖК : 0 ~ 0», «Уровень возможностей», «Количество живых камней 0/0», «Доступно улучшений 0/0», «Прочность 0».
//   Лечение — `UAsdaItemTooltipSubsystem::ShowItemTooltip` (ключ `asda.ItemTip.BagNonGearSimple`): не-снаряжение (itemType 10/11)
//   с этой вкладки идёт в простой билдер (вес + описание), как с вкладки «Расходники». Разбор и список 8143 предметов —
//   `Reports/bug_booster_tooltip/affected.csv`. Путь игрока (закон `ui-check-game-path-geometry`): предметы кладутся
//   в ячейки сумки, наведение — настоящими событиями мыши Slate по живой геометрии ячеек, офлайн (-AsdaNoNet). Разделы:
//     B. бустер 76321 «Reindeer Costume» (группа «бустер 66/67», класс 1)  → простая подсказка: «Вес», описание, «Профессия»,
//        8503 «не передаётся после экипировки» (TooltipFlag200), премиум-подвал 109832; полей снаряжения нет;
//     M. бустер 76322 (тот же, класс 12) — «Профессия» есть, и текст ОТЛИЧАЕТСЯ от B (близнецов по классу видно);
//     N. бустер 76321 с байтом +50 = 1 (привязан) — 8401 «не передаётся» вместо 8503;
//     P. набор 76309 «Elite Reindeer Set» (группа «набор 200/201»)     → простая подсказка;
//     O. первый предмет «прочее не-снаряжение» (11, категория 3, подтип не 66/67/200/201) → то же;
//     G. оружие (тип 9) — контроль: полная подсказка снаряжения («Эффект ЖК» есть);
//     C. не-премиальный предмет 11 (категория 1) в ячейке сумки, т. е. полным путём, — контроль: состав прежний (полный);
//     S. бустер категории 1 на вкладке «Расходники» — контроль: простая подсказка, как была;
//     L. ключ 0 (как эталон) — бустер 76321 снова с полями снаряжения (печать «что было»).
//   Кадры — `Reports/bug_booster_tooltip/frames/<язык>_<раздел>.png`. Сумка, вкладка и ключ возвращаются в конце.
//   Итог: «БУСТЕР-ПОДСКАЗКА ИТОГ: ok=N fail=M». Карта мира (`--map world`), язык — `--lang ru|en`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaInventorySubsystem.h"
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
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBagBoosterTipTest, Log, All);

namespace AsdaBagBoosterTipTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.BagBoosterTip");   // имя команды
	static const TCHAR* kPfx = TEXT("БУСТЕР-ПОДСКАЗКА");              // префикс строк и итога
	static const TCHAR* kKey = TEXT("asda.ItemTip.BagNonGearSimple"); // ключ исправления (тест переключает его через AsdaCVarSave)

	// Нотисы строк, по которым видна ветка подсказки (те же id, что в `AsdaItemTooltipSubsystem.cpp`).
	static constexpr int32 kLocWeight       = 2295;    // «Вес» — есть в обеих ветках
	static constexpr int32 kLocStatEffect   = 2360;    // «Эффект ЖК» — только полный билдер снаряжения
	static constexpr int32 kLocSowelCount   = 2298;    // «Количество живых камней» — только полный
	static constexpr int32 kLocUpgradesLeft = 50024;   // «Доступно улучшений:» — только полный
	static constexpr int32 kLocClassLabel   = 2297;    // «Профессия»
	static constexpr int32 kLocNoTrade      = 8401;    // «не передаётся» (байт +50)
	static constexpr int32 kLocNoTradeEquip = 8503;    // «не передаётся после экипировки» (TooltipFlag200)
	static constexpr int32 kLocEliteFooter  = 109832;  // премиум-подвал

	// Один проверяемый предмет.
	struct FCase
	{
		FString Tag;            // раздел (B/P/O/G/S/L) — в имени кадра и в строках
		int32 Code = 0;         // код предмета
		int32 Container = 1;    // 1 — сумка «Снаряжение/Премиум», 2 — «Расходники»
		int32 Cell = 0;         // ячейка контейнера (= видимая ячейка сетки, страница 0)
		bool bExpectFull = false;   // ждём полный билдер снаряжения
		bool bCheckExtra = false;   // проверять строки r2: профессия, привязка, подвал
		int32 Bind50 = 0;           // байт +50 записи (привязка)
		int32 ExpectBind = 0;       // ждём нотис привязки 8401/8503 (0 — не проверять)
		FString DiffFrom;           // текст должен отличаться от раздела с этим тегом
	};

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                         // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                       // мир прогона
		bool bQuit = false;                             // выйти после итога
		TArray<FCase> Cases;                            // разделы по порядку
		TMap<int32, TArray<uint8>> SavedBag, SavedSpend;// ячейки до прогона (пусто — была пустой)
		int32 SavedTab = 1;                             // вкладка до прогона
		int32 SavedKey = 1;                             // значение ключа исправления до прогона
		TMap<FString, FString> Texts;                   // текст плашки по разделу
		FVector2D WorldPt = FVector2D::ZeroVector;      // точка мира без интерфейса
		FString FrameDir;                               // куда класть кадры

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaBagBoosterTipTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaBagBoosterTipTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд от старта.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Центр живого прямоугольника узла (false — не отрисован / скрыт).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Курсор в точку + событие движения (наведение без щелчка).
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Весь текст показанной плашки (заголовок + тело; у RichText — с тегами разметки).
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
			if (const UTextBlock* TB = Cast<UTextBlock>(X))        { Out += TB->GetText().ToString() + TEXT("\n"); }
			else if (const URichTextBlock* RB = Cast<URichTextBlock>(X)) { Out += RB->GetText().ToString() + TEXT("\n"); }
		}
		// RichText хранит спецсимволы сущностями (EN 8401 — в кавычках: `&quot;You cannot trade this item&quot;`) — раскодируем.
		Out.ReplaceInline(TEXT("&quot;"), TEXT("\""));
		Out.ReplaceInline(TEXT("&apos;"), TEXT("'"));
		Out.ReplaceInline(TEXT("&lt;"), TEXT("<"));
		Out.ReplaceInline(TEXT("&gt;"), TEXT(">"));
		Out.ReplaceInline(TEXT("&amp;"), TEXT("&"));
		return Out;
	}
	// Есть ли в тексте подпись нотиса (пустая подпись — «нет данных», считаем отсутствием).
	static bool Has(const FCtxRef& C, const FString& Text, int32 Loc)
	{
		const FString L = AsdaNotice::LocRaw(C->GI(), Loc, /*bDebugFallback*/false).TrimStartAndEnd();
		return !L.IsEmpty() && Text.Contains(L);
	}
	// Положить запись в ячейку (пусто — очистить).
	static void Put(UAsdaInventorySubsystem* Inv, int32 Cont, int32 Cell, const TArray<uint8>& Rec)
	{
		Inv->ApplyExternalRecord(Rec.Num() == FAsdaItemRecordWriter::kSize ? Rec : FAsdaItemRecordWriter::ClearCell(Cont, Cell),
			UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	}
	// Снимок экрана с плашкой.
	static void Shot(const FCtxRef& C, const FString& Tag)
	{
		const FString File = C->FrameDir / FString::Printf(TEXT("%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), *Tag);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogAsdaBagBoosterTipTest, Display, TEXT("%s кадр заказан → %s"), kPfx, *File);
	}

	static void Finish(const FCtxRef& C)
	{
		AsdaCVarSave::SetForTest(kKey, C->SavedKey);   // исходное значение ключа, а не 1
		if (UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>()) { T->HideTooltip(); }
		if (UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>())
		{
			for (const TPair<int32, TArray<uint8>>& P : C->SavedBag)   { Put(I, 1, P.Key, P.Value); }
			for (const TPair<int32, TArray<uint8>>& P : C->SavedSpend) { Put(I, 2, P.Key, P.Value); }
			I->SetTab(C->SavedTab);
		}
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaBagBoosterTipTest, kPfx, C->Ok, C->Fail);
	}

	// Один раздел: наведение «мир → ячейка» на разных кадрах, затем разбор текста и кадр.
	static void RunCase(const FCtxRef& C, int32 Idx, float T0)
	{
		At(C, T0, [C, Idx]()
		{
			const FCase& K = C->Cases[Idx];
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			if (I)
			{
				// Запись раздела — в его ячейку (B/N/L делят ячейку 0 с разным байтом +50).
				TArray<uint8> Rec = FAsdaItemRecordWriter::Cell(K.Code, K.Container, K.Cell, 1);
				if (Rec.IsValidIndex(50)) { Rec[50] = (uint8)K.Bind50; }
				Put(I, K.Container, K.Cell, Rec);
				if (!I->IsWindowOpen()) { I->OpenWindow(); }
				I->SetTab(K.Container == 1 ? 1 : 0);
			}
			Hover(C->WorldPt);
		});
		At(C, T0 + 0.25f, [C, Idx]()
		{
			const FCase& K = C->Cases[Idx];
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D P = FVector2D::ZeroVector;
			const bool bGeo = I && Center(I->DebugGetGridIcon(K.Cell), P);
			C->Expect(bGeo, FString::Printf(TEXT("%s0 ячейка %d контейнера %d на экране (%.0f,%.0f)"), *K.Tag, K.Cell, K.Container, P.X, P.Y));
			if (bGeo) { Hover(P); }
		});
		At(C, T0 + 0.7f, [C, Idx]()
		{
			const FCase& K = C->Cases[Idx];
			UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
			const FAsdaItemRecord* R = IT ? IT->GetItem(K.Code) : nullptr;
			const FString Txt = TipText(C);
			const bool bFullLines = Has(C, Txt, kLocStatEffect) || Has(C, Txt, kLocSowelCount) || Has(C, Txt, kLocUpgradesLeft);
			FAsdaItemTooltipRow Tip;
			const FString Desc = (IT && IT->GetItemTooltip(K.Code, Tip)) ? Tip.Description.Left(24) : FString();
			UE_LOG(LogAsdaBagBoosterTipTest, Display, TEXT("%s %s код %d «%s» (тип %d подтип %d кат %d) — текст плашки:\n%s"),
				kPfx, *K.Tag, K.Code, IT ? *IT->GetItemName(K.Code) : TEXT("?"), R ? R->ItemType : -1, R ? R->ItemSubType : -1,
				R ? R->CategoryFlag : -1, *Txt);
			if (K.bExpectFull)
			{
				C->Expect(!Txt.IsEmpty() && bFullLines,
					FString::Printf(TEXT("%s1 код %d — полная подсказка снаряжения (строки «Эффект ЖК/камни/улучшения» %s)"),
						*K.Tag, K.Code, bFullLines ? TEXT("есть") : TEXT("НЕТ")));
			}
			else
			{
				C->Expect(!Txt.IsEmpty() && !bFullLines && Has(C, Txt, kLocWeight) && (Desc.IsEmpty() || Txt.Contains(Desc)),
					FString::Printf(TEXT("%s1 код %d — простая подсказка: «Вес» %s, описание %s, строк снаряжения %s"),
						*K.Tag, K.Code, Has(C, Txt, kLocWeight) ? TEXT("есть") : TEXT("НЕТ"),
						(Desc.IsEmpty() || Txt.Contains(Desc)) ? TEXT("есть") : TEXT("НЕТ"), bFullLines ? TEXT("ЕСТЬ") : TEXT("нет")));
			}
			if (K.bCheckExtra)
			{
				const bool bProf = Has(C, Txt, kLocClassLabel);
				const bool bBind = (K.ExpectBind == 0) || Has(C, Txt, K.ExpectBind);
				const bool bWrongBind = (K.ExpectBind == kLocNoTrade) ? Has(C, Txt, kLocNoTradeEquip)
					: (K.ExpectBind == kLocNoTradeEquip) ? Has(C, Txt, kLocNoTrade) : false;
				const bool bFooter = Has(C, Txt, kLocEliteFooter);
				C->Expect(bProf && bBind && !bWrongBind && bFooter,
					FString::Printf(TEXT("%s2 код %d — «Профессия» %s, привязка %d %s, чужая привязка %s, премиум-подвал %s"),
						*K.Tag, K.Code, bProf ? TEXT("есть") : TEXT("НЕТ"), K.ExpectBind, bBind ? TEXT("есть") : TEXT("НЕТ"),
						bWrongBind ? TEXT("ЕСТЬ") : TEXT("нет"), bFooter ? TEXT("есть") : TEXT("НЕТ")));
			}
			if (!K.DiffFrom.IsEmpty())
			{
				const FString* Other = C->Texts.Find(K.DiffFrom);
				C->Expect(Other && !Other->IsEmpty() && *Other != Txt,
					FString::Printf(TEXT("%s3 код %d — текст отличается от раздела %s (близнецы по классу различимы)"), *K.Tag, K.Code, *K.DiffFrom));
			}
			C->Texts.Add(K.Tag, Txt);
			Shot(C, K.Tag);
		});
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		if (!Inv || !IT || !WM) { C->Expect(false, TEXT("подсистемы инвентаря/таблиц/окон есть")); Finish(C); return; }

		// Данные: из таблиц игры, по признакам группы (коды бага — первыми).
		int32 Booster = 76321, Twin = 76322, Package = 76309, Other = 0, Weapon = 0, SpendBooster = 0, Plain = 0;
		auto IsNonGearBag = [IT](int32 Code, int32 Sub)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			return R && R->ItemType == 11 && R->CategoryFlag == 3 && R->ItemSubType == Sub;
		};
		if (!IsNonGearBag(Booster, 66))  { Booster = 0; }
		if (!IsNonGearBag(Twin, 66))     { Twin = 0; }
		if (!IsNonGearBag(Package, 200)) { Package = 0; }
		for (int32 Code = 20000; Code < 80000; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (!R) { continue; }
			if (!Other && R->ItemType == 11 && R->CategoryFlag == 3 && R->ItemSubType != 66 && R->ItemSubType != 67
				&& R->ItemSubType != 200 && R->ItemSubType != 201 && !IT->IsSpecialItemCode(Code)) { Other = Code; }
			if (!Weapon && R->ItemType == 9) { Weapon = Code; }
			if (!SpendBooster && R->ItemType == 11 && R->ItemSubType == 66 && R->CategoryFlag == 1 && R->DurationOrEnchant > 0) { SpendBooster = Code; }
			if (!Plain && R->ItemType == 11 && R->CategoryFlag == 1 && R->TradableFlag == 0 && !IT->IsSpecialItemCode(Code)) { Plain = Code; }
		}
		const bool bData = Booster && Twin && Package && Other && Weapon && SpendBooster && Plain;
		C->Expect(bData, FString::Printf(TEXT("данные: бустер %d, близнец %d, набор %d, прочее %d, оружие %d, бустер расходников %d, ")
			TEXT("не-премиум 11 %d"), Booster, Twin, Package, Other, Weapon, SpendBooster, Plain));
		if (!bData) { Finish(C); return; }

		// Раздел: тег, код, контейнер, ячейка, ждём ли полный билдер.
		auto Mk = [](const TCHAR* Tag, int32 Code, int32 Cont, int32 Cell, bool bFull)
		{
			FCase K; K.Tag = Tag; K.Code = Code; K.Container = Cont; K.Cell = Cell; K.bExpectFull = bFull; return K;
		};
		C->Cases.Add(Mk(TEXT("B"), Booster, 1, 0, false));
		C->Cases.Last().bCheckExtra = true; C->Cases.Last().ExpectBind = kLocNoTradeEquip;
		C->Cases.Add(Mk(TEXT("M"), Twin, 1, 1, false));
		C->Cases.Last().bCheckExtra = true; C->Cases.Last().ExpectBind = kLocNoTradeEquip; C->Cases.Last().DiffFrom = TEXT("B");
		C->Cases.Add(Mk(TEXT("N"), Booster, 1, 0, false));
		C->Cases.Last().bCheckExtra = true; C->Cases.Last().Bind50 = 1; C->Cases.Last().ExpectBind = kLocNoTrade;
		C->Cases.Add(Mk(TEXT("P"), Package, 1, 2, false));
		C->Cases.Add(Mk(TEXT("O"), Other,   1, 3, false));
		C->Cases.Add(Mk(TEXT("G"), Weapon,  1, 4, true));
		C->Cases.Add(Mk(TEXT("C"), Plain,   1, 5, true));
		C->Cases.Add(Mk(TEXT("S"), SpendBooster, 2, 0, false));
		C->Cases.Add(Mk(TEXT("L"), Booster, 1, 0, true));   // ключ 0 — как эталон
		// Ячейки до прогона и ключ.
		C->SavedTab = Inv->GetTab();
		if (const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kKey)) { C->SavedKey = V->GetInt(); }
		for (int32 S = 0; S < 6; ++S) { const FAsdaItemInstance* It = Inv->GetCell(1, S); C->SavedBag.Add(S, (It && !It->IsEmpty()) ? It->Raw : TArray<uint8>()); }
		{ const FAsdaItemInstance* It = Inv->GetCell(2, 0); C->SavedSpend.Add(0, (It && !It->IsEmpty()) ? It->Raw : TArray<uint8>()); }

		C->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/bug_booster_tooltip/frames"));
		IFileManager::Get().MakeDirectory(*C->FrameDir, /*Tree*/ true);
		// Точка мира без интерфейса.
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
		Inv->OpenWindow();
		AsdaCVarSave::SetForTest(kKey, 1);

		float T = 0.6f;
		for (int32 i = 0; i < C->Cases.Num(); ++i)
		{
			if (C->Cases[i].Tag == TEXT("L")) { At(C, T - 0.1f, []() { AsdaCVarSave::SetForTest(kKey, 0); }); }
			RunCase(C, i, T);
			T += 1.4f;
		}
		At(C, T + 0.3f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestBagBoosterTip(
	TEXT("asda.SelfTest.BagBoosterTip"),
	TEXT("Самотест бага 11: подсказка бустера/набора/прочего не-снаряжения на вкладке «Снаряжение/Премиум» — простая (вес + описание), "
	     "профессия/привязка/подвал, близнецы по классу различимы, оружие и не-премиум 11 — полная, бустер «Расходников» — как был, ключ 0 — как эталон. Кадры в Reports/bug_booster_tooltip/frames. "
	     "Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaBagBoosterTipTest;
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
