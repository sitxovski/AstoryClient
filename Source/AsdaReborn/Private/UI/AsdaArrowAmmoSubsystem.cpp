// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaArrowAmmoSubsystem — расход стрел слота 10 (пакет 5030). Поток, разметка и отступления — в шапке заголовка.
#include "UI/AsdaArrowAmmoSubsystem.h"
#include "AsdaPerf.h"   // R4, доля a: области замера времени игрового потока
#include "AsdaBytes.h"                    // RdI16/RdI32/WrI16/WrI32 — поля тела и записи 57 б
#include "AsdaDevConsole.h"               // ASDA_DEV_CVAR — ключ отката
#include "Items/AsdaItemTableSubsystem.h" // имя предмета, flagWord176/tooltipFlag200, тип
#include "UI/AsdaInventorySubsystem.h"    // модель контейнеров 2/3 и штатная запись `ApplyExternalRecord`
#include "UI/AsdaMessageSubsystem.h"      // EAsdaMsgCategory
#include "UI/AsdaNotice.h"                // LocSlot / Loc / ChatColored
#include "World/AsdaAttackExit.h"        // выход своего из атаки в покой (`CCharacter_SetAction` case 0) + 4024/400
#include "World/AsdaWorldSubsystem.h"     // часть модели при `flagWord176 == 0` (`itemUI_ApplyEquipWithColor`)
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaArrowAmmo, Log, All);

// ★ [ИСПРАВЛЕНИЕ] 31.10.2026 (баг 10): приём 5030 по `recvItemResultNotice@0xD61300` (шапка заголовка).
static ASDA_DEV_CVAR(int32, CVarArrow5030,
	TEXT("asda.Inven.Arrow5030"),
	1,
	TEXT("Пакет 5030 (расход стрел слота 10): 1 = как эталон — число стрел, подтяжка пачки из сумки, «стрел нет» "
	     "(снятие слота, остановка атаки, чат 2021); 0 = прежний порт — пакет игнорируется."),
	ECVF_Default);

namespace AsdaArrowAmmoLocal
{
	constexpr int32 kLocTakenOff = 20135;   // «[ss1]: предмет вынут» (`slotItemDrop` case 3)
	constexpr int32 kLocPutOn    = 20134;   // «[ss1]: предмет помещён» (`inventoryApplySlotItem` case 3)
	constexpr int32 kLocNoArrows = 2021;    // код 2 — стрел больше нет
	constexpr int32 kLocNoSowels = 2033;    // `itemUI_ApplyEquipWithColor`: гнёзда самоцветов пусты (не оружие)
	const FColor kColEquip(0xFF, 0xD3, 0x06, 0xFF);    // −11514 = 0xFFFFD306
	const FColor kColNoArrows(0xFF, 0xAD, 0x00, 0xFF); // −21248 = 0xFFFFAD00
}

void UAsdaArrowAmmoSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	TArray<TArray<uint8>> Drop;
	Pending.Drain(Drop);
	UE_LOG(LogAsdaArrowAmmo, Verbose, TEXT("[стрелы] сброс сессии (%s), выброшено тел 5030: %d"), AsdaSessionResetReasonName(Reason), Drop.Num());
}

bool UAsdaArrowAmmoSubsystem::Push5030(const TArray<uint8>& Body18)
{
	if (CVarArrow5030.GetValueOnGameThread() == 0) { return false; }
	Pending.Push(Body18);
	return true;
}

void UAsdaArrowAmmoSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.ArrowAmmo");   // R4, доля a: область замера (AsdaPerf.h; в Shipping пусто)
	PumpPending();
}

void UAsdaArrowAmmoSubsystem::PumpPending()
{
	if (TArray<TArray<uint8>> Local; Pending.Drain(Local))
	{
		for (const TArray<uint8>& B : Local) { Apply(B); }
	}
}

