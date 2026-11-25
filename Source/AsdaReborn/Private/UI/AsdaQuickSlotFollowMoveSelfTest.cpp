// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «СЛОТ ПАНЕЛИ СЛЕДУЕТ ЗА ПРЕДМЕТОМ» (07.11.2026): `asda.SelfTest.QuickSlotFollowMove [quit]`. [ИСПРАВЛЕНИЕ]
//   Жалоба владельца: «перенос предмета в сумке снимает его с панели или подменяет соседом». Эталон `itemMoveApply@0xD607C0`
//   перепривязывает слоты панели до применения ячеек, порт этот шаг потерял. Ключ `asda.Quick.FollowBagMove` (0 = прежнее поведение).
//   Без сети (-AsdaNoNet), карта мира. Предметы кладутся штатным батчем, в слот — `AssignToHotbarSlot` (точка броска мышью), ответ
//   сервера 5001 подаётся разбором пакета `DebugFeedPacket`, склад — штатным применением ответа 5009.
//   Случаи: переезд; ячейки 30..59; обмен (на панели оба и один); стопка расходников; две ссылки на одну ячейку; спецпредмет (маунт);
//   надеть, смена оружия обменом, своп слотов оружия с надетым, снять; отказ сервера; склад целиком и частично; перенос внутри склада;
//   цепочка переносов автосортировки; перезаход (перехваченный 5067 возвращается как 5059); ключ 0 — старый дефект.
//   Итог — «СЛЕДОМ-ЗА-ПРЕДМЕТОМ ИТОГ: ok=… fail=…». Слоты и ячейки в конце возвращаются пустыми.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFollowMoveTest, Log, All);

namespace AsdaFollowMoveTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.QuickSlotFollowMove");   // имя команды самотеста
	static constexpr int32 kCells = 60;                                      // ёмкость сумки и расходников (P96)
	static constexpr int32 kWeaponBody = 9;                                  // слот оружия на теле (контейнер 3)
	static constexpr int32 kWeaponColP1 = 12;                                // колонка оружия страницы 1
	static constexpr int32 kWeaponColP2 = 25;                                // колонка оружия страницы 2
	static constexpr int32 kTRowSlot = 66;                                   // слот T-ряда — вторая ссылка на ячейку
	static constexpr int32 kPackedPage = 131;                                // тело страницы 5059: страница + 13×10
}

