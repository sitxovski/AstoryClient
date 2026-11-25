// Copyright (c) 2026 @sitxovski (Discord)
// Реализация «Пожертвования» (кнопка NPC 30051) — весь реверс в шапке `Npc/AsdaDonationSubsystem.h`
//   и в отчёте `Reports/owner_1006/wave2B_hunt_donation.md` §3.
#include "Npc/AsdaDonationSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaWindowSubsystem.h"        // нумпад 1410 (`ShowNumPad` / `CloseNumPad`)
#include "UI/AsdaInventorySubsystem.h"     // CountItemById / ForEachItemStack / ApplyExternalRecord
#include "AsdaNetSubsystem.h"              // 6132 / 6133
#include "AsdaBytes.h"                     // RdI16 / RdI32

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDonation, Log, All);

// ★ [ПОРТ] Д3 — максимум нумпада = стопка, которая уйдёт в 6132 (обоснование — шапка .h). 0 — как оригинал (сумма).
static ASDA_DEV_CVAR(int32, CVarDonationStackMax,
	TEXT("asda.Npc.DonationStackMax"), 1,
	TEXT("1 — [ПОРТ] максимум нумпада пожертвования (30051) = размер ПОСЛЕДНЕЙ стопки 60073 — той, что уйдёт в 6132 ")
	TEXT("(сервер при кол-ве > стопки молчит); 0 — как оригинал: сумма всех стопок."),
	ECVF_Default);

using AsdaBytes::RdI16;
using AsdaBytes::RdI32;

void UAsdaDonationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HResult = Net->OnDonationResult.AddLambda([this](const TArray<uint8>& B) { PendingResults.Push(B); });
	}
}

void UAsdaDonationSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { Net->OnDonationResult.Remove(HResult); }
	Super::Deinitialize();
}

void UAsdaDonationSubsystem::ResetSessionState(EAsdaSessionResetReason /*Reason*/)
{
	PendingResults.Clear();
	bNumPadOwned = false;   // сам нумпад гасит подсистема окон своим сбросом
}

void UAsdaDonationSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.Donation");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPending();
}

// Порт `invFindItemById@0x999E50`: не-снаряжение — 60 ячеек контейнера 2, совпадение кода, БЕЗ `break` ⇒ последняя стопка.
//   Обход — общая точка инвентаря `ForEachItemStack` (тот же выбор контейнера, что у `CountItemById`).
bool UAsdaDonationSubsystem::FindLastStack(int32& OutContainer, int32& OutCell, int32& OutUnits) const
{
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return false; }
	bool bFound = false;
	Inv->ForEachItemStack(kItemCode, [&](int32 Container, int32 Cell, int32 Units)
	{
		OutContainer = Container; OutCell = Cell; OutUnits = Units; bFound = true;
	});
	return bFound;
}

int32 UAsdaDonationSubsystem::ComputeMax() const
{
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return 0; }
	const int32 Total = Inv->CountItemById(kItemCode);   // `CInventory_CountItemById(&g_InventoryBag, 60073)`
	if (CVarDonationStackMax.GetValueOnGameThread() == 0) { return Total; }
	int32 C = -1, Cell = -1, Units = 0;
	return FindLastStack(C, Cell, Units) ? FMath::Min(Total, Units) : Total;   // [ПОРТ] Д3
}

// `case 30051` (game_main.c:10217-10220): нумпад режима 6, максимум — счёт 60073, ПО ЦЕНТРУ (ui_core.c:61836-61841).
//   Ноль предметов эталон не отсекает: окно открывается с максимумом 0 (ввод режется до 0, отправки не будет).
bool UAsdaDonationSubsystem::OpenNumPad()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	const int32 Max = ComputeMax();
	TWeakObjectPtr<UAsdaDonationSubsystem> WeakThis(this);
	bNumPadOwned = true;
	UUserWidget* Dlg = WM->ShowNumPad(Max, [WeakThis](bool bOk, int32 Qty)
	{
		if (!WeakThis.IsValid()) { return; }
		WeakThis->bNumPadOwned = false;
		// ОК: `uiQtyDialog_sendUseItem60073` (`> 0` ⇒ 6132); отмена: `sub_DA6640` — только закрыть.
		if (bOk && Qty > 0) { WeakThis->OnNumPadAccepted(Qty); }
	}, /*bCenterOnScreen*/ true);
	if (!Dlg) { bNumPadOwned = false; }
	UE_LOG(LogAsdaDonation, Log, TEXT("[пожертвование] кнопка 30051 → нумпад режим 6 (по центру), максимум %d → %s"),
		Max, Dlg ? TEXT("открыт") : TEXT("НЕ открыт"));
	return Dlg != nullptr;
}

