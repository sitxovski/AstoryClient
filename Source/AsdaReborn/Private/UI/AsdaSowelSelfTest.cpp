// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ САМОЦВЕТОВ И ПРАВОЙ ИНФО-ПАНЕЛИ ИНВЕНТАРЯ (21.10.2026, баг-репорт игроков). [ПОРТ]
//   Отчёт: `Reports/sowel_bugfix_2026-10-21/REPORT.md`.
//
//   ЧТО ПРОВЕРЯЕТ — ПУТЁМ ИГРОКА (законы ui-check-game-path-geometry, diagnostic-must-use-real-path): клики, двойные
//   щелчки, перетаскивания и колесо подаются НАСТОЯЩИМИ событиями Slate (`UAsdaWindowSubsystem::SimulateClickAt/
//   SimulateDrag/HandleMouseWheel`) по ЖИВОЙ геометрии после отрисовки (`GetCachedGeometry`); отправки 5022/5024 —
//   БОЕВЫМИ отправителями под перехватом исходящих (`AsdaSelfTestKit::ArmCapture`, в сеть не уходит ничего).
//     A. Инфо-панель: выбор предмета кликом по ячейке сумки → панель раскрыта, привязана к ЖИВОМУ окну, имя предмета
//        настоящее, НИ ОДНОЙ видимой заглушки дизайнера («Опция 1…7»/«Option 1…7», «Доп. Соуэл…»/«Extra Sowel…»,
//        «Бла-бла…»/«Blah-blah…», «Бустер — …»/«Booster — …» и прочие надписи ассета).
//     B. Список камней: только совместимые (независимый оракул — байт-цель камня = тип предмета / «всем»),
//        2 страницы; полоса 495 собрана, видна, стоит у правого края ячеек; ↓/↑ полосы и колесо листают.
//     C. Перенос из ГНЕЗДА 0 в список: летит призрак, источник тусклый; уходит 5022 с гнездом 0 [ИСПРАВЛЕНИЕ запрета].
//     D. Двойной щелчок по гнезду 1 → 5022 с гнездом 1.  E. Двойной щелчок по камню списка → 5024 (гнездо 0, замена).
//     F. Перенос камня списка на гнездо → 5024.  G. Перенос из гнезда на ячейку сетки → 5022.
//     H. Пересоздание окна инвентаря после сброса сессии (как портал/перезаход): с ключом `asda.ItemTip.LegacyNoInfoRebind 1`
//        дефект воспроизводится (панель на СТАРОМ окне — в живом заглушки), без ключа — панель на живом окне, заглушек нет.
//   Состояние (сумка 0…29, расходники 0…59, окно, ключи, перехват) восстанавливается в конце.
//
// Консоль: `asda.SelfTest.Sowel [delay=N] [shot] [quit]` (`shot` — кадры в `Reports/sowel_bugfix_2026-10-21/shots`). Только офлайн (`-AsdaNoNet`), на карте мира.
//   Строки проверок — «САМОЦВЕТЫ-ТЕСТ ok/FAIL», итог — «САМОЦВЕТЫ-ТЕСТ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestSeq.h"   // ★ R5-fix-b 13.11.2026 (заключительный заход): шаги по событию «ввод доставлен», а не по часам
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "AsdaSessionReset.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaSowelSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaTipPanel.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"
#include "UnrealClient.h"
#include "Text/AsdaLang.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSowelTest, Log, All);