struct FAsdaQuickSlotFollowMoveSelfTest
{
	struct FRun
	{
		TWeakObjectPtr<UWorld> W;                 // мир прогона
		bool bQuit = false;                       // выйти после прогона
		int32 Ok = 0, Fail = 0, Waits = 0;        // счётчики проверок и ожидания
		int32 W1 = 0, W2 = 0, W3 = 0;             // оружие (itemType 9)
		int32 Mount = 0;                          // спецпредмет-маунт, который панель принимает
		int32 P1 = 0, P2 = 0;                     // расходники
		int32 Step = 0;                           // номер шага
		int32 Sent5067 = 0;                       // сколько раскладок ушло
		TArray<uint8> LastPage;                   // тело последнего 5067 (131 б)
		uint32 CapGen = 0;                        // перехват отправок
		TUniquePtr<FAsdaCVarScope> Keys;          // ключи на время прогона
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaFollowMoveTest, Log, TEXT("СЛЕДОМ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaFollowMoveTest, Error, TEXT("СЛЕДОМ FAIL %s"), *What); }
		}
	};
	using FRef = TSharedRef<FRun>;
	using FStep = TFunction<void(const FRef&)>;   // шаг прогона; между шагами идёт тик, в котором применяются очереди пакетов

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
	// Ответ сервера 5001: result, sub (1 обмен, 2 переезд), источник, цель — смещения `ApplyMoveResult`.
	static void Feed5001(const FRef& R, int32 Result, int32 SubKind, int32 SrcType, int32 SrcSlot, int32 DstType, int32 DstSlot)
	{
		TArray<uint8> B; B.SetNumZeroed(24);
		B[0] = (uint8)Result; B[1] = (uint8)SubKind;
		B[2] = (uint8)(SrcSlot & 0xFF); B[3] = (uint8)((SrcSlot >> 8) & 0xFF); B[6] = (uint8)SrcType;
		B[13] = (uint8)(DstSlot & 0xFF); B[14] = (uint8)((DstSlot >> 8) & 0xFF); B[17] = (uint8)DstType;
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R)) { Net->DebugFeedPacket(5001, B); }
	}
	// Ответ склада 5009 по одной ячейке инвентаря: `NewAmount` < 0 — предмет ушёл целиком.
	static void Keep5009(const FRef& R, int32 Cont, int32 Cell, int32 NewAmount, int32 StoreSlot, int32 StoreAmount)
	{
		TArray<uint8> B; B.Init(0xFF, 113);
		B[0] = 1;
		auto Ref = [&B](int32 O, int32 Slot, int32 Ct, int32 Amount)
		{
			B[O] = (uint8)(Slot & 0xFF); B[O + 1] = (uint8)((Slot >> 8) & 0xFF); B[O + 2] = 0; B[O + 3] = 0; B[O + 4] = (uint8)Ct;
			FMemory::Memcpy(B.GetData() + O + 5, &Amount, 4); B[O + 9] = 0; B[O + 10] = 0;
		};
		Ref(1, Cell, Cont, NewAmount);
		Ref(56, StoreSlot, 5, StoreAmount);
		if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R)) { Inv->ApplyWarehouseKeepResult5009(B); }
	}
	// Слот `Abs` ссылается на (контейнер, ячейку), держит код `Code` и его иконку; Count < 0 — количество не сверяем.
	static void ExpectSlot(const FRef& R, const TCHAR* What, int32 Abs, int32 Cont, int32 Cell, int32 Code, int32 Count = -1)
	{
		UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
		UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
		if (!Hb || !IT) { return; }
		const FAsdaHotbarSlot& S = Hb->GetSlot(Abs);
		const FAsdaItemIcon Ic = IT->GetItemIcon(Code, true);
		const bool bOk = S.Type == 1 && S.SubType == Cont && S.Index == Cell && S.Id == (int32)(uint16)Code
			&& S.IconAtlas == Ic.Atlas && S.IconX == Ic.CellX && S.IconY == Ic.CellY && (Count < 0 || S.Count == Count);
		R->Expect(bOk, FString::Printf(TEXT("%s: слот %d — тип %d, %d:%d, код %d ×%d, иконка %s (%d,%d); ожидание %d:%d, код %d ×%d, иконка %s (%d,%d)"),
			What, Abs, S.Type, S.SubType, S.Index, S.Id, S.Count, *S.IconAtlas, S.IconX, S.IconY, Cont, Cell, Code, Count, *Ic.Atlas, Ic.CellX, Ic.CellY));
	}
	static void ExpectEmpty(const FRef& R, const TCHAR* What, int32 Abs)
	{
		UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
		if (!Hb) { return; }
		R->Expect(Hb->GetSlot(Abs).IsEmpty(), FString::Printf(TEXT("%s: слот %d пуст (тип %d)"), What, Abs, Hb->GetSlot(Abs).Type));
	}
	// Чистая раскладка шага: панель пуста, сумка 0=W1 1=W2 2=маунт 40=W3, расходники 5=P1×7 6=P2×9, рука пуста.
	static void Fixture(const FRef& R)
	{
		using namespace AsdaFollowMoveTestLocal;
		UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
		UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
		if (!Hb || !Inv) { return; }
		for (int32 I = 0; I < UAsdaHotbarSubsystem::kSlotCount; ++I) { if (!Hb->GetSlot(I).IsEmpty()) { Hb->ClearSlot(I); } }
		for (int32 C = 0; C < kCells; ++C) { Inv->DebugPutItem(1, C, 0, 0); Inv->DebugPutItem(2, C, 0, 0); }
		Inv->DebugPutItem(3, kWeaponBody, 0, 0);
		Inv->DebugPutItem(1, 0, R->W1, 1); Inv->DebugPutItem(1, 1, R->W2, 1); Inv->DebugPutItem(1, 2, R->Mount, 1); Inv->DebugPutItem(1, 40, R->W3, 1);
		Inv->DebugPutItem(2, 5, R->P1, 7); Inv->DebugPutItem(2, 6, R->P2, 9);
	}
	static void Assign(const FRef& R, int32 Cont, int32 Cell, int32 Abs)
	{
		UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
		R->Expect(Inv && Inv->AssignToHotbarSlot(Cont, Cell, Abs), FString::Printf(TEXT("бросок %d:%d → слот %d"), Cont, Cell, Abs));
	}

	static void Start(UWorld* W, bool bQuit);
	static void WaitReady(const FRef& R);
	static void Prepare(const FRef& R);
	static void Run(const FRef& R);
	static TArray<FStep> Steps();
	static void Finish(const FRef& R);
};

