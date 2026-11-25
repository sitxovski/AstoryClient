// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «КОРОБКИ БУСТЕРА DEMON SET ПО КЛАССУ» (07.11.2026, пачка 12, баг 4): `asda.SelfTest.BoxClass [quit]`. [ИСПРАВЛЕНИЕ]
//   Баг: бустер воина 75978 выдавал коробку 75966 с баллистой, луком, арбалетом, посохом и тростью. Строки наборов 4365–4376 взяты из
//   Interlude (там 75906–75965 — броня Diablo), а у сервера под этими кодами вещи нумерации XYT. Лечение — данные: строки 4365–4376
//   дают оружие капитана своего класса (`Reports/booster_class_20261107`), ключа нет. Разделы:
//     D. все 12 бустеров 75978–75989: приз-коробка 75966–75977 и её состав — оружие только класса бустера, имя коробки по составу;
//     B/A/M/L. путь игрока (офлайн): коробка в ячейке сумки → настоящий двойной щелчок по живой геометрии → окно 26500; коды в ячейках
//        окна = состав нужного класса, чужих нет; наведение на ячейку даёт подсказку. B — воин ур. 1, A — лучник ур. 40, M — маг ур. 60,
//        L — воин ур. 70 (оружия 70 ур. нет, в коробке ур. 60).
//     R. часть 2 (слово владельца «Заменить и переименовать»): 14 коробок — имя и заголовок подсказки как в таблице, состав своего класса;
//        все пути имени: наведение в сумке (плашка), нотис получения 5013 в журнале чата 2572, карточки витрины 6000 (какие из 14 на ней есть).
//   Кадры — `Reports/booster_class_20261107/frames/<язык>_<раздел>.png`. Сумка возвращается. Итог: «КОРОБКИ-КЛАСС ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaPackagePanelSubsystem.h"
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaShopDblClick.h"
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
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBoxClassTest, Log, All);