namespace AsdaSowelTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.Sowel");   // имя команды (и ключ реестра ключей консоли)

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		TSharedPtr<AsdaSelfTestSeq::FSeq> Seq;      // очередь шагов `At` (★ R5-fix-b 13.11.2026 (заключительный заход))
		bool bQuit = false;                        // выйти после итога
		bool bShot = false;                        // `shot` — снимать кадры в Reports/sowel_bugfix_2026-10-21/shots
		TArray<TArray<uint8>> SavedBag;            // сумка 0…59 до прогона (пусто — ячейка была пуста; ★ 29.10.2026 было 0…29)
		TArray<TArray<uint8>> SavedSpend;          // расходники 0…59 до прогона
		uint32 CapGen = 0;                         // поколение перехвата отправок
		TArray<TPair<int32, TArray<uint8>>> Sent;  // перехваченные отправки (опкод, тело)
		int32 ItemCode = 0, ItemType = -1;         // предмет-носитель (броня, 4 гнезда)
		TArray<int32> Good;                        // совместимые обычные камни (SpecialFlag 0) — в расходниках 50…57
		int32 Bad = 0;                             // несовместимый камень — в расходниках 58
		int32 Special = 0;                         // совместимый камень со SpecialFlag — в расходниках 59 и в гнезде 1
		int32 Sock0 = 0;                           // камень гнезда 0 (обычный)
		bool bGhostShot = false;                   // кадр переноса уже снят
		bool bOverCellDuringDrag = false;          // G: курсор был над занятой ячейкой сетки, пока камень «в руке»
		bool bCellTipDuringDrag = false;           // G: подсказка ячейки сумки всплыла под камнем «в руке» (дефект)
		int32 N5024Before = 0;                     // J: отправок 5024 до тройного щелчка
		float GhostSeenAlpha = -1.f;               // прозрачность источника, когда призрак был на экране (−1 — не видели)
		FVector2D GhostSeenAt = FVector2D::ZeroVector;   // где был призрак (абсолютные пиксели Slate)
		FVector2D CursorSeenAt = FVector2D::ZeroVector;  // где был курсор в тот кадр

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaSowelTest, Log,   TEXT("САМОЦВЕТЫ-ТЕСТ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaSowelTest, Error, TEXT("САМОЦВЕТЫ-ТЕСТ FAIL %s"), *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	static constexpr int32 kBagCell = 0;       // ячейка сумки с предметом-носителем
	static constexpr int32 kGoodFirst = 50;    // расходники 50…57 — совместимые обычные камни
	static constexpr int32 kBadCell = 58;      // расходники 58 — несовместимый
	static constexpr int32 kSpecialCell = 59;  // расходники 59 — совместимый со SpecialFlag
	static constexpr int32 kFillerCell = 5;    // сумка 5 — занятая ячейка-цель переноса G

	// Шаг через Sec секунд.
	// ★ R5-fix-b 13.11.2026 (заключительный заход): расписание — общая очередь `AsdaSelfTestSeq::At` (запись та же): шаг ждёт доставки заказанного
	//   клика/перетаскивания и тот же промежуток после предыдущего шага. Под нагрузкой (три игры, `rx_r5fix3`) мировой таймер
	//   проверял до отпускания перетаскивания (30 кадров удержания), следующий заказ затирал недоставленный — каскад провалов.
	using AsdaSelfTestSeq::At;

	// Центр живой геометрии узла (false — не отрисован / скрыт / нулевой размер).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия отличалась только тем, что `Mn`/`Mx` объявлялись без начального значения (на результат не влияет).

	// Двойной щелчок НАСТОЯЩИМИ событиями Slate: второй — как только первый доставлен (`IsSimClickPending`).
	static void DoubleClick(UAsdaWindowSubsystem* WM, const FVector2D& P)
	{
		if (!WM) { return; }
		WM->SimulateClickAt(P, /*HoldFrames*/ 1);
		TWeakObjectPtr<UAsdaWindowSubsystem> WW(WM);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, P](float) -> bool
		{
			UAsdaWindowSubsystem* W2 = WW.Get();
			if (!W2) { return false; }
			if (W2->IsSimClickPending()) { return true; }
			W2->SimulateClickAt(P, /*HoldFrames*/ 1);
			return false;
		}), 0.f);
	}

	// Эффективно видим ли виджет на экране: он и все его родители не свёрнуты/не скрыты, геометрия ненулевая.
	static bool OnScreen(const UWidget* Wd)
	{
		for (const UWidget* P = Wd; P; P = P->GetParent())
		{
			const ESlateVisibility V = P->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
		}
		const FGeometry G = Wd->GetCachedGeometry();
		const FVector2D Sz = G.GetLocalSize() * G.Scale;
		return Sz.X > 1.f && Sz.Y > 1.f;
	}

	// ★ Заглушки дизайнера в надписях `WBP_INVEN_WIN` (RU — исходник ассета, EN — его перевод, ведомость
	//   `RawAssets/Text/L10N/en/_sources_UiLiteral.tsv`). Подстроки выбраны так, чтобы не совпадать с настоящими строками.
	static const TCHAR* const kPlaceholders[] = {
		TEXT("Опция 1"), TEXT("Опция 2"), TEXT("Опция 3"), TEXT("Опция 4"), TEXT("Опция 5"), TEXT("Опция 6"), TEXT("Опция 7"),
		TEXT("Доп. Соуэл"), TEXT("Опц. Соуэл"), TEXT("Бла-бла"), TEXT("Бустер — Стальной"), TEXT("Вес: 999"), TEXT("Уровень предмета: 45"),
		TEXT("Прочность: 990"), TEXT("Усиление: 100 ~ 100"), TEXT("Ранг: Артефакт"), TEXT("Число заточек: 3/5"), TEXT("Уровень ношения: 30"),
		TEXT("Прочность: 70/100"), TEXT("Диапазон уровней: 99~99"), TEXT("Класс: элементалист"), TEXT("Уровень заточки: +5"),
		TEXT("Заточка: +38%"), TEXT("Усиление Соуэла: высокое"), TEXT("Физ. атака: 50+120"), TEXT("Атака Соуэла: 555"),
		TEXT("Опыт персонажа +35"), TEXT("Голд персонажа +10"), TEXT("Бесконечная прочность"),
		TEXT("Option 1"), TEXT("Option 2"), TEXT("Option 3"), TEXT("Option 4"), TEXT("Option 5"), TEXT("Option 6"), TEXT("Option 7"),
		TEXT("Extra Sowel"), TEXT("Option Sowel"), TEXT("Blah-blah"), TEXT("Booster — Fierce"), TEXT("Weight: 999"), TEXT("Item level: 45"),
		TEXT("Durability: 990"), TEXT("Enhancement: 100 ~ 100"), TEXT("Grade: Artifact"), TEXT("Upgrades: 3/5"), TEXT("Wear level: 30"),
		TEXT("Durability: 70/100"), TEXT("Level range: Lv. 99~99"), TEXT("Job: Elementalist"), TEXT("Upgrade level: +5"),
		TEXT("Upgrade: +38%"), TEXT("Sowel enhancement: high"), TEXT("Phys. attack: 50+120"), TEXT("Sowel attack: 555"),
		TEXT("Character EXP +35"), TEXT("Character Gold +10"), TEXT("Infinite durability"),
	};

	// Видимые на экране надписи окна, содержащие заглушку. Возврат — «имя: текст» каждой.
	static TArray<FString> VisiblePlaceholders(const UUserWidget* Win)
	{
		TArray<FString> Out;
		if (!Win || !Win->WidgetTree) { return Out; }
		Win->WidgetTree->ForEachWidget([&Out](UWidget* Wd)
		{
			FString T;
			if (const UTextBlock* Tb = Cast<UTextBlock>(Wd)) { T = Tb->GetText().ToString(); }
			else if (const URichTextBlock* Rb = Cast<URichTextBlock>(Wd)) { T = Rb->GetText().ToString(); }
			else { return; }
			if (T.IsEmpty() || !OnScreen(Wd)) { return; }
			for (const TCHAR* P : kPlaceholders)
			{
				if (T.Contains(P)) { Out.Add(FString::Printf(TEXT("%s: «%s»"), *Wd->GetName(), *T.Left(48))); break; }
			}
		});
		return Out;
	}

	// Текст надписи окна по имени (TextBlock или подменивший его rich).
	static FString TextOf(const UUserWidget* Win, const TCHAR* Name, bool& bOnScreen)
	{
		bOnScreen = false;
		const UWidget* Wd = Win ? Win->GetWidgetFromName(FName(Name)) : nullptr;
		if (!Wd) { return FString(); }
		bOnScreen = OnScreen(Wd);
		if (const UTextBlock* Tb = Cast<UTextBlock>(Wd)) { return Tb->GetText().ToString(); }
		if (const URichTextBlock* Rb = Cast<URichTextBlock>(Wd)) { return Rb->GetText().ToString(); }
		return FString();
	}

	// Кадр экрана (с интерфейсом) в папку отчёта: <язык>_<имя>.png.
	static void Shot(const FCtxRef& C, const TCHAR* Name)
	{
		if (!C->bShot) { return; }
		const FString File = FString::Printf(TEXT("@@ASTORY_DEV_ROOT_FWD@@/Reports/sowel_bugfix_2026-10-21/shots/%s_%s.png"),
			FAsdaLang::Code(FAsdaLang::Current()), Name);
		UE_LOG(LogAsdaSowelTest, Log, TEXT("САМОЦВЕТЫ-ТЕСТ кадр → %s"), *File);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}

	// Последняя перехваченная отправка опкода (тело) и их число.
	static int32 LastSent(const FCtxRef& C, int32 Op, TArray<uint8>* OutBody)
	{
		int32 N = 0;
		for (const TPair<int32, TArray<uint8>>& P : C->Sent) { if (P.Key == Op) { ++N; if (OutBody) { *OutBody = P.Value; } } }
		return N;
	}

	// Подобрать предмет и камни по ДАННЫМ (оракул независим от боевой проверки совместимости: байт-цель камня =
	//   тип предмета, остальные байты-цели — 15 «нет цели»; несовместимый — байт-цель другого типа брони).
	static bool PickData(const FCtxRef& C)
	{
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		if (!IT) { return false; }
		for (int32 Code = 20551; Code <= 79428 && C->ItemCode == 0; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (!R || R->ItemType < 0 || R->ItemType > 4 || R->EnchantOrSocket != 3 || R->DurationOrEnchant != 0 || R->CategoryFlag == 3) { continue; }
			C->ItemCode = Code; C->ItemType = R->ItemType;
		}
		if (C->ItemCode == 0) { return false; }
		const int32 OtherType = (C->ItemType + 1) % 5;
		for (int32 Code = 7001; Code <= 10925; ++Code)
		{
			FAsdaSowellRow Row;
			if (!IT->GetSowell(Code, Row)) { continue; }
			const int32 B47 = AsdaBytes::HexByteAt(Row.DataHex, 47);
			if (Row.SocketTargetType2 != 15 || B47 != 15) { continue; }
			if (Row.SocketTargetType == C->ItemType && Row.SpecialFlag == 0 && C->Good.Num() < 8) { C->Good.Add(Code); }
			else if (Row.SocketTargetType == C->ItemType && Row.SpecialFlag != 0 && C->Special == 0) { C->Special = Code; }
			else if (Row.SocketTargetType == OtherType && Row.SpecialFlag == 0 && C->Bad == 0) { C->Bad = Code; }
		}
		if (C->Good.Num() < 8 || !C->Special || !C->Bad) { return false; }
		C->Sock0 = C->Good[0];
		return true;
	}

	static void PutRec(const FCtxRef& C, int32 Cont, int32 Cell, const TArray<uint8>& R)
	{
		if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>()) { Inv->ApplyExternalRecord(R, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
		(void)Cont; (void)Cell;
	}

	// Предмет-носитель: гнездо 0 — обычный камень, гнездо 1 — камень со SpecialFlag, 2/3 пусты.
	static void PutTarget(const FCtxRef& C)
	{
		FAsdaItemRecordWriter Rw;
		Rw.Code(C->ItemCode).Container(1).Slot(kBagCell).Second(-1).Amount(1).Durability(33).StackWeight(10)
		  .Sowel(0, C->Sock0).Sowel(1, C->Special).Byte26(5).Byte25(2);
		PutRec(C, 1, kBagCell, Rw.ToArray());
	}

	// Ячейка сумки kBagCell в сетке: план `INVEN_SET0_…` (вкладка «Снаряжение», страница 0).
	static UWidget* GridCell(const FCtxRef& C, int32 Col)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UUserWidget* Win = Inv ? Inv->GetWindowWidget() : nullptr;
		return Win ? AsdaUi::FindNodeByName(Win, TEXT(""), *FString::Printf(TEXT("INVEN_SET%d_"), Col)) : nullptr;
	}

	// Проверки инфо-панели (A и H): привязка к живому окну, имя, заглушки.
	static void CheckPanel(const FCtxRef& C, const TCHAR* Tag, bool bExpectClean)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaItemTooltipSubsystem* Tip = C->Sub<UAsdaItemTooltipSubsystem>();
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		UUserWidget* Live = Inv ? Inv->GetWindowWidget() : nullptr;
		const bool bBound = Tip && Live && Tip->GetInfoPanelWindow() == Live;
		const TArray<FString> Ph = VisiblePlaceholders(Live);
		bool bNameOn = false;
		const FString Name = TextOf(Live, TEXT("INVEN_ITEMNAME"), bNameOn);
		const FString Want = IT ? IT->GetItemName(C->ItemCode) : FString();
		if (bExpectClean)
		{
			C->Expect(Inv && Inv->IsPanelExpanded(), FString::Printf(TEXT("%s панель раскрыта выбором предмета"), Tag));
			C->Expect(bBound, FString::Printf(TEXT("%s инфо-панель привязана к ЖИВОМУ окну инвентаря (%s)"), Tag, Live ? *Live->GetName() : TEXT("нет")));
			C->Expect(bNameOn && !Want.IsEmpty() && Name.Contains(Want),
				FString::Printf(TEXT("%s имя предмета на экране настоящее: «%s» (ждали «%s»)"), Tag, *Name.Left(60), *Want));
			C->Expect(Ph.Num() == 0, FString::Printf(TEXT("%s видимых заглушек дизайнера нет (%d: %s)"), Tag, Ph.Num(), *FString::Join(Ph, TEXT("; "))));
			// Пустых плашек/полос нет: группа контрола на экране ⇒ его текст на экране и не пуст (эталон гасит контрол
			//   `CControl_SetVisible(…, 0)` вместе с планами — плашки 419/420/421/479, полоса «Премиум-опция» 477).
			static const TCHAR* const kPairs[][2] = {
				{ TEXT("INVEN_SOLLTXT01_grp"), TEXT("INVEN_SOLLTXT01") }, { TEXT("INVEN_SOLLTXT02_grp"), TEXT("INVEN_SOLLTXT02") },
				{ TEXT("INVEN_SOLLTXT03_grp"), TEXT("INVEN_SOLLTXT03") }, { TEXT("INVEN_SOLLTXT04_grp"), TEXT("INVEN_SOLLTXT04_1") },
				{ TEXT("INVEN_Primium_Option_grp"), TEXT("INVEN_Primium_Option") },
			};
			TArray<FString> Empty;
			int32 Shown = 0;
			for (const auto& Pr : kPairs)
			{
				const UWidget* G = Live ? Live->GetWidgetFromName(FName(Pr[0])) : nullptr;
				if (!G || !OnScreen(G)) { continue; }
				++Shown;
				bool bOn = false;
				const FString T = TextOf(Live, Pr[1], bOn);
				if (!bOn || T.TrimStartAndEnd().IsEmpty()) { Empty.Add(Pr[0]); }
			}
			C->Expect(Empty.Num() == 0, FString::Printf(TEXT("%s пустых плашек/полос на экране нет (видимых групп %d, пустых: %s)"),
				Tag, Shown, Empty.Num() ? *FString::Join(Empty, TEXT(", ")) : TEXT("—")));
		}
		else
		{
			// Контроль «до исправления»: панель осталась на старом окне, в живом — надписи ассета.
			C->Expect(!bBound && Ph.Num() > 0,
				FString::Printf(TEXT("%s КОНТРОЛЬ (ключ LegacyNoInfoRebind 1): дефект воспроизведён — панель не на живом окне, заглушек на экране %d (%s)"),
					Tag, Ph.Num(), Ph.Num() ? *Ph[0] : TEXT("—")));
		}
	}

	static void Finish(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>()) { S->DebugClearAwaiting(); }
		if (Net && C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); C->CapGen = 0; }
		if (Inv && C->SavedBag.Num() == UAsdaInventorySubsystem::kShellSlots && C->SavedSpend.Num() == 60)   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
		{
			for (int32 I = 0; I < UAsdaInventorySubsystem::kShellSlots; ++I) { if (C->SavedBag[I].Num() >= 57) { Inv->ApplyExternalRecord(C->SavedBag[I], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); } else { Inv->ClearCell(1, I); } }
			for (int32 I = 0; I < 60; ++I) { if (C->SavedSpend[I].Num() >= 57) { Inv->ApplyExternalRecord(C->SavedSpend[I], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); } else { Inv->ClearCell(2, I); } }
		}
		if (Inv && Inv->IsWindowOpen()) { Inv->CloseWindow(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaSowelTest, TEXT("САМОЦВЕТЫ-ТЕСТ"), C->Ok, C->Fail);
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!Inv || !S || !WM || !Net || !C->Sub<UAsdaItemTooltipSubsystem>())
		{
			C->Expect(false, TEXT("подсистемы инвентаря/самоцветов/окон/сети/подсказок есть"));
			Finish(C);
			return;
		}
		if (!PickData(C))
		{
			C->Expect(false, TEXT("данные: броня с 4 гнёздами, 8 совместимых камней, камень со SpecialFlag, несовместимый"));
			Finish(C);
			return;
		}
		C->Expect(true, FString::Printf(TEXT("данные: предмет %d (тип %d), камни %d…, особый %d, несовместимый %d"),
			C->ItemCode, C->ItemType, C->Good[0], C->Special, C->Bad));
		// Перехват ВСЕХ отправок — боевые отправители 5022/5024 доходят до него, в сеть не уходит ничего.
		TWeakPtr<FCtx> WC(C);
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WC](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> X = WC.Pin()) { TArray<uint8> B; if (P && Len > 0) { B.Append(P, Len); } X->Sent.Emplace((int32)Op, MoveTemp(B)); }
		}, 0, kCmd);
		if (!C->CapGen) { C->Expect(false, TEXT("перехват отправок свободен")); Finish(C); return; }

		// Снимок сумки и расходников, затем свои записи.
		for (int32 I = 0; I < UAsdaInventorySubsystem::kShellSlots; ++I) { const FAsdaItemInstance* X = Inv->GetCell(1, I); C->SavedBag.Add(X && !X->IsEmpty() ? X->Raw : TArray<uint8>()); }
		for (int32 I = 0; I < 60; ++I) { const FAsdaItemInstance* X = Inv->GetCell(2, I); C->SavedSpend.Add(X && !X->IsEmpty() ? X->Raw : TArray<uint8>()); }
		for (int32 I = 0; I < 60; ++I) { Inv->ClearCell(2, I); }
		PutTarget(C);
		// Ячейка сумки 5 — занята (цель переноса G: под камнем «в руке» её подсказка всплывать не должна).
		PutRec(C, 1, kFillerCell, FAsdaItemRecordWriter::Cell(C->ItemCode, 1, kFillerCell, 1));
		for (int32 i = 0; i < 8; ++i) { PutRec(C, 2, kGoodFirst + i, FAsdaItemRecordWriter::Cell(C->Good[i], 2, kGoodFirst + i, 1)); }
		PutRec(C, 2, kBadCell, FAsdaItemRecordWriter::Cell(C->Bad, 2, kBadCell, 1));
		PutRec(C, 2, kSpecialCell, FAsdaItemRecordWriter::Cell(C->Special, 2, kSpecialCell, 1));
		if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
		Inv->SetTab(1);

		// ═══ A. Выбор предмета НАСТОЯЩИМ кликом по ячейке сумки → инфо-панель ═══
		At(C, 0.8f, [C]()
		{
			FVector2D P;
			const bool bGeo = Center(GridCell(C, kBagCell), P);
			C->Expect(bGeo, TEXT("A0 ячейка сумки 0 отрисована (вкладка «Снаряжение»)"));
			if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, 2); }
		});
		At(C, 2.0f, [C]()
		{
			CheckPanel(C, TEXT("A"), /*bExpectClean*/ true);
			Shot(C, TEXT("A_panel_list"));
			// ═══ B. Список и полоса ═══
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			TSet<int32> Codes;
			for (const FAsdaSowelListEntry& E : S->GetList()) { Codes.Add(E.Code); }
			bool bAllGood = true;
			for (int32 G : C->Good) { bAllGood &= Codes.Contains(G); }
			C->Expect(bAllGood && Codes.Contains(C->Special) && !Codes.Contains(C->Bad) && Codes.Num() == 9,
				FString::Printf(TEXT("B1 фильтр: в списке 9 совместимых (8 обычных + особый), несовместимого %d нет (в списке %d)"), C->Bad, Codes.Num()));
			C->Expect(S->GetListPageCount() == 2 && S->GetListPage() == 0, FString::Printf(TEXT("B2 страниц 2, текущая 0 (%d/%d)"), S->GetListPage(), S->GetListPageCount()));
			const FAsdaScrollBarNomal& Bar = S->DebugListBar();
			FVector2D Up = FVector2D::ZeroVector, Dn = FVector2D::ZeroVector, Cell2 = FVector2D::ZeroVector;
			const bool bBar = Bar.IsBuilt() && Center(Bar.Up, Up) && Center(Bar.Down, Dn) && Center(S->DebugTrayIcon(2), Cell2);
			C->Expect(bBar && Up.X > Cell2.X && Dn.Y > Up.Y && AsdaUi::IsShown(Bar.Thumb),
				FString::Printf(TEXT("B3 полоса 495 на экране справа от ячеек: ↑(%.0f,%.0f) ↓(%.0f,%.0f), ячейка 432 x=%.0f, бегунок %s"),
					Up.X, Up.Y, Dn.X, Dn.Y, Cell2.X, AsdaUi::IsShown(Bar.Thumb) ? TEXT("виден") : TEXT("скрыт")));
			if (bBar) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(Dn, 2); }
		});
		At(C, 2.8f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			C->Expect(S->GetListPage() == 1, FString::Printf(TEXT("B4 стрелка ↓ полосы (клик Slate) — страница 1 (%d)"), S->GetListPage()));
			FVector2D Up;
			if (Center(S->DebugListBar().Up, Up)) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(Up, 2); }
		});
		At(C, 3.6f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			C->Expect(S->GetListPage() == 0, FString::Printf(TEXT("B5 стрелка ↑ полосы — страница 0 (%d)"), S->GetListPage()));
			// Колесо: курсор над списком, общий диспетчер колеса окон (тот же, что зовёт препроцессор ввода).
			//   Точку берём там, где хит-тест окон отдаёт САМ инвентарь: при 1024×768 его правую панель частично накрывает
			//   окно системного чата (z выше) — над ним колесо по праву достаётся чату.
			UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			FVector2D P = FVector2D::ZeroVector;
			bool bPt = false;
			FString Cover;
			for (int32 Col = 0; Col < 6 && !bPt; ++Col)
			{
				FVector2D Q;
				if (!Center(S->DebugTrayIcon(Col), Q)) { continue; }
				const FName Top = WM->FindWindowCatchingAt(Q);
				Cover += FString::Printf(TEXT(" %d:%s"), Col, *Top.ToString());
				if (Top == FName(TEXT("INVEN_WIN"))) { P = Q; bPt = true; }
			}
			C->Expect(true, FString::Printf(TEXT("B6a хит-тест окон над ячейками списка:%s"), *Cover));
			if (bPt)
			{
				FSlateApplication::Get().SetCursorPos(P);
				const bool bEaten = WM->HandleMouseWheel(-1);
				C->Expect(bEaten && S->GetListPage() == 1, FString::Printf(TEXT("B6 колесо вниз над списком — страница 1 (%d, съедено %s)"), S->GetListPage(), bEaten ? TEXT("да") : TEXT("нет")));
				WM->HandleMouseWheel(+1);
				C->Expect(S->GetListPage() == 0, FString::Printf(TEXT("B7 колесо вверх — страница 0 (%d)"), S->GetListPage()));
			}
			else
			{
				C->Expect(false, TEXT("B6 нет ячейки списка, которую хит-тест отдаёт инвентарю — колесо не проверено"));
			}
		});

		// ═══ C. Перенос ГЛАВНОГО камня (гнездо 0) в список ═══
		At(C, 4.2f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			FVector2D From = FVector2D::ZeroVector, To = FVector2D::ZeroVector;
			const bool bGeo = Center(S->DebugSocketIcon(0), From) && Center(S->DebugTrayIcon(4), To);
			C->Expect(bGeo, TEXT("C0 гнездо 0 и ячейка списка 434 отрисованы"));
			if (!bGeo) { return; }
			UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			WM->SimulateDrag(From, To, /*HoldFrames*/ 40);
			// Пока перенос идёт — каждый кадр смотрим на призрак (кадров в секунду в прогоне заранее не знаем).
			TWeakPtr<FCtx> WC(C);
			TWeakObjectPtr<UAsdaWindowSubsystem> WW(WM);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WC, WW](float) -> bool
			{
				TSharedPtr<FCtx> X = WC.Pin();
				if (!X || !WW.IsValid()) { return false; }
				UAsdaSowelSubsystem* S = X->Sub<UAsdaSowelSubsystem>();
				if (S && S->DebugIsDragging())
				{
					const UImage* G = S->DebugDragGhost();
					const UImage* Src = S->DebugDragSource();
					if (G && OnScreen(G) && Src && Src->GetColorAndOpacity().A < 0.99f)
					{
						X->GhostSeenAlpha = Src->GetColorAndOpacity().A;
						X->GhostSeenAt = G->GetCachedGeometry().GetAbsolutePosition();
						X->CursorSeenAt = FSlateApplication::Get().GetCursorPos();
						if (!X->bGhostShot) { X->bGhostShot = true; Shot(X.ToSharedRef(), TEXT("C_drag_ghost")); }
					}
				}
				return WW->IsSimClickPending();
			}), 0.f);
		});
		At(C, 6.0f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			C->Expect(C->GhostSeenAlpha >= 0.f && FVector2D::Distance(C->GhostSeenAt, C->CursorSeenAt) < 64.f,
				FString::Printf(TEXT("C1 во время переноса: призрак камня на экране у курсора (призрак %s, курсор %s), источник притушен (α %.2f)"),
					*C->GhostSeenAt.ToString(), *C->CursorSeenAt.ToString(), C->GhostSeenAlpha));
			TArray<uint8> B;
			const int32 N = LastSent(C, 5022, &B);
			C->Expect(N == 1 && B.Num() >= 4 && B[0] == 1 && AsdaBytes::RdI16(B.GetData() + 1) == kBagCell && B[3] == 0,
				FString::Printf(TEXT("C2 отпускание над списком — 5022 «вынуть» с ГНЕЗДОМ 0 [ИСПРАВЛЕНИЕ запрета] (отправок %d, тело %s)"),
					N, *FString::FromHexBlob(B.GetData(), B.Num())));
			C->Expect(!S->DebugIsDragging() && (!S->DebugDragSource()), TEXT("C3 после отпускания призрака нет, источник ярок"));
			S->DebugClearAwaiting();
		});

		// ═══ D. Двойной щелчок по гнезду 1 ═══
		At(C, 6.4f, [C]()
		{
			FVector2D P;
			if (Center(C->Sub<UAsdaSowelSubsystem>()->DebugSocketIcon(1), P)) { DoubleClick(C->Sub<UAsdaWindowSubsystem>(), P); }
			else { C->Expect(false, TEXT("D0 гнездо 1 отрисовано")); }
		});
		At(C, 7.6f, [C]()
		{
			TArray<uint8> B;
			const int32 N = LastSent(C, 5022, &B);
			C->Expect(N == 2 && B.Num() >= 4 && B[3] == 1, FString::Printf(TEXT("D1 двойной щелчок по гнезду 1 — 5022 с гнездом 1 (отправок 5022: %d, гнездо %d)"), N, B.Num() >= 4 ? B[3] : -1));
			C->Sub<UAsdaSowelSubsystem>()->DebugClearAwaiting();
		});

		// ═══ E. Двойной щелчок по камню списка → замена главного ═══
		At(C, 8.2f, [C]()
		{
			FVector2D P;
			if (Center(C->Sub<UAsdaSowelSubsystem>()->DebugTrayIcon(1), P)) { DoubleClick(C->Sub<UAsdaWindowSubsystem>(), P); }
			else { C->Expect(false, TEXT("E0 ячейка списка 431 отрисована")); }
		});
		At(C, 9.4f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			TArray<uint8> B;
			const int32 N = LastSent(C, 5024, &B);
			const int32 Want = S->GetList().IsValidIndex(1) ? S->GetList()[1].Code : -1;
			const int32 WantCell = S->GetList().IsValidIndex(1) ? S->GetList()[1].SpendCell : -1;
			C->Expect(N == 1 && B.Num() >= 11 && B[0] == 1 && B[3] == 0 && AsdaBytes::RdI16(B.GetData() + 4) == C->Sock0
				&& AsdaBytes::RdI16(B.GetData() + 6) == Want && B[8] == 2 && AsdaBytes::RdI16(B.GetData() + 9) == WantCell,
				FString::Printf(TEXT("E1 двойной щелчок по камню списка — 5024: гнездо 0, старый %d, новый %d из ячейки 2:%d (тело %s)"),
					C->Sock0, Want, WantCell, *FString::FromHexBlob(B.GetData(), B.Num())));
			S->DebugClearAwaiting();
		});

		// ═══ F. Перенос камня из списка на гнездо ═══
		At(C, 10.0f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			FVector2D From = FVector2D::ZeroVector, To = FVector2D::ZeroVector;
			if (Center(S->DebugTrayIcon(2), From) && Center(S->DebugSocketIcon(3), To)) { C->Sub<UAsdaWindowSubsystem>()->SimulateDrag(From, To, 24); }
			else { C->Expect(false, TEXT("F0 ячейка списка 432 и гнездо 3 отрисованы")); }
		});
		At(C, 11.6f, [C]()
		{
			TArray<uint8> B;
			const int32 N = LastSent(C, 5024, &B);
			C->Expect(N == 2 && B.Num() >= 11 && B[3] == 0, FString::Printf(TEXT("F1 перенос камня списка на гнездо — 5024 (гнездо по правилу: 0) (отправок 5024: %d)"), N));
			C->Sub<UAsdaSowelSubsystem>()->DebugClearAwaiting();
		});

		// ═══ G. Перенос из гнезда 1 на ячейку сетки ═══
		At(C, 12.2f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			FVector2D From = FVector2D::ZeroVector, To = FVector2D::ZeroVector;
			if (!Center(S->DebugSocketIcon(1), From) || !Center(GridCell(C, kFillerCell), To)) { C->Expect(false, TEXT("G0 гнездо 1 и ячейка сетки 5 отрисованы")); return; }
			UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			WM->SimulateDrag(From, To, 40);
			// Каждый кадр переноса: курсор над ЗАНЯТОЙ ячейкой 5 — всплыла ли подсказка ячейки (владелец `Item`)?
			TWeakPtr<FCtx> WC(C);
			TWeakObjectPtr<UAsdaWindowSubsystem> WW(WM);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WC, WW](float) -> bool
			{
				TSharedPtr<FCtx> X = WC.Pin();
				if (!X || !WW.IsValid()) { return false; }
				UAsdaSowelSubsystem* S2 = X->Sub<UAsdaSowelSubsystem>();
				UAsdaItemTooltipSubsystem* T2 = X->Sub<UAsdaItemTooltipSubsystem>();
				if (S2 && S2->DebugIsDragging() && AsdaUi::IsCursorOverWidget(GridCell(X.ToSharedRef(), kFillerCell)))
				{
					X->bOverCellDuringDrag = true;
					UAsdaTipPanel* P = T2 ? T2->GetMainPanel() : nullptr;
					if (P && P->IsShown() && P->GetOwner() == EAsdaTipOwner::Item) { X->bCellTipDuringDrag = true; }
				}
				return WW->IsSimClickPending();
			}), 0.f);
		});
		At(C, 13.8f, [C]()
		{
			TArray<uint8> B;
			const int32 N = LastSent(C, 5022, &B);
			C->Expect(N == 3 && B.Num() >= 4 && B[3] == 1, FString::Printf(TEXT("G1 перенос из гнезда 1 на ячейку сетки — 5022 с гнездом 1 (отправок 5022: %d)"), N));
			C->Expect(C->bOverCellDuringDrag && !C->bCellTipDuringDrag,
				FString::Printf(TEXT("G2 [ревизия М-2] пока камень «в руке» над занятой ячейкой сумки (%s), её подсказка НЕ всплывает (%s)"),
					C->bOverCellDuringDrag ? TEXT("был") : TEXT("НЕ был"), C->bCellTipDuringDrag ? TEXT("всплыла") : TEXT("нет")));
			// Контроль: камень отпущен, курсор остался над той же ячейкой — обычная подсказка ячейки на месте.
			UAsdaTipPanel* P = C->Sub<UAsdaItemTooltipSubsystem>()->GetMainPanel();
			C->Expect(P && P->IsShown() && P->GetOwner() == EAsdaTipOwner::Item, TEXT("G3 контроль: после отпускания подсказка ячейки под курсором показывается"));
			C->Sub<UAsdaSowelSubsystem>()->DebugClearAwaiting();
		});

		// ═══ J. [ревизия М-1] «камень → щелчок мимо → тот же камень» за окно двойного щелчка — НЕ двойной щелчок ═══
		At(C, 14.4f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D Stone = FVector2D::ZeroVector, Away = FVector2D::ZeroVector;
			const UWidget* Desc = I->GetWindowWidget() ? I->GetWindowWidget()->GetWidgetFromName(TEXT("INVEN_SHELLCONTENS")) : nullptr;
			if (!Center(S->DebugTrayIcon(1), Stone) || !Center(Desc, Away)) { C->Expect(false, TEXT("J0 камень 431 и описание 422 отрисованы")); return; }
			C->N5024Before = LastSent(C, 5024, nullptr);
			UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			WM->SimulateClickAt(Stone, 1);
			// Второй и третий щелчки — как только доставлен предыдущий (всё укладывается в окно двойного щелчка).
			TWeakObjectPtr<UAsdaWindowSubsystem> WW(WM);
			TSharedRef<int32> Stage = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, Stone, Away, Stage](float) -> bool
			{
				UAsdaWindowSubsystem* W2 = WW.Get();
				if (!W2) { return false; }
				if (W2->IsSimClickPending()) { return true; }
				if (*Stage == 0) { W2->SimulateClickAt(Away, 1); *Stage = 1; return true; }
				W2->SimulateClickAt(Stone, 1);
				return false;
			}), 0.f);
		});
		At(C, 15.6f, [C]()
		{
			const int32 N = LastSent(C, 5024, nullptr);
			C->Expect(N == C->N5024Before, FString::Printf(TEXT("J1 камень → щелчок мимо (описание) → камень: двойного щелчка НЕТ, 5024 не ушёл (было %d, стало %d)"), C->N5024Before, N));
			C->Sub<UAsdaSowelSubsystem>()->DebugClearAwaiting();
		});

		// ═══ K. [ревизия ВАЖНО-1] нотис 20120 — УБЫЛЬ прочности (старая +13 − новая +13), цвета отказов 4/5 ═══
		At(C, 16.0f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			const FAsdaItemInstance* It = I->GetCell(1, kBagCell);
			const TArray<uint8> Raw = It ? It->Raw : TArray<uint8>();
			const int32 OldDur = (Raw.Num() > 13) ? (int32)Raw[13] : -1;
			const int32 Sock0 = C->Sock0;
			auto Body = [Raw, Sock0](int32 Code, int32 NewDur) -> TArray<uint8>
			{
				TArray<uint8> B; B.SetNumZeroed(115);
				B[0] = (uint8)Code;
				if (Raw.Num() >= 57) { FMemory::Memcpy(B.GetData() + 1, Raw.GetData(), 57); }
				B[1 + 13] = (uint8)NewDur;                 // @14 — прочность НОВОЙ записи предмета
				B[1 + 16] = 0; B[1 + 17] = 0;              // гнездо 0 освобождено
				const TArray<uint8> Stone = FAsdaItemRecordWriter::Cell(Sock0, 2, 40, 1);
				FMemory::Memcpy(B.GetData() + 58, Stone.GetData(), 57);
				return B;
			};
			S->DebugClearLastNotice();
			S->SimulateDetachResult(Body(1, OldDur - 7));
			const FString T1 = S->DebugLastNotice();
			C->Expect(OldDur == 33 && T1.Contains(TEXT("7")) && !T1.Contains(TEXT("26")),
				FString::Printf(TEXT("K1 5023 успех, прочность 33 → 26: нотис 20120 с УБЫЛЬЮ 7 «%s»"), *T1));
			S->DebugClearLastNotice();
			S->SimulateDetachResult(Body(1, OldDur - 7));   // прочность не изменилась (26 → 26) — у оригинала строки нет вовсе
			C->Expect(S->DebugLastNotice().IsEmpty(), FString::Printf(TEXT("K2 убыль 0 — нотиса нет, как у оригинала («%s»)"), *S->DebugLastNotice()));
			const FColor Gold(0xFF, 0xD3, 0x06, 0xFF);
			S->DebugClearLastNotice();
			S->SimulateDetachResult(Body(4, 0));
			C->Expect(!S->DebugLastNotice().IsEmpty() && S->DebugLastNoticeColor() == Gold, FString::Printf(TEXT("K3 код 4 — 2003 цветом −11514 «%s»"), *S->DebugLastNotice()));
			S->DebugClearLastNotice();
			S->SimulateDetachResult(Body(5, 0));
			C->Expect(!S->DebugLastNotice().IsEmpty() && S->DebugLastNoticeColor() == Gold, FString::Printf(TEXT("K4 код 5 — 8013 цветом −11514 «%s»"), *S->DebugLastNotice()));

			// K5: 5025 успех (замена главного): [ss1] — ВСТАВЛЕННЫЙ камень (тот, что лежал в ячейке 2:51 ДО ответа),
			//   [ss2] — предмет; маркеров на экране нет; цвет −11514.
			UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
			FAsdaSowellRow Ins;
			const bool bIns = IT && IT->GetSowell(C->Good[1], Ins);
			TArray<uint8> R5; R5.SetNumZeroed(117);
			R5[0] = 1;
			if (Raw.Num() >= 57) { FMemory::Memcpy(R5.GetData() + 1, Raw.GetData(), 57); }
			const TArray<uint8> Back = FAsdaItemRecordWriter::Cell(C->Sock0, 2, kGoodFirst + 1, 1);   // старый камень — на место вставленного
			FMemory::Memcpy(R5.GetData() + 58, Back.GetData(), 57);
			S->DebugClearLastNotice();
			S->SimulateSocketResult(R5);
			const FString T5 = S->DebugLastNotice();
			const FString ItemName = IT ? IT->GetItemName(C->ItemCode) : FString();
			C->Expect(bIns && !Ins.Name.IsEmpty() && T5.Contains(Ins.Name) && T5.Contains(ItemName) && !T5.Contains(TEXT("[ss")) && S->DebugLastNoticeColor() == Gold,
				FString::Printf(TEXT("K5 5025 успех — 20121 «%s» (вставлен «%s», предмет «%s»)"), *T5, *Ins.Name, *ItemName));
		});

		// ═══ H. Пересоздание окна после сброса сессии (портал/перезаход): контроль с ключом, затем исправление ═══
		auto Recreate = [](const FCtxRef& X)
		{
			UAsdaInventorySubsystem* I = X->Sub<UAsdaInventorySubsystem>();
			UAsdaItemTooltipSubsystem* T = X->Sub<UAsdaItemTooltipSubsystem>();
			T->ResetSessionState(EAsdaSessionResetReason::MapChanged);   // как при смене карты
			// Кадр спустя подсистема подсказки привяжется к тому, что сейчас отдаёт инвентарь (= старому окну).
			At(X, 0.2f, [X, I]()
			{
				I->CloseWindow();
				X->Sub<UAsdaWindowSubsystem>()->CloseWindow(FName(TEXT("INVEN_WIN")));   // старый WBP уходит из вьюпорта
				I->OpenWindow();                                                         // `EnsureWidget` создаёт НОВЫЙ
				I->SetTab(1);
				PutTarget(X);
			});
			At(X, 1.0f, [X]()
			{
				FVector2D P;
				if (Center(GridCell(X, kBagCell), P)) { X->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, 2); }
				else { X->Expect(false, TEXT("H0 ячейка сумки 0 нового окна отрисована")); }
			});
		};
		At(C, 17.0f, [C, Recreate]()
		{
			C->Expect(true, FString::Printf(TEXT("H окно до пересоздания: %s"), *C->Sub<UAsdaInventorySubsystem>()->GetWindowWidget()->GetName()));
			AsdaCVarSave::SetForTest(TEXT("asda.ItemTip.LegacyNoInfoRebind"), 1);
			Recreate(C);
		});
		At(C, 19.4f, [C]() { CheckPanel(C, TEXT("H1"), /*bExpectClean*/ false); Shot(C, TEXT("H1_control_placeholders")); });
		At(C, 19.7f, [C, Recreate]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.ItemTip.LegacyNoInfoRebind"), 0);
			Recreate(C);
		});
		At(C, 22.2f, [C]() { CheckPanel(C, TEXT("H2"), /*bExpectClean*/ true); Shot(C, TEXT("H2_fixed")); });
		At(C, 22.6f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSowel(
	TEXT("asda.SelfTest.Sowel"),
	TEXT("Самотест самоцветов и правой инфо-панели инвентаря: выбор кликом, заглушки дизайнера, фильтр списка, полоса и колесо, "
	     "перенос и двойной щелчок (5022 гнездо 0/1, 5024) под перехватом, пересоздание окна. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaSowelTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Свой персонаж нужен гейтам мира — офлайн его нет: штатный фейковый спавн и повторный запуск через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s%s"), kCmd, KA.Has(TEXT("shot")) ? TEXT(" shot") : TEXT(""), KA.bQuit ? TEXT(" quit") : TEXT(""));
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
		C->bShot = KA.Has(TEXT("shot"));
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