void FAsdaQuickSlotFollowMoveSelfTest::Start(UWorld* W, bool bQuit)
{
	using namespace AsdaFollowMoveTestLocal;
	FRef R = MakeShared<FRun>();
	R->W = W; R->bQuit = bQuit;
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("нет сетевой подсистемы")); return; }
	if (Net->IsConnected()) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("живое соединение")); return; }
	if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
	{
		GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
		GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
	}
	WaitReady(R);
}

// Панель и мосты создаёт тик мира; контейнеры помечаются полученными штатным маркером конца батча.
void FAsdaQuickSlotFollowMoveSelfTest::WaitReady(const FRef& R)
{
	using namespace AsdaFollowMoveTestLocal;
	UWorld* W = R->W.Get();
	if (!W) { return; }
	UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
	UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
	if (!Hb || !Inv || !Hb->bSkillBridgeBound)
	{
		if (++R->Waits > 40) { AsdaSelfTestKit::Refused(W, kCmd, R->bQuit, TEXT("нет панели/инвентаря за 20 с")); return; }
		After(R, 0.5f, [](const FRef& R) { WaitReady(R); });
		return;
	}
	if (R->Waits >= 0)
	{
		R->Waits = -1;
		for (int32 Cont = 1; Cont <= 3; ++Cont) { Inv->SimulateBatch(Cont, TArray<uint8>()); }
		After(R, 0.5f, [](const FRef& R) { WaitReady(R); });
		return;
	}
	Prepare(R);
}

void FAsdaQuickSlotFollowMoveSelfTest::Prepare(const FRef& R)
{
	using namespace AsdaFollowMoveTestLocal;
	UWorld* W = R->W.Get();
	UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
	UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(R);
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	if (!IT || !WS || !Net) { AsdaSelfTestKit::Refused(W, kCmd, R->bQuit, TEXT("нет таблиц/мира/сети")); return; }
	// Подбор предметов: разные иконки, чтобы подмена соседом была видна и в иконке.
	TSet<FString> Cells;
	auto Fresh = [&](int32 Code) -> bool
	{
		const FAsdaItemIcon Ic = IT->GetItemIcon(Code, true);
		if (!Ic.IsValid()) { return false; }
		const FString Key = FString::Printf(TEXT("%s#%d"), *Ic.Atlas, Ic.Cell);
		if (Cells.Contains(Key)) { return false; }
		Cells.Add(Key);
		return true;
	};
	for (int32 Code = 1; Code <= 100000; ++Code)
	{
		const FAsdaItemRecord* Rec = IT->GetItem(Code);
		if (Rec && !IT->IsSowelCode(Code) && !IT->IsSpecialItemCode(Code) && Code != 20551 && Rec->FlagWord176 == 0)
		{
			if (Rec->ItemType == 9 && !R->W3 && Fresh(Code)) { (!R->W1 ? R->W1 : !R->W2 ? R->W2 : R->W3) = Code; }
			if (Rec->ItemType == 11 && Rec->ItemSubType != 34 && !(Rec->ItemSubType >= 6 && Rec->ItemSubType <= 12) && !R->P2 && Fresh(Code)) { (!R->P1 ? R->P1 : R->P2) = Code; }
		}
		FAsdaSpecialItemlistRow Sp;
		if (!R->Mount && Code >= 559 && Code <= 637 && IT->GetSpecialItem(Code, Sp) && Sp.SubType == 15 && Fresh(Code)) { R->Mount = Code; }
	}
	UE_LOG(LogAsdaFollowMoveTest, Log, TEXT("СЛЕДОМ: оружие %d %d %d, маунт %d, расходники %d %d"), R->W1, R->W2, R->W3, R->Mount, R->P1, R->P2);
	if (!R->W3 || !R->Mount || !R->P2) { AsdaSelfTestKit::Refused(W, kCmd, R->bQuit, TEXT("не подобраны предметы")); return; }
	R->Keys = MakeUnique<FAsdaCVarScope>(W, kCmd);
	R->Keys->Set(TEXT("asda.Quick.FollowBagMove"), 1);
	TWeakPtr<FRun> WR = R;
	R->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WR](uint16 Op, const uint8* Data, int32 Len, bool, TCHAR)
	{
		TSharedPtr<FRun> Run = WR.Pin();
		if (!Run || Op != 5067) { return; }
		++Run->Sent5067;
		if (Data && Len >= kPackedPage) { Run->LastPage = TArray<uint8>(Data + Len - kPackedPage, kPackedPage); }
	}, 0, kCmd);
	if (R->CapGen == 0) { AsdaSelfTestKit::Refused(W, kCmd, R->bQuit, TEXT("перехват отправок занят")); return; }
	Run(R);
}