void UAsdaArrowAmmoSubsystem::Apply(const TArray<uint8>& B)
{
	using namespace AsdaArrowAmmoLocal;
	if (B.Num() < kBodySize) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return; }
	// ★ Ревизия 01.11 (гонка очередей): эталон применяет пакеты строго в порядке прихода, а у порта 5030 и пакеты инвентаря
	//   (5001/5003/4999/подбор…) лежат в РАЗНЫХ очередях, чьи Tick идут в произвольном порядке. Пачка, пришедшая в том же
	//   кадре ДО 5030, иначе была бы ещё не в модели. Поэтому сначала разбираем очередь инвентаря (та же работа, что в его Tick).
	Inv->DebugPumpPendingPackets();
	const uint8* P = B.GetData();
	const int32 Code = P[0];
	const int32 Amount = AsdaBytes::RdI32(P + 1);
	const int32 BagSlot = AsdaBytes::RdI16(P + 7);
	UE_LOG(LogAsdaArrowAmmo, Log, TEXT("[стрелы] 5030: код=%d кол-во=%d ячейка расходников=%d"), Code, Amount, BagSlot);

	if (Code == 1)
	{
		if (Amount > 0)
		{
			// `itemDropUpdateWeightBar` ветка «> 0»: +9 = количество, `uiStatLabelUpdateCount` — надпись (через перерисовку).
			const FAsdaItemInstance* Cur = Inv->GetCell(3, kArrowSlot);
			if (!Cur || Cur->IsEmpty() || Cur->ItemCode <= 0)
			{
				UE_LOG(LogAsdaArrowAmmo, Warning, TEXT("[стрелы] 5030 кол-во %d, а слот 10 пуст — надписи нет (эталон: гейт кода > 0)"), Amount);
				return;
			}
			TArray<uint8> Rec = Cur->Raw;
			AsdaBytes::WrI32(Rec.GetData() + 9, Amount);
			Inv->ApplyExternalRecord(Rec, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			return;
		}
		// Ветка «≤ 0»: пачка кончилась — сервер уже переложил следующую из ячейки @7 расходников. Вес поставил диспетчер.
		TArray<uint8> Next;
		if (const FAsdaItemInstance* Src = Inv->GetCell(2, BagSlot); Src && !Src->IsEmpty()) { Next = Src->Raw; }
		DropArrowSlot();
		if (Next.Num() >= FAsdaItemInstance::kRecordSize)
		{
			PutArrowSlot(Next);
			// `slotItemDrop(2, @7)`: memset ячейки расходников (без нотиса) — штатной записью «−1» с перерисовкой сетки.
			TArray<uint8> Clr; Clr.SetNumZeroed(FAsdaItemInstance::kRecordSize);
			Clr[4] = 2; AsdaBytes::WrI16(Clr.GetData() + 5, BagSlot); AsdaBytes::WrI32(Clr.GetData() + 9, -1);
			Inv->ApplyExternalRecord(Clr);
		}
		else
		{
			UE_LOG(LogAsdaArrowAmmo, Warning, TEXT("[стрелы] 5030: ячейка расходников %d пуста — новая пачка не надета [ПОРТ]"), BagSlot);
		}
		return;
	}
	if (Code == 2)
	{
		// `itemDropBuildLocalPlayerRecord` → `slotItemDrop(3, 10)`, затем `CharFx_AutoSabzil_State(…, 0)` и чат 2021.
		DropArrowSlot();
		StopOwnAction();
		ChatLoc(kLocNoArrows, FString(), kColNoArrows);
		return;
	}
	UE_LOG(LogAsdaArrowAmmo, Log, TEXT("[стрелы] 5030 код %d — эталон ничего не делает"), Code);
}

void UAsdaArrowAmmoSubsystem::DropArrowSlot()
{
	using namespace AsdaArrowAmmoLocal;
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemInstance* Cur = Inv ? Inv->GetCell(3, kArrowSlot) : nullptr;
	// `slotItemDrop` case 3: нотис и очистка — только если в слоте предмет (`code > 0`).
	if (!Cur || Cur->IsEmpty() || Cur->ItemCode <= 0) { return; }
	ChatLoc(kLocTakenOff, IT ? IT->GetItemName(Cur->ItemCode) : FString(), kColEquip);
	TArray<uint8> Clr; Clr.SetNumZeroed(FAsdaItemInstance::kRecordSize);
	AsdaBytes::WrI32(Clr.GetData() + 0, Cur->ItemCode);
	Clr[4] = 3; AsdaBytes::WrI16(Clr.GetData() + 5, kArrowSlot); AsdaBytes::WrI32(Clr.GetData() + 9, -1);
	Inv->ApplyExternalRecord(Clr);   // −1 ⇒ ClearCell + RefreshEquip (хотбар — внутри ClearCell)
}