namespace AsdaBoxClassTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.BoxClass");   // имя команды
	static const TCHAR* kPfx = TEXT("КОРОБКИ-КЛАСС");            // префикс строк и итога

	// Раздел пути игрока.
	struct FCase
	{
		const TCHAR* Tag;   // раздел — в имени кадра и в строках
		int32 Box;          // код коробки
	};
	static const FCase kCases[] = { { TEXT("B"), 75966 }, { TEXT("A"), 75970 }, { TEXT("M"), 75974 }, { TEXT("L"), 75975 } };

	// Коробка части 2: код, имя RU, имя EN, ветка класса вещей внутри (−1 — бонусный набор: класс у вложенной коробки).
	struct FRenamed
	{
		int32 Code;
		const TCHAR* Ru;
		const TCHAR* En;
		int32 Want;
	};
	static const FRenamed kRenamed[] = {
		{ 75512, TEXT("Набор испанского наряда (лучник, ур. 1)"), TEXT("Spanish Costume Set (Archer, Lv. 1)"), 1 },
		{ 75513, TEXT("Набор испанского наряда (маг, ур. 1)"), TEXT("Spanish Costume Set (Mage, Lv. 1)"), 2 },
		{ 75514, TEXT("Набор испанского наряда (солдат, ур. 40)"), TEXT("Spanish Costume Set (Fighter, Lv. 40)"), 0 },
		{ 75515, TEXT("Набор испанского наряда (лучник, ур. 1)"), TEXT("Spanish Costume Set (Archer, Lv. 1)"), 1 },
		{ 75516, TEXT("Костюм оленя-ветерана (маг, ур. 40)"), TEXT("Veteran Reindeer Set (Mage, Lv. 40)"), 2 },
		{ 75519, TEXT("Костюм оленя-ветерана (маг, ур. 40)"), TEXT("Veteran Reindeer Set (Mage, Lv. 40)"), 2 },
		{ 76152, TEXT("Набор испанского наряда (маг, ур. 1)"), TEXT("Spanish Costume Set (Mage, Lv. 1)"), 2 },
		{ 76153, TEXT("Костюм веселого снеговика (солдат, ур. 60)"), TEXT("Reindeer Set (Fighter, Lv. 60)"), 0 },
		{ 76154, TEXT("Костюм веселого снеговика (лучник, ур. 60)"), TEXT("Reindeer Set (Archer, Lv. 60)"), 1 },
		{ 76155, TEXT("Костюм веселого снеговика (маг, ур. 60)"), TEXT("Reindeer Set (Mage, Lv. 60)"), 2 },
		{ 76263, TEXT("Костюм оленя-капитана (маг, ур. 60)"), TEXT("Captain Reindeer Set (Mage, Lv. 60)"), 2 },
		{ 76305, TEXT("Бонусный набор снеговика (маг, ур. 40)"), TEXT("Snowman Bonus Package (Mage, Lv. 40)"), -1 },
		{ 76306, TEXT("Бонусный набор снеговика (маг, ур. 40)"), TEXT("Snowman Bonus Package (Mage, Lv. 40)"), -1 },
		{ 76308, TEXT("Бонусный набор снеговика (маг, ур. 40)"), TEXT("Snowman Bonus Package (Mage, Lv. 40)"), -1 },
	};
	// Имя коробки на языке сессии.
	static FString WantName(const FRenamed& R) { return FAsdaLang::IsRu() ? R.Ru : R.En; }

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;              // пройдено / провалено
		TWeakObjectPtr<UWorld> W;            // мир прогона
		bool bQuit = false;                  // выйти после итога
		TArray<uint8> SavedCell;             // ячейка 0 сумки до прогона (пусто — была пустой)
		int32 ShotCat = -1, ShotSub = 0, ShotPage = 0;   // страница витрины 6000 с первой найденной коробкой
		int32 SavedTab = 1;                  // вкладка до прогона
		bool bInvWasOpen = false;            // ★ R4-b (15.11.2026): окно рюкзака было открыто до прогона (закрыть за собой, если открыл сам)
		bool bShopWas = false, bPkWas = false;   // ★ R4-b хвосты (15.11.2026) (ревизия, мелочь 5): витрина 6000 и окно 26500 до прогона
		bool bSaved = false;                 // ★ R4-b хвосты (15.11.2026): ячейка (1,0) и вкладка сняты (ранний Finish их не пишет)
		// ★ R4-b хвосты-2 (15.11.2026) (М3): состояние витрины 6000 и окна 26500 до прогона (возвращается, если были открыты).
		int32 ShopCat = 0, ShopTool = 0, ShopEq = 0, ShopEq2 = 0, ShopPage = 0; bool bShopEq2 = false, bShopPkgTab = false;
		int32 PkItem = 0, PkMode = 0, PkCont = -1, PkSlot = -1;
		bool bPreOpen = false;               // ★ R4-b хвосты-2 (15.11.2026): аргумент `preopen` — открыть витрину и 26500 до снимка (проверка ветки возврата)
		FString FrameDir;                    // куда класть кадры

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaBoxClassTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaBoxClassTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд от старта.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Центр живого прямоугольника узла (false — не отрисован / скрыт).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Курсор в точку + событие движения.
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Ветка класса по UsageCategory: 0 воин, 1 лучник, 2 маг, −1 общая.
	static int32 Branch(int32 Usage)
	{
		return (Usage >= 1 && Usage <= 11) ? 0 : (Usage >= 12 && Usage <= 22) ? 1 : (Usage >= 23 && Usage <= 33) ? 2 : -1;
	}
	// Ветка оружия по подтипу (щит — воин); −1 — не оружие.
	static int32 WeaponBranch(const FAsdaItemRecord* R)
	{
		if (!R) { return -1; }
		if (R->ItemType == 8) { return 0; }
		if (R->ItemType != 9) { return -1; }
		switch (R->ItemSubType)
		{
		case 1: case 2: case 3:  return 0;
		case 9: case 10: case 11: return 1;
		case 5:                  return 2;
		default:                 return -1;
		}
	}
	// Положить запись в ячейку 0 сумки (пусто — очистить).
	static void PutCell(UAsdaInventorySubsystem* Inv, const TArray<uint8>& Rec)
	{
		Inv->ApplyExternalRecord(Rec.Num() == FAsdaItemRecordWriter::kSize ? Rec : FAsdaItemRecordWriter::ClearCell(1, 0),
			UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	}

	// Весь текст показанной плашки предмета.
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
		return Out;
	}
	// Тексты окна витрины 6000 в вьюпорте.
	static TArray<FString> ShopTexts(const FCtxRef& C)
	{
		TArray<FString> Out;
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* U = *It;
			if (!U || U->GetWorld() != C->W.Get() || !U->IsInViewport() || !U->GetClass()->GetName().Contains(TEXT("SHELLITEM_STORE")) || !U->WidgetTree) { continue; }
			TArray<UWidget*> All;
			U->WidgetTree->GetAllWidgets(All);
			for (UWidget* Wd : All) { if (const UTextBlock* Tb = Cast<UTextBlock>(Wd)) { Out.Add(Tb->GetText().ToString()); } }
		}
		return Out;
	}
	// Тело 5013 (62 б, раскладка сервера): результат, подобравший, запись 57 б, вес.
	static TArray<uint8> Body5013(const TArray<uint8>& Rec57)
	{
		TArray<uint8> B; B.SetNumZeroed(62);
		B[0] = 1;
		AsdaBytes::WrI16(&B[1], (int16)-1);
		if (Rec57.Num() >= FAsdaItemInstance::kRecordSize) { FMemory::Memcpy(&B[3], Rec57.GetData(), FAsdaItemInstance::kRecordSize); }
		AsdaBytes::WrI16(&B[60], (int16)100);
		return B;
	}

	// Раздел R, данные: имя, заголовок подсказки, состав.
	static void CheckRenamedData(const FCtxRef& C, UAsdaItemTableSubsystem* IT, UAsdaItemTooltipSubsystem* Tip)
	{
		for (const FRenamed& R : kRenamed)
		{
			const FString Want = WantName(R);
			const FString Got = IT->GetItemName(R.Code);
			const FAsdaItemTooltipText T = Tip->BuildTooltipByCode(R.Code);
			TArray<FAsdaPackageEntry> Pc;
			const bool bBox = IT->GetPackageContents(R.Code, Pc);
			int32 Foreign = 0;
			FString Names;
			for (const FAsdaPackageEntry& E : Pc)
			{
				const FAsdaItemRecord* X = IT->GetItem(E.ItemCode);
				if (R.Want >= 0 && (!X || Branch(X->UsageCategory) != R.Want)) { ++Foreign; }
				if (R.Want < 0 && X && X->ItemSubType == 200 && Branch(X->UsageCategory) != 2) { ++Foreign; }
				Names += IT->GetItemName(E.ItemCode) + TEXT("; ");
			}
			C->Expect(Got == Want && T.Title.Contains(Want) && bBox && Foreign == 0,
				FString::Printf(TEXT("R0 %d: имя «%s» (ждём «%s»), заголовок «%s», чужих %d: %s"), R.Code, *Got, *Want, *T.Title, Foreign, *Names));
		}
	}

	// Раздел R, путь игрока: нотис получения и наведение в сумке — по одной коробке за шаг.
	static void RunRenamed(const FCtxRef& C, int32 Idx, float T0)
	{
		const FRenamed R = kRenamed[Idx];
		At(C, T0, [C, R]()
		{
			if (UAsdaPackagePanelSubsystem* Pk = C->Sub<UAsdaPackagePanelSubsystem>()) { Pk->ClosePanel(); }
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
			const UAsdaChatSubsystem* Chat = C->Sub<UAsdaChatSubsystem>();
			if (!I || !Net || !Chat) { return; }
			PutCell(I, TArray<uint8>());
			I->DebugPumpPendingPackets();
			const FString Needle = FString::Printf(TEXT("[%s]"), *WantName(R));
			const int32 Before = Chat->DebugCountSystemLines(Needle);
			Net->DebugFeedPacket(5013, Body5013(FAsdaItemRecordWriter::Cell(R.Code, 1, 0, 0)));
			I->DebugPumpPendingPackets();
			const int32 After = Chat->DebugCountSystemLines(Needle);
			C->Expect(After == Before + 1, FString::Printf(TEXT("R1 %d нотис получения 5013 с именем «%s»: строк %d→%d"), R.Code, *Needle, Before, After));
			if (!I->IsWindowOpen()) { I->OpenWindow(); }
			I->SetTab(1);
			Hover(FVector2D(5.0, 5.0));
		});
		At(C, T0 + 0.3f, [C]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D P = FVector2D::ZeroVector;
			if (I && Center(I->DebugGetGridIcon(0), P)) { Hover(P); }
		});
		At(C, T0 + 0.7f, [C, R]()
		{
			const FString Txt = TipText(C);
			C->Expect(Txt.Contains(WantName(R)), FString::Printf(TEXT("R2 %d наведение в сумке: плашка с именем «%s»"), R.Code, *WantName(R)));
		});
	}

	// Раздел R, витрина 6000: у каждой из 14 коробок, что есть на карточках, — новое имя.
	static void CheckShop(const FCtxRef& C)
	{
		UAsdaPremiumShopSubsystem* P = C->Sub<UAsdaPremiumShopSubsystem>();
		if (!P) { C->Expect(false, TEXT("R3 подсистема витрины есть")); return; }
		// ★ R4-b хвосты-2 (15.11.2026) (ревизия 3) (М-2): открытие сбрасывает витрину целиком (категории, страницы, корзину) — только если закрыта;
		//   вкладка наборов прячет сетку карточек — на время обхода снять (вернёт `Finish`, если витрина была открыта).
		if (!P->IsOpen()) { P->OpenWindow(); }
		if (P->IsPackageTab()) { P->ShowPackageTab(false); }
		TSet<int32> Seen, SeenOk;
		for (int32 Cat = 0; Cat < 8; ++Cat)
		{
			for (int32 Sub = 0; Sub < 5; ++Sub)
			{
				P->SetCategory(Cat);
				if (P->GetCategory() == 3) { if (Sub > 2) { continue; } P->SetEquipSub(Sub); } else { P->SetToolSub(Sub); }
				for (int32 Pg = 0; Pg < 20; ++Pg)
				{
					P->SetPage(Pg);
					const TArray<FString> T = ShopTexts(C);
					for (int32 Card = 0; Card < UAsdaPremiumShopSubsystem::kCards; ++Card)
					{
						const int32 Code = P->DebugCardCode(Card);
						for (const FRenamed& R : kRenamed)
						{
							if (R.Code != Code) { continue; }
							Seen.Add(Code);
							if (T.Contains(WantName(R))) { SeenOk.Add(Code); }
							if (C->ShotCat < 0) { C->ShotCat = Cat; C->ShotSub = Sub; C->ShotPage = Pg; }
						}
					}
				}
			}
		}
		FString List;
		for (int32 Code : Seen) { List += FString::Printf(TEXT("%d "), Code); }
		C->Expect(Seen.Num() > 0 && Seen.Num() == SeenOk.Num(), FString::Printf(TEXT("R3 витрина 6000: коробок на карточках %d (%s), с новым именем %d"), Seen.Num(), *List, SeenOk.Num()));
		if (C->ShotCat >= 0)
		{
			P->SetCategory(C->ShotCat);
			if (P->GetCategory() == 3) { P->SetEquipSub(C->ShotSub); } else { P->SetToolSub(C->ShotSub); }
			P->SetPage(C->ShotPage);
		}
	}

	static void Finish(const FCtxRef& C)
	{
		// ★ R4-b хвосты (15.11.2026) (ревизия, мелочь 5): закрывать только открытое самим тестом; ячейку и вкладку — только после снимка.
		//   Подсказку снимать всегда: тест подменил её содержимое, прежнюю показывает следующее наведение.
		// ★ R4-b хвосты-2 (15.11.2026) (М3): открытое до прогона — вернуть в прежнее состояние, а не только «не закрывать».
		UAsdaPremiumShopSubsystem* P = C->Sub<UAsdaPremiumShopSubsystem>();
		if (P && !C->bShopWas) { P->CloseWindow(); }
		else if (P)
		{
			if (!P->IsOpen()) { P->OpenWindow(); }
			// ★ R4-b хвосты-2 (15.11.2026) (ревизия 3) (М-1): все ветки, не только текущая. `SetEquipSub` ставит класс (и показывает третий уровень), `SetCategory`
			//   класс у 3 сохраняет и третий уровень скрывает (у 2 — сбрасывает подкатегорию, поэтому `SetToolSub` после), третий уровень —
			//   заново, если был показан; вкладка наборов — после категории; страница — последней (её сбрасывает каждый сеттер).
			P->SetEquipSub(C->ShopEq);
			P->SetCategory(C->ShopCat);
			P->SetToolSub(C->ShopTool);
			if (C->bShopEq2) { P->SetEquipSub(C->ShopEq); P->SetEquipSub2(C->ShopEq2); }
			if (P->IsPackageTab() != C->bShopPkgTab) { P->ShowPackageTab(C->bShopPkgTab); }
			P->SetPage(C->ShopPage);
		}
		if (UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>()) { T->HideTooltip(); }
		UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
		if (I)   // ★ R4-b (15.11.2026): окно рюкзака — как до прогона; ★ R4-b хвосты-2 (15.11.2026) (М1-аналог): открытое до прогона — открыть снова
		{
			if (C->bSaved) { PutCell(I, C->SavedCell); I->SetTab(C->SavedTab); }
			if (!C->bInvWasOpen && I->IsWindowOpen()) { I->CloseWindow(); }
			else if (C->bInvWasOpen && !I->IsWindowOpen()) { I->OpenWindow(); UE_LOG(LogAsdaBoxClassTest, Display, TEXT("%s уборка: рюкзак открыт снова"), kPfx); }   // ★ R4-b хвосты-2 (15.11.2026) (ревизия 3)
		}
		// 26500 — после возврата ячейки (1,0): источник панели снова на месте.
		UAsdaPackagePanelSubsystem* Pk = C->Sub<UAsdaPackagePanelSubsystem>();
		if (Pk && !C->bPkWas) { Pk->ClosePanel(); }
		else if (Pk) { Pk->Populate(C->PkItem, C->PkMode, C->PkCont, C->PkSlot); }
		if (P && Pk && I)
		{
			// ★ R4-b хвосты-2 (15.11.2026) (ревизия 3) (М-1): сверка всех полей (третий уровень — только если показан: скрытый сбрасывается следующим показом).
			const bool bShopOk = P->IsOpen() == C->bShopWas && (!C->bShopWas || (P->GetCategory() == C->ShopCat && P->GetPage() == C->ShopPage
				&& P->GetToolSub() == C->ShopTool && P->GetEquipSub() == C->ShopEq && P->IsEquipSub2Shown() == C->bShopEq2
				&& (!C->bShopEq2 || P->GetEquipSub2() == C->ShopEq2) && P->IsPackageTab() == C->bShopPkgTab));
			C->Expect(bShopOk, FString::Printf(TEXT("R4 витрина 6000 как до прогона: открыта %d/%d, категория %d/%d, подкатегория %d/%d, класс %d/%d, "
				"третий уровень %d/%d, наборы %d/%d, страница %d/%d"),
				P->IsOpen() ? 1 : 0, C->bShopWas ? 1 : 0, P->GetCategory(), C->ShopCat, P->GetToolSub(), C->ShopTool, P->GetEquipSub(), C->ShopEq,
				P->IsEquipSub2Shown() ? 1 : 0, C->bShopEq2 ? 1 : 0, P->IsPackageTab() ? 1 : 0, C->bShopPkgTab ? 1 : 0, P->GetPage(), C->ShopPage));
			const bool bPkOk = Pk->IsPanelOpen() == C->bPkWas && (!C->bPkWas || (Pk->GetSelItemCode() == C->PkItem && Pk->GetStampMode() == C->PkMode));
			C->Expect(bPkOk, FString::Printf(TEXT("R4 окно 26500 как до прогона: открыто %d/%d, коробка %d/%d, режим %d/%d"),
				Pk->IsPanelOpen() ? 1 : 0, C->bPkWas ? 1 : 0, Pk->GetSelItemCode(), C->PkItem, Pk->GetStampMode(), C->PkMode));
			C->Expect(I->IsWindowOpen() == C->bInvWasOpen, FString::Printf(TEXT("R4 рюкзак как до прогона: открыт %d/%d"), I->IsWindowOpen() ? 1 : 0, C->bInvWasOpen ? 1 : 0));
		}
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaBoxClassTest, kPfx, C->Ok, C->Fail);
	}

	// Раздел D: данные всех 12 бустеров.
	static void CheckData(const FCtxRef& C, UAsdaItemTableSubsystem* IT)
	{
		for (int32 Booster = 75978; Booster <= 75989; ++Booster)
		{
			const FAsdaItemRecord* B = IT->GetItem(Booster);
			const int32 Want = B ? Branch(B->UsageCategory) : -1;
			TArray<int32> Slots;
			const bool bRow = IT->GetBoosterSlotsRaw(Booster, Slots);
			const int32 Box = 75966 + (Booster - 75978);
			TArray<FAsdaPackageEntry> Pc;
			const bool bBox = IT->GetPackageContents(Box, Pc);
			int32 Foreign = 0;
			FString Names;
			for (const FAsdaPackageEntry& E : Pc)
			{
				const FAsdaItemRecord* R = IT->GetItem(E.ItemCode);
				if (WeaponBranch(R) != Want) { ++Foreign; }
				Names += IT->GetItemName(E.ItemCode) + TEXT("; ");
			}
			C->Expect(B && Want >= 0 && bRow && Slots.Contains(Box) && bBox && Pc.Num() >= 3 && Foreign == 0,
				FString::Printf(TEXT("D бустер %d (класс %d) → коробка %d «%s»: %d вещей, чужих %d: %s"),
					Booster, Want, Box, *IT->GetItemName(Box), Pc.Num(), Foreign, *Names));
		}
	}

	// Раздел пути игрока: двойной щелчок по коробке и разбор ячеек окна.
	static void RunCase(const FCtxRef& C, const FCase K, float T0)
	{
		At(C, T0, [C, K]()
		{
			if (UAsdaPackagePanelSubsystem* Pk = C->Sub<UAsdaPackagePanelSubsystem>()) { Pk->ClosePanel(); }
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			if (!I) { return; }
			PutCell(I, FAsdaItemRecordWriter::Cell(K.Box, 1, 0, 1));
			if (!I->IsWindowOpen()) { I->OpenWindow(); }
			I->SetTab(1);
		});
		At(C, T0 + 0.7f, [C, K]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D P = FVector2D::ZeroVector;
			const bool bGeo = I && Center(I->DebugGetGridIcon(0), P);
			C->Expect(bGeo, FString::Printf(TEXT("%s0 коробка %d в ячейке сумки на экране (%.0f,%.0f)"), K.Tag, K.Box, P.X, P.Y));
			if (bGeo) { Hover(P); AsdaSimDoubleClickAt(C->Sub<UAsdaWindowSubsystem>(), P, false); }
		});
		At(C, T0 + 1.6f, [C, K]()
		{
			UAsdaPackagePanelSubsystem* Pk = C->Sub<UAsdaPackagePanelSubsystem>();
			UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
			const FAsdaItemRecord* Box = IT ? IT->GetItem(K.Box) : nullptr;
			const int32 Want = Box ? Branch(Box->UsageCategory) : -1;
			TArray<FAsdaPackageEntry> Pc;
			if (IT) { IT->GetPackageContents(K.Box, Pc); }
			TSet<int32> Shown;
			UWidget* First = nullptr;
			for (int32 r = 0; Pk && r < UAsdaPackagePanelSubsystem::kBlocks; ++r)
			{
				for (int32 Big = 0; Big < 2; ++Big)
				{
					const int32 N = Big ? UAsdaPackagePanelSubsystem::kBigCells : UAsdaPackagePanelSubsystem::kSmallCells;
					for (int32 k = 0; k < N; ++k)
					{
						const int32 Code = Pk->DebugCellCode(r, Big != 0, k);
						if (Code > 0)
						{
							Shown.Add(Code);
							if (!First) { First = Pk->DebugCellIcon(r, Big != 0, k); }
						}
					}
				}
			}
			int32 Foreign = 0, Missing = 0;
			FString Names;
			for (int32 Code : Shown) { if (Code != K.Box && WeaponBranch(IT->GetItem(Code)) != Want) { ++Foreign; } Names += IT->GetItemName(Code) + TEXT("; "); }
			for (const FAsdaPackageEntry& E : Pc) { if (!Shown.Contains(E.ItemCode)) { ++Missing; } }
			C->Expect(Pk && Pk->IsPanelOpen() && Pc.Num() >= 3 && Missing == 0 && Foreign == 0,
				FString::Printf(TEXT("%s1 двойной щелчок по коробке %d «%s» → окно 26500 (открыто %d): в ячейках %d, нет %d, чужого класса %d: %s"),
					K.Tag, K.Box, IT ? *IT->GetItemName(K.Box) : TEXT("?"), Pk && Pk->IsPanelOpen() ? 1 : 0, Shown.Num(), Missing, Foreign, *Names));
			FVector2D P = FVector2D::ZeroVector;
			if (Center(First, P)) { Hover(P); }
		});
		At(C, T0 + 2.2f, [C, K]()
		{
			const UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
			C->Expect(T && T->IsTooltipShown(), FString::Printf(TEXT("%s2 наведение на ячейку окна 26500 → подсказка предмета"), K.Tag));
			const FString File = C->FrameDir / FString::Printf(TEXT("%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), K.Tag);
			FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogAsdaBoxClassTest, Display, TEXT("%s кадр заказан → %s"), kPfx, *File);
		});
	}

	static void Run(const FCtxRef& C)
	{
		// ★ R4-b хвосты-2 (15.11.2026) (М3): `preopen` — витрина (категория 2, подкатегория 1, страница 3 — `SetPage` ограничивает её числом страниц
		//   подкатегории, выходит 1; категория 1 скрыта ключом владельца), рюкзак (★ R4-b хвосты-2 (15.11.2026) (ревизия 3), М-4: после витрины — она при открытии закрывает
		//   окна) и 26500 по коробке 75966 открыты до снимка.
		if (C->bPreOpen)
		{
			if (UAsdaPremiumShopSubsystem* P1 = C->Sub<UAsdaPremiumShopSubsystem>()) { if (!P1->IsOpen()) { P1->OpenWindow(); } P1->SetCategory(2); P1->SetToolSub(1); P1->SetPage(3); }
			if (UAsdaInventorySubsystem* I1 = C->Sub<UAsdaInventorySubsystem>()) { if (!I1->IsWindowOpen()) { I1->OpenWindow(); } }   // ★ R4-b хвосты-2 (15.11.2026) (ревизия 3) (М-4)
			if (UAsdaPackagePanelSubsystem* K1 = C->Sub<UAsdaPackagePanelSubsystem>()) { K1->Populate(75966, 0, -1, -1); }
			const UAsdaInventorySubsystem* Il = C->Sub<UAsdaInventorySubsystem>();
			const UAsdaPremiumShopSubsystem* Pl = C->Sub<UAsdaPremiumShopSubsystem>();
			const UAsdaPackagePanelSubsystem* Kl = C->Sub<UAsdaPackagePanelSubsystem>();
			UE_LOG(LogAsdaBoxClassTest, Display, TEXT("%s preopen: до снимка витрина=%d, рюкзак=%d, 26500=%d"), kPfx,
				Pl && Pl->IsOpen() ? 1 : 0, Il && Il->IsWindowOpen() ? 1 : 0, Kl && Kl->IsPanelOpen() ? 1 : 0);
		}
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		C->bInvWasOpen = Inv && Inv->IsWindowOpen();   // ★ R4-b (15.11.2026): до любого выхода (ранний Finish тоже его видит)
		if (const UAsdaPremiumShopSubsystem* P0 = C->Sub<UAsdaPremiumShopSubsystem>()) { C->bShopWas = P0->IsOpen(); }   // ★ R4-b хвосты (15.11.2026)
		if (const UAsdaPackagePanelSubsystem* K0 = C->Sub<UAsdaPackagePanelSubsystem>()) { C->bPkWas = K0->IsPanelOpen(); }   // ★ R4-b хвосты (15.11.2026)
		if (const UAsdaPremiumShopSubsystem* P2 = C->Sub<UAsdaPremiumShopSubsystem>())   // ★ R4-b хвосты-2 (15.11.2026) (М3)
		{
			C->ShopCat = P2->GetCategory(); C->ShopTool = P2->GetToolSub(); C->ShopEq = P2->GetEquipSub(); C->ShopEq2 = P2->GetEquipSub2();
			C->bShopEq2 = P2->IsEquipSub2Shown(); C->ShopPage = P2->GetPage(); C->bShopPkgTab = P2->IsPackageTab();
		}
		if (const UAsdaPackagePanelSubsystem* K2 = C->Sub<UAsdaPackagePanelSubsystem>())   // ★ R4-b хвосты-2 (15.11.2026) (М3)
		{
			C->PkItem = K2->GetSelItemCode(); C->PkMode = K2->GetStampMode(); C->PkCont = K2->GetSelContainer(); C->PkSlot = K2->GetSelSlot();
		}
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		if (!Inv || !IT || !C->Sub<UAsdaWindowSubsystem>() || !C->Sub<UAsdaPackagePanelSubsystem>())
		{
			C->Expect(false, TEXT("подсистемы инвентаря/таблиц/окон/окна 26500 есть"));
			Finish(C);
			return;
		}
		CheckData(C, IT);
		if (UAsdaItemTooltipSubsystem* Tip = C->Sub<UAsdaItemTooltipSubsystem>()) { CheckRenamedData(C, IT, Tip); }
		C->SavedTab = Inv->GetTab();
		{ const FAsdaItemInstance* It = Inv->GetCell(1, 0); C->SavedCell = (It && !It->IsEmpty()) ? It->Raw : TArray<uint8>(); }
		C->bSaved = true;   // ★ R4-b хвосты (15.11.2026): с этого места Finish возвращает ячейку и вкладку
		C->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/booster_class_20261107/frames"));
		IFileManager::Get().MakeDirectory(*C->FrameDir, /*Tree*/ true);
		float T = 0.5f;
		for (const FCase& K : kCases) { RunCase(C, K, T); T += 3.f; }
		for (int32 i = 0; i < UE_ARRAY_COUNT(kRenamed); ++i) { RunRenamed(C, i, T); T += 1.f; }
		At(C, T, [C]() { CheckShop(C); });
		At(C, T + 0.8f, [C]()
		{
			const FString File = C->FrameDir / FString::Printf(TEXT("%s_shop.png"), FAsdaLang::Code(FAsdaLang::Current()));
			FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		});
		At(C, T + 1.5f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestBoxClass(
	TEXT("asda.SelfTest.BoxClass"),
	TEXT("Самотест бага Demon Set: 12 бустеров 75978–75989 и коробки 75966–75977 — оружие только своего класса; двойной щелчок по коробке "
	     "в сумке, ячейки окна 26500, подсказка. Кадры в Reports/booster_class_20261107/frames. Офлайн, карта мира. `delay=N` `quit`; `preopen` — витрина и 26500 открыты до прогона (проверка возврата)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaBoxClassTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""),
				KA.Has(TEXT("preopen")) ? TEXT(" preopen") : TEXT(""));   // ★ R4-b хвосты-2 (15.11.2026): `preopen` переживает повторный запуск
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
		C->bPreOpen = KA.Has(TEXT("preopen"));   // ★ R4-b хвосты-2 (15.11.2026) (М3)
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