TArray<FAsdaQuickSlotFollowMoveSelfTest::FStep> FAsdaQuickSlotFollowMoveSelfTest::Steps()
{
	using namespace AsdaFollowMoveTestLocal;
	TArray<FStep> S;
	// 1. Переезд в пустую ячейку, слот не снят, раскладка ушла.
	S.Add([](const FRef& R) { Fixture(R); Assign(R, 1, 0, 0); R->Sent5067 = 0; Feed5001(R, 1, 2, 1, 0, 1, 10); });
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("переезд"), 0, 1, 10, R->W1); R->Expect(R->Sent5067 == 1, FString::Printf(TEXT("переезд: 5067 ушёл один раз (%d)"), R->Sent5067)); });
	// 2. Правая половина сумки 60.
	S.Add([](const FRef& R) { Fixture(R); Assign(R, 1, 40, 1); Assign(R, 1, 1, 2); Feed5001(R, 1, 2, 1, 40, 1, 59); Feed5001(R, 1, 2, 1, 1, 1, 33); });
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("ячейки 30..59"), 1, 1, 59, R->W3); ExpectSlot(R, TEXT("ячейки 30..59"), 2, 1, 33, R->W2); });
	// 3. Обмен: оба на панели.
	S.Add([](const FRef& R) { Fixture(R); Assign(R, 1, 0, 0); Assign(R, 1, 1, 1); Feed5001(R, 1, 1, 1, 0, 1, 1); });
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("обмен, оба"), 0, 1, 1, R->W1); ExpectSlot(R, TEXT("обмен, оба"), 1, 1, 0, R->W2); });
	// 4. Обмен: на панели только один.
	S.Add([](const FRef& R) { Fixture(R); Assign(R, 1, 0, 0); Feed5001(R, 1, 1, 1, 0, 1, 1); });
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("обмен, один"), 0, 1, 1, R->W1); ExpectEmpty(R, TEXT("обмен, один"), 1); });
	// 5. Стопки расходников: переезд и обмен, количество едет с предметом.
	S.Add([](const FRef& R) { Fixture(R); Assign(R, 2, 5, 3); Assign(R, 2, 6, 4); Feed5001(R, 1, 2, 2, 5, 2, 20); });
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("стопка, переезд"), 3, 2, 20, R->P1, 7); ExpectSlot(R, TEXT("стопка, сосед"), 4, 2, 6, R->P2, 9); Feed5001(R, 1, 1, 2, 20, 2, 6); });
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("стопка, обмен"), 3, 2, 6, R->P1, 7); ExpectSlot(R, TEXT("стопка, обмен"), 4, 2, 20, R->P2, 9); });
	// 6. Две ссылки на одну ячейку (основной ряд и T-ряд на разных страницах): обе следуют, ушли обе страницы.
	S.Add([](const FRef& R) { Fixture(R); Assign(R, 1, 0, 0); Assign(R, 1, 0, kTRowSlot); R->Sent5067 = 0; Feed5001(R, 1, 2, 1, 0, 1, 11); });
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("две ссылки"), 0, 1, 11, R->W1); ExpectSlot(R, TEXT("две ссылки"), kTRowSlot, 1, 11, R->W1); R->Expect(R->Sent5067 == 2, FString::Printf(TEXT("две ссылки: 5067 по числу страниц (%d)"), R->Sent5067)); });
	// 7. Спецпредмет.
	S.Add([](const FRef& R) { Fixture(R); Assign(R, 1, 2, 5); Feed5001(R, 1, 2, 1, 2, 1, 12); });
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("маунт"), 5, 1, 12, R->Mount); });
	// 8. Надеть, смена оружия обменом, своп слотов оружия, снять.
	S.Add([](const FRef& R) { Fixture(R); Assign(R, 1, 0, kWeaponColP1); Assign(R, 1, 1, kWeaponColP2); Feed5001(R, 1, 2, 1, 0, 3, kWeaponBody); });
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("надеть"), kWeaponColP1, 3, kWeaponBody, R->W1); ExpectSlot(R, TEXT("надеть, сосед"), kWeaponColP2, 1, 1, R->W2); Feed5001(R, 1, 1, 1, 1, 3, kWeaponBody); });
	S.Add([](const FRef& R)
	{
		ExpectSlot(R, TEXT("смена оружия"), kWeaponColP1, 1, 1, R->W1); ExpectSlot(R, TEXT("смена оружия"), kWeaponColP2, 3, kWeaponBody, R->W2);
		if (UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R)) { Hb->SwapSlots(kWeaponColP1, kWeaponColP2); }
		ExpectSlot(R, TEXT("своп слотов оружия с надетым"), kWeaponColP1, 3, kWeaponBody, R->W2); ExpectSlot(R, TEXT("своп слотов оружия с надетым"), kWeaponColP2, 1, 1, R->W1);
		Feed5001(R, 1, 2, 3, kWeaponBody, 1, 30);
	});
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("снять"), kWeaponColP1, 1, 30, R->W2); });
	// 9. Отказ сервера: ничего не меняется.
	S.Add([](const FRef& R) { Fixture(R); Assign(R, 1, 0, 0); R->Sent5067 = 0; Feed5001(R, 18, 2, 1, 0, 1, 10); });
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("отказ 18"), 0, 1, 0, R->W1); R->Expect(R->Sent5067 == 0, TEXT("отказ 18: 5067 не ушёл")); });
	// 10. Склад: часть стопки — слот с новым количеством; целиком — слот снят, как `CClass_BuildItemRecord(1)`.
	S.Add([](const FRef& R) { Fixture(R); Assign(R, 2, 5, 3); Assign(R, 1, 1, 4); Keep5009(R, 2, 5, 4, 7, 3); Keep5009(R, 1, 1, -1, 8, 1); });
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("склад, часть стопки"), 3, 2, 5, R->P1, 4); ExpectEmpty(R, TEXT("склад, целиком"), 4); Feed5001(R, 1, 2, 5, 7, 5, 9); });
	// 11. Перенос внутри склада панели не касается.
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("перенос 5→5"), 3, 2, 5, R->P1, 4); });
	// 12. Цепочка автосортировки: W1 и W2 меняются через пустую ячейку.
	S.Add([](const FRef& R) { Fixture(R); Assign(R, 1, 0, 0); Assign(R, 1, 1, 1); Feed5001(R, 1, 2, 1, 0, 1, 20); Feed5001(R, 1, 2, 1, 1, 1, 0); Feed5001(R, 1, 2, 1, 20, 1, 1); });
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("цепочка"), 0, 1, 1, R->W1); ExpectSlot(R, TEXT("цепочка"), 1, 1, 0, R->W2); });
	// 13. Перезаход: сервер хранит то, что ушло 5067; его же и вернёт в 5059.
	S.Add([](const FRef& R) { Fixture(R); Assign(R, 1, 0, 0); R->LastPage.Reset(); Feed5001(R, 1, 2, 1, 0, 1, 15); });
	S.Add([](const FRef& R)
	{
		R->Expect(R->LastPage.Num() == kPackedPage && R->LastPage[0] == 0, FString::Printf(TEXT("перезаход: страница 0 перехвачена (%d б)"), R->LastPage.Num()));
		TArray<uint8> Empty; Empty.SetNumZeroed(kPackedPage);
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R)) { Net->DebugFeedPacket(5059, Empty); }
	});
	S.Add([](const FRef& R)
	{
		ExpectEmpty(R, TEXT("перезаход: панель стёрта"), 0);
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R)) { if (R->LastPage.Num() == kPackedPage) { Net->DebugFeedPacket(5059, R->LastPage); } }
	});
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("перезаход"), 0, 1, 15, R->W1); });
	// 14. Ключ 0 — старый дефект: переезд снимает слот, обмен подменяет соседом.
	S.Add([](const FRef& R) { R->Keys->Set(TEXT("asda.Quick.FollowBagMove"), 0); Fixture(R); Assign(R, 1, 0, 0); Assign(R, 1, 1, 1); Feed5001(R, 1, 2, 1, 0, 1, 10); });
	S.Add([](const FRef& R) { ExpectEmpty(R, TEXT("ключ 0, переезд: дефект"), 0); ExpectSlot(R, TEXT("ключ 0, сосед"), 1, 1, 1, R->W2); Fixture(R); Assign(R, 1, 0, 0); Feed5001(R, 1, 1, 1, 0, 1, 1); });
	S.Add([](const FRef& R) { ExpectSlot(R, TEXT("ключ 0, обмен: дефект, в слоте сосед"), 0, 1, 0, R->W2); R->Keys->Set(TEXT("asda.Quick.FollowBagMove"), 1); });
	return S;
}