void UAsdaArrowAmmoSubsystem::PutArrowSlot(TArray<uint8> Rec57)
{
	using namespace AsdaArrowAmmoLocal;
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Inv || Rec57.Num() < FAsdaItemInstance::kRecordSize) { return; }
	const int32 ItemCode = AsdaBytes::RdI32(Rec57.GetData() + 0);
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemCode) : nullptr;
	// `inventoryApplySlotItem` case 3: +50 = 1 при `tooltipFlag200 == 1`; +4 = 3, +5 = слот (`localPlayerSetSlotRecord` + поля 296/297).
	if (Rec && Rec->TooltipFlag200 == 1) { Rec57[50] = 1; }
	Rec57[4] = 3;
	AsdaBytes::WrI16(Rec57.GetData() + 5, kArrowSlot);
	Inv->ApplyExternalRecord(Rec57, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	if (Rec && Rec->FlagWord176 == 0)
	{
		// `itemUI_ApplyEquipWithColor@0xC6B5B0` (только при `flagWord176 == 0`): часть модели `tagCHARACTER_SetPartsModelByType`
		//   — общая точка порта, что и у надевания в сумке (слоты без части модели она отсекает сама).
		if (UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
		{
			WS->ApplySelfEquipPart(kArrowSlot, ItemCode, (int32)Rec57[24]);
		}
	}
	if (Rec)
	{
		// `flagWord176 == 0` ⇒ `itemUI_ApplyEquipWithColor@0xC6B5B0`: все четыре гнезда (+16/18/20/22) пусты и тип вне 5…7 ⇒
		//   для слота ≠ 9 нотис 2033 цветом −21248. (У колчанов и наживки поле ненулевое — ветка для настоящих данных молчит.)
		const int16 S0 = AsdaBytes::RdI16(Rec57.GetData() + 16), S1 = AsdaBytes::RdI16(Rec57.GetData() + 18);
		const int16 S2 = AsdaBytes::RdI16(Rec57.GetData() + 20), S3 = AsdaBytes::RdI16(Rec57.GetData() + 22);
		const bool bAccessory = Rec->ItemType >= 5 && Rec->ItemType <= 7;
		if (Rec->FlagWord176 == 0 && !bAccessory && S0 <= 0 && S1 <= 0 && S2 <= 0 && S3 <= 0)
		{
			ChatLoc(kLocNoSowels, FString(), kColNoArrows);
		}
		ChatLoc(kLocPutOn, IT->GetItemName(ItemCode), kColEquip);
	}
}

void UAsdaArrowAmmoSubsystem::StopOwnAction()
{
	// `CharFx_AutoSabzil_State(LocalPlayer, …, 0, 0, 0)` → `CCharacter_SetAction(…, 0)`: общая точка выхода из атаки
	//   (PvP — `StopPvpAttack`, обычная — как клик по земле; стойка, слот показа навыка, покой; было действие 5 ⇒ 4024/400).
	AsdaAttackExit::StopSelfActionToIdle(GetGameInstance(), TEXT("5030 код 2: стрел нет"));
	++AttackStops;
	UE_LOG(LogAsdaArrowAmmo, Log, TEXT("[стрелы] стрел нет — атака снята, персонаж в покое"));
}

void UAsdaArrowAmmoSubsystem::ChatLoc(int32 LocId, const FString& Slot1, FColor Color)
{
	UGameInstance* GI = GetGameInstance();
	FString Text;
	if (LocId == AsdaArrowAmmoLocal::kLocTakenOff || LocId == AsdaArrowAmmoLocal::kLocPutOn)
	{
		// 20134/20135 — тем же маршрутом, что надевание/снятие в сумке (`OnSelfEquipSlotChanged`: категория Loot = #FFD306, 10 с).
		Text = AsdaNotice::LocSlot(GI, LocId, Slot1);
		if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
		{
			Msg->PostLocFmt(EAsdaMsgCategory::Loot, LocId, { Slot1 }, {});
		}
	}
	else
	{
		Text = AsdaNotice::Loc(GI, LocId);
		const EAsdaMsgCategory Cat = LocId == AsdaArrowAmmoLocal::kLocNoArrows ? EAsdaMsgCategory::CombatRefused : EAsdaMsgCategory::ItemRefused;
		AsdaNotice::ChatColored(GI, Cat, Text, Color);
	}
	ShownNotices.Add(LocId);
	if (ShownNotices.Num() > 32) { ShownNotices.RemoveAt(0); }
	UE_LOG(LogAsdaArrowAmmo, Log, TEXT("[стрелы] чат %d: «%s»"), LocId, *Text);
}
