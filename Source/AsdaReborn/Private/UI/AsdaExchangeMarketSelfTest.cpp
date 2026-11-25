// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПАЧКИ 20 (09.11.2026): РЫНОК (АУКЦИОН 13000) — ДЕСЯТЬ ЖАЛОБ ВЛАДЕЛЬЦА.
//
// ЧТО ПРОВЕРЯЕТ (путь игрока: окно открывает та же точка, что код диалога NPC — `OpenMode`; ответы сервера идут через штатный
//   приём сети `DebugFeedPacket` → `npcShop_DispatchResult`-порт; галочки и ячейки — щелчком `SimulateClickAt` по живой геометрии):
//   Ш0  [воспроизведение, ключ `asda.Exchange.RouteBySub` 0] пустой 9908 уводит в ветку 9910 и шлёт 9907 снова (шторм).
//   Ж1/Ж2  «Мои предметы» при пустом списке: 9907 ушёл один раз, пустой 9908 ОТКРЫВАЕТ окно с панелью 13200, повторного 9907 нет.
//   Ж8  вкладки 13260: лот «расходники» → вкладка «снаряжение» (9907 с категорией 2) → пустой 9908 очищает список.
//   Ж5  большая галочка «выбрать всё» 13479/13478/13482 отмечает все строки; снятие строки гасит её.
//   Ж4/Ж9  забор 9910 кладёт вещь в ячейку из записи; лот уходит из списка без нового 9907; вещь видна в сетке выставления.
//   Ж6  вещь с количеством 1: второй щелчок по той же ячейке строку НЕ добавляет, ячейка в сетке гаснет.
//   Ж7  выставление 9902: ячейка вещи очищается (стопка — остаток B − A).
//   Ж3  страница 9904: лоты видны; ПУСТАЯ страница очищает выдачу (раньше уходила в ветку покупки и оставляла старое).
//   Покупка 9906 кладёт вещь в сумку.
//   Ж10 рынок открыт: склад не открывается, лавка 6300 не уходит, своя заявка 5282 не уходит, входящая 5283 — авто-ответ 5284/3;
//       обратное: при открытом складе рынок не открывается.
//   Кадры — `Reports/batch20/market/<язык>_<кадр>.png`.
//
// Консоль: `asda.Exchange.MarketSelfTest [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-РЫНОК», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaExchangeSubsystem.h"
#include "UI/AsdaExchangeP2PSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"
#include "UI/AsdaSelfStoreSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaIconCellGrid.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMarketTest, Log, All);

struct FAsdaMarketSelfTest
{
	static constexpr const TCHAR* kWho = TEXT("asda.Exchange.MarketSelfTest");   // имя самотеста (сторож ключей)

	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено
	TArray<TFunction<float(UWorld*)>> Steps;   // шаг возвращает паузу (с) до следующего
	FString Lang;                              // ru / en — префикс кадров
	FString ShotDir;                           // папка кадров
	TMap<int32, int32> Sent;                   // перехваченные отправки: опкод → число
	TMap<int32, TArray<uint8>> LastBody;       // последнее тело по опкоду
	uint32 CapGen = 0;                         // поколение перехвата
	TSharedPtr<FAsdaNetSelfScope> SelfScope;   // уровень/номер своего персонажа (возврат в конце)

	// Предметы прогона (выбираются из таблицы при старте).
	int32 CodeSpend = 0;    // расходник: категория 1, `tradableFlag == 1`, без `flagWord176` (режим 1, контейнер 2)
	int32 CodeGear  = 0;    // снаряжение: категория 3 (режим 2, контейнер 1)
	static constexpr int32 kCellOne   = 40;   // контейнер 2: расходник ×1 (жалоба 6)
	static constexpr int32 kCellStack = 41;   // контейнер 2: расходник ×5 (выставление стопки, жалоба 7)
	static constexpr int32 kCellTake  = 42;   // контейнер 2: сюда сервер «вернул» забранный лот (жалобы 4/9)
	static constexpr int32 kCellBuy   = 43;   // контейнер 2: сюда «легла» покупка 9906