// Шаги идут с паузой: очереди 5001 и 5059 применяет Tick.
void FAsdaQuickSlotFollowMoveSelfTest::Run(const FRef& R)
{
	const TArray<FStep> All = Steps();
	if (R->Step >= All.Num()) { Finish(R); return; }
	All[R->Step](R);
	++R->Step;
	After(R, 0.2f, [](const FRef& R) { Run(R); });
}

void FAsdaQuickSlotFollowMoveSelfTest::Finish(const FRef& R)
{
	using namespace AsdaFollowMoveTestLocal;
	UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(R);
	UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
	if (Hb) { for (int32 I = 0; I < UAsdaHotbarSubsystem::kSlotCount; ++I) { if (!Hb->GetSlot(I).IsEmpty()) { Hb->ClearSlot(I); } } }
	if (Inv)
	{
		for (int32 C = 0; C < kCells; ++C) { Inv->DebugPutItem(1, C, 0, 0); Inv->DebugPutItem(2, C, 0, 0); }
		Inv->DebugPutItem(3, kWeaponBody, 0, 0);
	}
	if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R)) { if (R->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, R->CapGen); } }
	if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R)) { WM->ClearMsgBoxQueue(TEXT("самотест QuickSlotFollowMove")); }
	if (R->Keys) { R->Keys->Restore(); }
	AsdaSelfTestKit::Finish(R->W.Get(), kCmd, R->bQuit, LogAsdaFollowMoveTest, TEXT("СЛЕДОМ-ЗА-ПРЕДМЕТОМ"), R->Ok, R->Fail);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestQuickSlotFollowMove(
	TEXT("asda.SelfTest.QuickSlotFollowMove"),
	TEXT("Самотест: слот панели следует за предметом при переносе (5001): переезд, обмен, стопки, ячейки 30..59, надеть/снять, смена оружия, "
	     "отказ, склад, автосортировка, перезаход 5067→5059; ключ asda.Quick.FollowBagMove 0 — старый дефект. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaFollowMoveTestLocal::kCmd, A)) { return; }
		if (!W) { return; }
		if (A.Delay <= 0.f) { FAsdaQuickSlotFollowMoveSelfTest::Start(W, A.bQuit); return; }
		TWeakObjectPtr<UWorld> WW = W;
		const bool bQuit = A.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, bQuit]()
		{
			if (WW.Get() && AsdaSelfTestKit::RefuseDeferred(WW.Get(), AsdaFollowMoveTestLocal::kCmd, bQuit)) { return; }
			if (UWorld* Wd = WW.Get()) { FAsdaQuickSlotFollowMoveSelfTest::Start(Wd, bQuit); }
		}), A.Delay, false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