// `netSendGuarded_op6132_useItem60073@0xD9F680`.
bool UAsdaDonationSubsystem::OnNumPadAccepted(int32 Qty)
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Inv || !Net || Qty <= 0) { return false; }
	// Эталон: `if (CInventory_CountItemById(60073) < a2) return 0;` — проверка ОБЩЕГО счёта (после сборки тела).
	if (Inv->CountItemById(kItemCode) < Qty)
	{
		UE_LOG(LogAsdaDonation, Log, TEXT("[пожертвование] 6132 не ушёл: предметов %d < %d (эталон молчит)"), Inv->CountItemById(kItemCode), Qty);
		return false;
	}
	int32 Container = -1, Cell = -1, Units = 0;
	if (!FindLastStack(Container, Cell, Units)) { return false; }   // счёт > 0 ⇒ стопка есть; ветка — защита
	const FAsdaItemInstance* It = Inv->GetCell(Container, Cell);
	if (!It) { return false; }
	// Поля — из САМОЙ записи (`*(DWORD*)rec`, `rec+4`, `rec+5`).
	const uint8 Cont = (It->Raw.Num() > 6) ? It->Raw[4] : (uint8)It->ContainerType;
	const int32 Slot = (It->Raw.Num() > 6) ? (int32)RdI16(It->Raw.GetData() + 5) : It->SlotIndex;
	return Net->SendUserEventItemStack6132(It->ItemCode, Cont, Slot, Qty);
}

// Хвост `UI_resetAllWindowsAndLeave_5045`: `if (нумпад открыт) uiDialogClose(g_pNumPadDialog)` — колбэк режима НЕ зовётся.
//   Наш колбэк на «отмену» ничего не делает, поэтому `CloseNumPad(false)` эквивалентен.
bool UAsdaDonationSubsystem::CloseNumPadOnNpcLeave()
{
	if (!bNumPadOwned) { return false; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	bNumPadOwned = false;
	if (!WM || !WM->IsNumPadOpen()) { return false; }
	WM->CloseNumPad(false);
	UE_LOG(LogAsdaDonation, Log, TEXT("[пожертвование] уход от NPC — нумпад режима 6 закрыт без отправки (эталонный 5045)"));
	return true;
}

void UAsdaDonationSubsystem::DrainPending()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива. Прежний цикл `while … [0]; RemoveAt(0)` давал тот же порядок: во время разбора новые тела в очередь
	//   не попадают (колбэк сети приходит из `UAsdaNetSubsystem::Tick`, не отсюда).
	if (TArray<TArray<uint8>> Local; PendingResults.Drain(Local))
	{
		for (const TArray<uint8>& B : Local) { OnResult6133(B); }
	}
}

// `onRecvRefineSlotUpdate@0xD9F8B0` (network.c:24169-24192) — все ветки.
void UAsdaDonationSubsystem::OnResult6133(const TArray<uint8>& Body)
{
	if (Body.Num() < 66) { return; }
	const int32 Code = (int32)Body[4];
	if (Code != 1)
	{
		// Эталон: `if (a2[4] == 1) {…}` — иначе НИЧЕГО (ни нотиса, ни перерисовки).
		UE_LOG(LogAsdaDonation, Log, TEXT("[пожертвование] 6133 код %d — у эталона ветки нет (молча)"), Code);
		return;
	}
	// Кол-во ≠ 0 ⇒ `SlotTable_SetFieldById` (только кол-во, контейнеры 1/2, по слоту); 0 ⇒ `OnRecv_RemoveSlot` — это ровно
	//   режим `CountOrClear` общей точки инвентаря (ремесло). ⚠ Отрицательное кол-во эталон записал бы как есть, общая точка
	//   очищает ячейку — сервер отрицательных не шлёт (стопка − сданное > 0, иначе удаление).
	TArray<uint8> Rec; Rec.Append(Body.GetData() + 5, 57);
	if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->ApplyExternalRecord(Rec, UAsdaInventorySubsystem::EExternalRecordMode::CountOrClear);
	}
	UE_LOG(LogAsdaDonation, Log, TEXT("[пожертвование] 6133 успех: предмет %d контейнер %d слот %d → кол-во %d"),
		RdI32(Rec.GetData()), (int32)Rec[4], RdI16(Rec.GetData() + 5), RdI32(Rec.GetData() + 9));
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GDonationOpen(
	TEXT("asda.Npc.Donation"),
	TEXT("asda.Npc.Donation — открыть нумпад пожертвования так же, как кнопка NPC 30051 (режим 6, предмет 60073 → 6132)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaDonationSubsystem* S = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaDonationSubsystem>() : nullptr;
		if (S) { S->OpenNumPad(); }
	}));
#endif // ASDA_DEV_CONSOLE