	void Expect(bool b, const FString& Name, const FString& Detail = FString())
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaMarketTest, Log, TEXT("САМОТЕСТ-РЫНОК %s: %s%s%s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name,
			Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
	}
	void Shot(const TCHAR* Name)
	{
		const FString File = FPaths::Combine(ShotDir, FString::Printf(TEXT("%s_%s.png"), *Lang, Name));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaMarketTest, Log, TEXT("САМОТЕСТ-РЫНОК кадр: %s"), *File);
	}
	static UGameInstance* GI(UWorld* W) { return W ? W->GetGameInstance() : nullptr; }
	template <class T> static T* Sub(UWorld* W) { UGameInstance* G = GI(W); return G ? G->GetSubsystem<T>() : nullptr; }
	int32 Count(int32 Op) const { const int32* N = Sent.Find(Op); return N ? *N : 0; }

	// Щелчок ЛКМ по центру живой геометрии узла.
	bool Click(UWorld* W, const UWidget* X, const TCHAR* What)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		FVector2D Mn, Mx;
		if (!WM || !X || !AsdaUi::WidgetAbsRect(X, Mn, Mx) || (Mx - Mn).IsNearlyZero())
		{
			Expect(false, FString::Printf(TEXT("щелчок: %s"), What), TEXT("нет живой геометрии узла"));
			return false;
		}
		WM->SimulateClickAt((Mn + Mx) * 0.5f, 2, false);
		return true;
	}
	// Снять окна-сообщения (модалки нотисов перекрывают мышь окна).
	static void Calm(UWorld* W)
	{
		if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W)) { WM->ClearMsgBoxQueue(TEXT("самотест рынка")); }
	}
	static void Feed(UWorld* W, int32 Op, const TArray<uint8>& Body)
	{
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(W)) { Net->DebugFeedPacket(Op, Body); }
	}

	// Запись 9908 «мой лот» (62 б): серийник +0, статус +4, код +5, количество +9, остаток мс +46, цена +50.
	static void AddMyLot(TArray<uint8>& B, int32 Serial, int32 Code, int32 Amount, int32 Price)
	{
		const int32 O = B.Num(); B.AddZeroed(62);
		AsdaBytes::WrI32(B.GetData() + O + 0, Serial);
		AsdaBytes::WrI32(B.GetData() + O + 5, Code);
		AsdaBytes::WrI32(B.GetData() + O + 9, Amount);
		AsdaBytes::WrI32(B.GetData() + O + 46, 3 * 3600000 + 25 * 60000);   // 3 ч 25 мин
		AsdaBytes::WrI32(B.GetData() + O + 50, Price);
	}
	// Запись 9910 «лот снят» (61 б) / 9906 «лот куплен» (85 б): код +7, количество +11, контейнер +15, слот +16.
	static TArray<uint8> LotBack(int32 Size, int32 Serial, int32 Code, int32 Amount, int32 Cont, int32 Slot)
	{
		TArray<uint8> B; B.SetNumZeroed(Size);
		AsdaBytes::WrI32(B.GetData() + 0, Serial);
		B[6] = 0;
		AsdaBytes::WrI32(B.GetData() + 7, Code);
		AsdaBytes::WrI32(B.GetData() + 11, Amount);
		B[15] = (uint8)Cont;
		AsdaBytes::WrI16(B.GetData() + 16, Slot);
		B[20] = 100;   // прочность
		return B;
	}
	// Запись 9904 «лот выдачи» (80 б).
	static void AddListing(TArray<uint8>& B, int32 Serial, int32 Code, int32 Price)
	{
		const int32 O = B.Num(); B.AddZeroed(80);
		AsdaBytes::WrI32(B.GetData() + O + 0, Serial);
		AsdaBytes::WrI32(B.GetData() + O + 5, Code);
		AsdaBytes::WrI32(B.GetData() + O + 9, 1);
		B[O + 13] = 100;
		B[O + 46] = 0; B[O + 47] = 0;
		AsdaBytes::WrI32(B.GetData() + O + 48, Price);
		FMemory::Memcpy(B.GetData() + O + 52, "TESTSELLER", 10);
	}
	static int32 MyCount(const UAsdaExchangeSubsystem* Ex) { int32 N = 0; for (const FAsdaExchangeMyLot& L : Ex->MyLots) { N += L.IsEmpty() ? 0 : 1; } return N; }
	static int32 LotCount(const UAsdaExchangeSubsystem* Ex) { int32 N = 0; for (const FAsdaExchangeListing& L : Ex->Listings) { N += L.IsEmpty() ? 0 : 1; } return N; }
	static int32 RegCount(const UAsdaExchangeSubsystem* Ex) { int32 N = 0; for (const FAsdaExchangeRegRow& R : Ex->RegRows) { N += R.IsEmpty() ? 0 : 1; } return N; }
	static bool MyPanelShown(const UAsdaExchangeSubsystem* Ex)
	{
		for (const TObjectPtr<UWidget>& X : Ex->GrpMyItem) { if (X && X->IsVisible()) { return true; } }
		return false;
	}
	static bool WinShown(UWorld* W)
	{
		const UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		return WM && WM->IsWindowVisible(FName(TEXT("EXCHANGE_WIN")));
	}
	// Иконка сетки выставления, показывающая ячейку контейнера `Cell` (nullptr — ячейки в сетке нет).
	static UImage* SellIconFor(const UAsdaExchangeSubsystem* Ex, int32 Cell)
	{
		const int32 Gi = Ex->SellCells.IndexOfByKey(Cell);
		const int32 Vis = Gi - Ex->ScrollRowSell * 6;
		return (Gi >= 0 && Ex->SellIcons.IsValidIndex(Vis)) ? Ex->SellIcons[Vis].Get() : nullptr;
	}
	static int32 CellCode(UWorld* W, int32 Cont, int32 Cell)
	{
		const UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(W);
		return Inv ? Inv->GetCellItemCode(Cont, Cell) : -1;
	}
	static int32 CellAmount(UWorld* W, int32 Cont, int32 Cell)
	{
		const UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(W);
		return Inv ? Inv->GetCellAmount(Cont, Cell) : -1;
	}

	bool PickItems(UWorld* W)
	{
		const UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(W);
		if (!IT) { return false; }
		for (const FAsdaItemRecord& R : IT->GetAllItems())
		{
			const int32 Cat = IT->GetItemCategory(R.ItemCode);
			if (R.ItemCode <= 20552 || R.ItemName.IsEmpty()) { continue; }   // золото 20551 и служебные записи — мимо
			if (!CodeSpend && Cat == 1 && R.TradableFlag == 1 && R.FlagWord176 == 0 && R.CategoryFlag != 3) { CodeSpend = R.ItemCode; }
			if (!CodeGear && Cat == 3 && R.CategoryFlag != 3 && R.ItemType >= 0 && R.ItemType <= 4) { CodeGear = R.ItemCode; }
			if (CodeSpend && CodeGear) { break; }
		}
		return CodeSpend > 0 && CodeGear > 0;
	}

	void BuildSteps()
	{
		// ═══ Подготовка: уровень 40, предметы в сумке, перехват отправок ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(W);
			UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(W);
			SelfScope = MakeShared<FAsdaNetSelfScope>(Net, kWho);
			if (Net) { Net->SelfLevel = 40; }
			const bool bItems = PickItems(W);
			if (Inv)
			{
				Inv->DebugPutItem(2, kCellOne, CodeSpend, 1);
				Inv->DebugPutItem(2, kCellStack, CodeSpend, 5);
				Inv->DebugPutItem(2, kCellTake, 0, 0);
				Inv->DebugPutItem(2, kCellBuy, 0, 0);
				Inv->DebugPutItem(1, 3, CodeGear, 1);
			}
			CapGen = AsdaSelfTestKit::ArmCapture(Net, [this](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
			{
				Sent.FindOrAdd(Op)++;
				TArray<uint8>& B = LastBody.FindOrAdd(Op); B.Reset(); B.Append(P, Len);
			}, 0, kWho);
			Expect(bItems && Inv && CapGen != 0, TEXT("П0 подготовка: предметы выбраны, перехват взведён"),
				FString::Printf(TEXT("расходник %d, снаряжение %d, перехват %u"), CodeSpend, CodeGear, CapGen));
			return 0.3f;
		});

		// ═══ Ш0. Воспроизведение (ключ RouteBySub 0): пустой 9908 → снова 9907 ═══
		Steps.Add([this](UWorld* W) -> float
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Exchange.RouteBySub"), 0);
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			Sent.Reset();
			if (Ex) { Ex->OpenMode(3); }
			Feed(W, 9908, TArray<uint8>());
			Feed(W, 9908, TArray<uint8>());
			return 0.4f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			Expect(Count(9907) >= 3 && !WinShown(W), TEXT("Ш0 воспроизведение (ключ 0): пустой 9908 шлёт 9907 снова, окна нет"),
				FString::Printf(TEXT("9907×%d, окно %d"), Count(9907), WinShown(W) ? 1 : 0));
			AsdaCVarSave::SetForTest(TEXT("asda.Exchange.RouteBySub"), 1);
			Calm(W);
			return 0.3f;
		});

		// ═══ Ж1/Ж2. «Мои предметы» при пустом списке ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			Sent.Reset();
			if (Ex) { Ex->OpenMode(3); }   // код NPC 30019
			Expect(Count(9907) == 1 && !WinShown(W), TEXT("Ж1 код 30019: ушёл ровно один 9907, окно ждёт ответа"),
				FString::Printf(TEXT("9907×%d"), Count(9907)));
			Feed(W, 9908, TArray<uint8>());   // ItemRelay: в категории лотов нет — тело 0 б
			return 0.6f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			Expect(Ex && WinShown(W) && Ex->GetMode() == EAsdaExchangeMode::MyLots && MyPanelShown(Ex) && MyCount(Ex) == 0 && Count(9907) == 1,
				TEXT("Ж1/Ж2 пустой 9908 открывает окно «мои предметы», повторного 9907 нет"),
				FString::Printf(TEXT("окно %d, режим %d, панель 13200 %d, лотов %d, 9907×%d"), WinShown(W) ? 1 : 0,
					Ex ? (int32)Ex->GetMode() : -1, Ex && MyPanelShown(Ex) ? 1 : 0, Ex ? MyCount(Ex) : -1, Count(9907)));
			Calm(W);
			Shot(TEXT("1_mylots_empty"));
			return 0.6f;
		});

		// ═══ Ж8. Вкладки «расходники»/«снаряжение» ═══
		Steps.Add([this](UWorld* W) -> float
		{
			TArray<uint8> B; AddMyLot(B, 7001, CodeSpend, 3, 1500);
			Feed(W, 9908, B);
			return 0.4f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			Expect(Ex && MyCount(Ex) == 1, TEXT("Ж8 вкладка «расходники»: 1 лот"), FString::Printf(TEXT("лотов %d"), Ex ? MyCount(Ex) : -1));
			Calm(W);
			Sent.Reset();
			Click(W, Ex && Ex->MyCatBtns.IsValidIndex(1) ? Ex->MyCatBtns[1].Get() : nullptr, TEXT("вкладка 13260 «снаряжение»"));
			return 0.5f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			const TArray<uint8>* B = LastBody.Find(9907);
			Expect(Count(9907) == 1 && B && B->Num() >= 16 && (*B)[15] == 2, TEXT("Ж8 щелчок по вкладке «снаряжение» — 9907 с категорией 2"),
				FString::Printf(TEXT("9907×%d, категория %d"), Count(9907), (B && B->Num() >= 16) ? (int32)(*B)[15] : -1));
			Feed(W, 9908, TArray<uint8>());
			return 0.4f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			Expect(Ex && MyCount(Ex) == 0 && WinShown(W) && Count(9907) == 1, TEXT("Ж8 пустой ответ вкладки «снаряжение» очищает список (лоты «расходников» не остаются)"),
				FString::Printf(TEXT("лотов %d, окно %d, 9907×%d"), Ex ? MyCount(Ex) : -1, WinShown(W) ? 1 : 0, Count(9907)));
			Calm(W);
			Shot(TEXT("8_gear_tab_empty"));
			Click(W, Ex && Ex->MyCatBtns.IsValidIndex(0) ? Ex->MyCatBtns[0].Get() : nullptr, TEXT("вкладка 13260 «расходники»"));
			return 0.4f;
		});

		// ═══ Ж5 (мои лоты). Три лота, «выбрать всё» ═══
		Steps.Add([this](UWorld* W) -> float
		{
			TArray<uint8> B;
			AddMyLot(B, 7001, CodeSpend, 1, 1000);
			AddMyLot(B, 7002, CodeSpend, 2, 2000);
			AddMyLot(B, 7003, CodeSpend, 3, 3000);
			Feed(W, 9908, B);
			return 0.5f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			Calm(W);
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			Click(W, Ex ? Ex->ChkMyAll.Get() : nullptr, TEXT("«выбрать всё» 13479"));
			return 0.4f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			int32 Checked = 0; if (Ex) { for (const FAsdaExchangeMyLot& L : Ex->MyLots) { Checked += (!L.IsEmpty() && L.bChecked) ? 1 : 0; } }
			Expect(Ex && MyCount(Ex) == 3 && Checked == 3 && Ex->bMyAll, TEXT("Ж5 «выбрать всё» в «моих лотах» отмечает все 3 строки"),
				FString::Printf(TEXT("лотов %d, отмечено %d, флаг %d"), Ex ? MyCount(Ex) : -1, Checked, Ex && Ex->bMyAll ? 1 : 0));
			Shot(TEXT("5_mylots_select_all"));
			Click(W, Ex && Ex->MyChecks.IsValidIndex(1) ? Ex->MyChecks[1].Get() : nullptr, TEXT("галочка строки 2"));
			return 0.4f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			Expect(Ex && !Ex->bMyAll && Ex->MyLots.IsValidIndex(1) && !Ex->MyLots[1].bChecked && Ex->MyLots[0].bChecked,
				TEXT("Ж5 снятие одной строки гасит «выбрать всё», остальные отметки остаются"),
				FString::Printf(TEXT("флаг %d"), Ex && Ex->bMyAll ? 1 : 0));
			return 0.2f;
		});

		// ═══ Ж4/Ж9. Забор: строка 1 (серийник 7001) → «Снять» → 9909 → 9910 кладёт вещь в ячейку 42 ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			if (Ex) { for (FAsdaExchangeMyLot& L : Ex->MyLots) { L.bChecked = (L.Serial == 7001); } Ex->RefreshMyLots(); }
			Sent.Reset();
			Click(W, Ex ? Ex->BtnMyDel.Get() : nullptr, TEXT("кнопка «Снять» 13251"));
			return 0.4f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			Expect(Count(9909) == 1, TEXT("Ж4 «Снять» отправил 9909"), FString::Printf(TEXT("9909×%d"), Count(9909)));
			Sent.Reset();
			Feed(W, 9910, LotBack(61, 7001, CodeSpend, 1, 2, kCellTake));
			return 0.5f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			bool bGone = Ex != nullptr; if (Ex) { for (const FAsdaExchangeMyLot& L : Ex->MyLots) { if (L.Serial == 7001) { bGone = false; } } }
			Expect(CellCode(W, 2, kCellTake) == CodeSpend && CellAmount(W, 2, kCellTake) == 1 && bGone && Ex && MyCount(Ex) == 2 && Count(9907) == 0,
				TEXT("Ж4/Ж9 забор 9910 кладёт вещь в ячейку сервера, лот уходит из списка без нового 9907"),
				FString::Printf(TEXT("ячейка 2/%d: код %d ×%d, лотов %d, 9907×%d"), kCellTake, CellCode(W, 2, kCellTake), CellAmount(W, 2, kCellTake),
					Ex ? MyCount(Ex) : -1, Count(9907)));
			Calm(W);
			Shot(TEXT("4_taken_back"));
			if (Ex) { Ex->CloseAll(); }
			return 0.4f;
		});

		// ═══ Ж6. Выставление из инвентаря: вещь ×1 щёлкается дважды ═══
		Steps.Add([this](UWorld* W) -> float
		{
			Calm(W);
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			if (Ex) { for (FAsdaExchangeRegRow& R : Ex->RegRows) { R.Clear(); } Ex->OpenMode(1); }   // код NPC 30018
			return 0.6f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			Expect(Ex && Ex->SellCells.Contains(kCellTake), TEXT("Ж4 забранная вещь (ячейка 42) снова в сетке выставления"),
				FString::Printf(TEXT("ячеек в сетке %d"), Ex ? Ex->SellCells.Num() : -1));
			Calm(W);
			Click(W, Ex ? SellIconFor(Ex, kCellOne) : nullptr, TEXT("ячейка сетки с вещью ×1 (1-й щелчок)"));
			return 0.4f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			Click(W, Ex ? SellIconFor(Ex, kCellOne) : nullptr, TEXT("ячейка сетки с вещью ×1 (2-й щелчок)"));
			return 0.4f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			const int32 Gi = Ex ? Ex->SellCells.IndexOfByKey(kCellOne) : -1;
			Expect(Ex && RegCount(Ex) == 1 && Gi >= 0 && Ex->SellCellCode(Gi) == 0,
				TEXT("Ж6 вещь ×1: второй щелчок строку не добавляет, ячейка в сетке пуста"),
				FString::Printf(TEXT("строк лота %d, код ячейки в сетке %d"), Ex ? RegCount(Ex) : -1, (Ex && Gi >= 0) ? Ex->SellCellCode(Gi) : -1));
			Shot(TEXT("6_single_qty_once"));
			Click(W, Ex ? SellIconFor(Ex, kCellTake) : nullptr, TEXT("ячейка сетки с забранной вещью"));
			return 0.4f;
		});
		// Ж5 (выставление): две строки, «выбрать всё» 13478.
		Steps.Add([this](UWorld* W) -> float
		{
			Calm(W);
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			Click(W, Ex ? Ex->ChkRegAll.Get() : nullptr, TEXT("«выбрать всё» 13478"));
			return 0.4f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			int32 Checked = 0; if (Ex) { for (const FAsdaExchangeRegRow& R : Ex->RegRows) { Checked += (!R.IsEmpty() && R.bChecked) ? 1 : 0; } }
			Expect(Ex && RegCount(Ex) == 2 && Checked == 2 && Ex->bRegAll, TEXT("Ж5 «выбрать всё» в окне выбора отмечает обе строки лота"),
				FString::Printf(TEXT("строк %d, отмечено %d"), Ex ? RegCount(Ex) : -1, Checked));
			Shot(TEXT("5_register_select_all"));
			return 0.3f;
		});

		// ═══ Ж7. Ответ 9902: вещь ×1 ушла (ячейка 40 пуста), стопка ×5 — остаток 3 ═══
		Steps.Add([this](UWorld* W) -> float
		{
			TArray<uint8> B; B.SetNumZeroed(67 * 2);
			auto Rec = [&B](int32 I, int32 Code, int32 Cell, int32 A, int32 Bv)
			{
				uint8* P = B.GetData() + 67 * I;
				P[0] = 0; AsdaBytes::WrI32(P + 1, Code); P[5] = 2; AsdaBytes::WrI16(P + 6, Cell);
				AsdaBytes::WrI32(P + 20, A); AsdaBytes::WrI32(P + 24, Bv); AsdaBytes::WrI32(P + 51, 10); AsdaBytes::WrI32(P + 55, 98765);
			};
			Rec(0, CodeSpend, kCellOne, 1, 1);
			Rec(1, CodeSpend, kCellStack, 2, 5);
			Feed(W, 9902, B);
			return 0.5f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			const UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(W);
			Expect(CellCode(W, 2, kCellOne) <= 0 && CellAmount(W, 2, kCellStack) == 3 && Inv && Inv->GetGold() == 98765,
				TEXT("Ж7 выставление 9902 забирает вещь из сумки (×1 — ячейка пуста, стопка ×5 → остаток 3), золото после комиссии"),
				FString::Printf(TEXT("ячейка 40: код %d; ячейка 41: ×%d; золото %d"), CellCode(W, 2, kCellOne), CellAmount(W, 2, kCellStack),
					Inv ? Inv->GetGold() : -1));
			Calm(W);
			if (UAsdaInventorySubsystem* InvM = Sub<UAsdaInventorySubsystem>(W)) { InvM->OpenWindow(); }
			return 0.6f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			Shot(TEXT("7_bag_after_register"));
			if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(W)) { Inv->CloseWindow(); }
			return 0.4f;
		});

		// ═══ Ж3. Поиск: страница с 2 лотами, затем пустая страница ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			Sent.Reset();
			if (Ex) { Ex->OpenMode(4); }   // код NPC 30021
			TArray<uint8> B; AddListing(B, 9001, CodeSpend, 1111); AddListing(B, 9002, CodeGear, 2222);
			Feed(W, 9904, B);
			return 0.6f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			Expect(Count(9903) >= 1 && Ex && LotCount(Ex) == 2, TEXT("Ж3 поиск 9903 ушёл, страница 9904 показывает 2 лота"),
				FString::Printf(TEXT("9903×%d, лотов %d"), Count(9903), Ex ? LotCount(Ex) : -1));
			// ★ Ж3 (RU): подписи вкладок 6/7 набора 13409 — вкладка 6 ищет зелья (9903 вид 11/подтип 1), 7 — ремесло (11/3).
			//   Ведомость gametext_overrides 12533/12534 (09.11): прежде RU «Ремесло»/«Прочее» стояли на зельях/ремесле.
			if (Ex && Ex->CatTexts2.IsValidIndex(6) && Ex->CatTexts2[5] && Ex->CatTexts2[6])
			{
				const FString T5 = Ex->CatTexts2[5]->GetText().ToString(), T6 = Ex->CatTexts2[6]->GetText().ToString();
				const bool bRu = FAsdaLang::IsRu();
				Expect(bRu ? (T5 == TEXT("Зелье") && T6 == TEXT("Ремесло")) : (T5 == TEXT("Potion") && T6 == TEXT("Crafting")),
					TEXT("Ж3 подписи вкладок 6/7 режима 4: зелья и ремесло на своих кнопках"), FString::Printf(TEXT("«%s» / «%s»"), *T5, *T6));
				Ex->OnCategoryPicked(4, 5);
				uint8 Crit[8]; Ex->BuildSearchCriteria(Crit);
				Expect(Crit[0] == 11 && Crit[1] == 1, TEXT("Ж3 вкладка «Зелье» ищет корзину 11/1 (Potion)"), FString::Printf(TEXT("%d/%d"), Crit[0], Crit[1]));
				Ex->OnCategoryPicked(4, 6);
				Ex->BuildSearchCriteria(Crit);
				Expect(Crit[0] == 11 && Crit[1] == 3, TEXT("Ж3 вкладка «Ремесло» ищет корзину 11/3 (Minig)"), FString::Printf(TEXT("%d/%d"), Crit[0], Crit[1]));
				TArray<uint8> B2; AddListing(B2, 9001, CodeSpend, 1111); AddListing(B2, 9002, CodeGear, 2222);
				Feed(W, 9904, B2);
				Shot(TEXT("3_tabs_potion_crafting"));
			}
			else { Expect(false, TEXT("Ж3 подписи вкладок 13409 найдены")); }
			Calm(W);
			Click(W, Ex ? Ex->ChkLotAll.Get() : nullptr, TEXT("«выбрать всё» 13482"));
			return 0.4f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			int32 Checked = 0; if (Ex) { for (const FAsdaExchangeListing& L : Ex->Listings) { Checked += (!L.IsEmpty() && L.bChecked) ? 1 : 0; } }
			Expect(Ex && Checked == 2 && Ex->bLotAll, TEXT("Ж5 «выбрать всё» в выдаче отмечает оба лота"), FString::Printf(TEXT("отмечено %d"), Checked));
			Shot(TEXT("3_5_listing_select_all"));
			Feed(W, 9904, TArray<uint8>());
			return 0.4f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			Expect(Ex && LotCount(Ex) == 0 && !Ex->bLotAll, TEXT("Ж3 пустая страница 9904 очищает выдачу и галочку «всё»"),
				FString::Printf(TEXT("лотов %d"), Ex ? LotCount(Ex) : -1));
			// Покупка 9906 (режим 4): вещь кладётся в ячейку 43.
			Feed(W, 9906, LotBack(85, 9001, CodeSpend, 2, 2, kCellBuy));
			return 0.4f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			Expect(CellCode(W, 2, kCellBuy) == CodeSpend && CellAmount(W, 2, kCellBuy) == 2, TEXT("покупка 9906 кладёт вещь в сумку"),
				FString::Printf(TEXT("ячейка 2/%d: код %d ×%d"), kCellBuy, CellCode(W, 2, kCellBuy), CellAmount(W, 2, kCellBuy)));
			Calm(W);
			return 0.2f;
		});

		// ═══ Ж10. Рынок открыт (режим 4): склад, лавка, обмен ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaWarehouseSubsystem* Wh = Sub<UAsdaWarehouseSubsystem>(W);
			UAsdaSelfStoreSubsystem* Ss = Sub<UAsdaSelfStoreSubsystem>(W);
			UAsdaExchangeP2PSubsystem* X = Sub<UAsdaExchangeP2PSubsystem>(W);
			Sent.Reset();
			if (Wh) { Wh->OpenWindow(UAsdaWarehouseSubsystem::kModeTake); }
			if (Ss) { Ss->DispatchPrivateShop(TEXT("самотест рынка")); }
			if (X) { X->OnMenuCommand(false, 321, 321, TEXT("Peer")); }
			TArray<uint8> Req; Req.SetNumZeroed(24); Req[0] = 1; AsdaBytes::WrI16(Req.GetData() + 1, 77); Req[23] = 0;
			Feed(W, 5283, Req);
			return 0.6f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			const UAsdaWarehouseSubsystem* Wh = Sub<UAsdaWarehouseSubsystem>(W);
			const TArray<uint8>* Ack = LastBody.Find(5284);
			Expect(Wh && !Wh->IsOpen(), TEXT("Ж10 при открытом рынке склад не открывается"));
			Expect(Count(6300) == 0, TEXT("Ж10 при открытом рынке личная лавка (6300) не готовится"), FString::Printf(TEXT("6300×%d"), Count(6300)));
			Expect(Count(5282) == 0, TEXT("Ж10 при открытом рынке своя заявка обмена 5282 не уходит"), FString::Printf(TEXT("5282×%d"), Count(5282)));
			Expect(Count(5284) == 1 && Ack && Ack->Num() >= 1 && (*Ack)[0] == 3, TEXT("Ж10 входящая заявка 5283 — авто-ответ «занят» 5284/3"),
				FString::Printf(TEXT("5284×%d, подкод %d"), Count(5284), (Ack && Ack->Num() >= 1) ? (int32)(*Ack)[0] : -1));
			Calm(W);
			Shot(TEXT("10_market_blocks_windows"));
			if (UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W)) { Ex->CloseAll(); }
			return 0.4f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			Calm(W);
			UAsdaWarehouseSubsystem* Wh = Sub<UAsdaWarehouseSubsystem>(W);
			if (Wh) { Wh->OpenWindow(UAsdaWarehouseSubsystem::kModeTake); }
			UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W);
			const bool bOpened = Ex && Ex->OpenMode(4);
			Expect(Wh && Wh->IsOpen() && !bOpened && !WinShown(W), TEXT("Ж10 обратно: при открытом складе рынок не открывается"),
				FString::Printf(TEXT("склад %d, рынок открыт %d"), Wh && Wh->IsOpen() ? 1 : 0, bOpened ? 1 : 0));
			if (Wh) { Wh->CloseWindow(); }
			return 0.4f;
		});

		// ═══ Уборка ═══
		Steps.Add([this](UWorld* W) -> float
		{
			Calm(W);
			if (UAsdaExchangeSubsystem* Ex = Sub<UAsdaExchangeSubsystem>(W)) { if (Ex->IsOpen()) { Ex->CloseAll(); } for (FAsdaExchangeRegRow& R : Ex->RegRows) { R.Clear(); } }
			if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(W))
			{
				for (const int32 C : { kCellOne, kCellStack, kCellTake, kCellBuy }) { Inv->DebugPutItem(2, C, 0, 0); }
				Inv->DebugPutItem(1, 3, 0, 0);
			}
			AsdaSelfTestKit::DisarmCapture(Sub<UAsdaNetSubsystem>(W), CapGen);
			if (SelfScope) { SelfScope->Restore(); }
			return 0.2f;
		});
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaMarketSelfTest(
		TEXT("asda.Exchange.MarketSelfTest"),
		TEXT("asda.Exchange.MarketSelfTest [delay=N] [quit] — ОФЛАЙН-самотест пачки 20: рынок (аукцион 13000), десять жалоб владельца, кадры RU/EN."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TCHAR* Who = FAsdaMarketSelfTest::kWho;
			AsdaCVarSave::ArmWorldGuard(W, Who);
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(W, Who, KitArgs)) { return; }
			float Delay = 0.f;
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (был отдельный цикл по аргументам с одной этой веткой)
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit, Who](float) -> bool
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(W2, Who, bQuit)) { return false; }
				TSharedRef<FAsdaMarketSelfTest> T = MakeShared<FAsdaMarketSelfTest>();
				T->Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
				T->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch20/market")));
				T->BuildSteps();
				TSharedRef<int32> Idx = MakeShared<int32>(0);
				TSharedRef<double> Next = MakeShared<double>(0.0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, Next, bQuit, Who](float) -> bool
				{
					UWorld* W3 = WW.Get();
					if (!W3) { AsdaCVarSave::RestoreAll(Who); return false; }
					const double Now = FPlatformTime::Seconds();
					if (Now < *Next) { return true; }
					// ★ 11.11.2026 (D4b): следующий шаг не начинается, пока заказанный прошлым шагом щелчок не доставлен (нажатие,
					//   удержание, отпускание идут по кадрам). Под нагрузкой фиксированной паузы шага не хватало: проверка читала
					//   состояние до щелчка, а следующий заказ щелчка затирал недоставленный. Потолок ожидания — 5 с сверх паузы.
					if (const UAsdaWindowSubsystem* ClickWm = W3->GetGameInstance() ? W3->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
					{
						if (ClickWm->IsSimClickPending() && Now < *Next + 5.0) { return true; }
					}
					if (*Idx < T->Steps.Num()) { *Next = Now + T->Steps[(*Idx)++](W3); return true; }
					AsdaCVarSave::RestoreAll(Who);
					UE_LOG(LogAsdaMarketTest, Log, TEXT("САМОТЕСТ-РЫНОК ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaMarketTest, Log, TEXT("САМОТЕСТ-РЫНОК: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
