// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВОЛНЫ В1 (часть A) АУДИТА ДУБЛЕЙ №2 — ПОБАЙТНАЯ СВЕРКА ВЫНЕСЕННЫХ СБОРЩИКОВ С ПРЕЖНЕЙ РУЧНОЙ СБОРКОЙ
//   (11.10.2026; план `Reports/dedup_audit2/00_PLAN.md` §5 В1, отчёт `wave1_A.md`).
//
// ★ ЗАЧЕМ. Волна В1 — ЧИСТЫЙ ВЫНОС: байты записей и пакетов меняться не должны. Стенд `asda.Net.PacketStand` ловит
//   отправители `UAsdaNetSubsystem::Send*`, но записи, которые собирает ОКНО (U6: 9901/9905/9909, 5018, 11006, 6096) и
//   синтетические записи предмета для подсказок (U17), до стенда не доходят. Здесь каждая новая сборка сравнивается
//   побайтно с КОПИЕЙ ПРЕЖНЕЙ (функции `Old*` ниже — дословно тела до правки, из бэкапа
//   `_backup/2026-10-11_dedup2_w1A`) на одних и тех же входах:
//     • U17 — предпросмотр по коду для ВСЕХ кодов item_list, special_itemlist, sowell_list и кодов 0 / −1 / несуществующего;
//       «только код», «код + количество», «очистить ячейку», запись ячейки самотестов, консольные записи поверх `Clear()`,
//       запись лота лавки, запись лотка партнёра обмена, запись эмуляции «показа предмета»;
//     • U6 — записи 9901/9905/9909 (случайные строки), ссылки 5018, запись 11006, слоты-приёмники 6096 по всем коробкам;
//     • N10 — `TAsdaNetQueue::RemoveIf` против прежней тройки «Drain → RemoveAll → Push».
//   Входы — детерминированный xorshift64 (зерно постоянное), поэтому прогон воспроизводим.
//
// Консоль: `asda.Dedup.W1ASelfTest [случаев=2000] [quit]`. Строки — «САМОТЕСТ-В1А ok|FAIL …», итог — «САМОТЕСТ-В1А ИТОГ: ok=… fail=…».
//   Только офлайн-отладка (`-AsdaNoNet`); пакеты не шлются, состояние окон не меняется (сборщики — чистые функции).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetQueue.h"
#include "AsdaPacketWriter.h"
#include "AsdaSelfTestBatch.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"   // ON_SCOPE_EXIT — возврат ключа asda.Item.ChargeStackReserve после сверки U6
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Login/AsdaLoginData.h"
#include "UI/AsdaExchangeP2PSubsystem.h"
#include "UI/AsdaExchangeSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaNpcShopSubsystem.h"
#include "UI/AsdaPremiumPostSubsystem.h"
#include "UI/AsdaSelfStoreSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDedupW1A, Log, All);

// Ключ `asda.Package.EmptyRowShowsPanel` (определение — UI/AsdaPackagePanelSubsystem.cpp): эталонная копия резерва U6
//   повторяет правило ключа (11.11.2026, хвост правки А1).
bool AsdaPackageEmptyRowShowsPanelOn();

// Друг подсистем лавки, обмена и инвентаря (объявлен в их заголовках): нужны `FLot`, `BuildLotTipRecord`, `PartnerRecordTo57`,
//   `Container()`.
struct FAsdaDedupW1ASelfTest
{
	// Точка входа консольной команды.
	static void Run(const TArray<FString>& Args, UWorld* W);

	// ── генератор и учёт ──
	struct FRng
	{
		uint64 S;
		explicit FRng(uint64 Seed) : S(Seed ? Seed : 0x9E3779B97F4A7C15ull) {}
		uint32 Next() { S ^= S << 13; S ^= S >> 7; S ^= S << 17; return (uint32)(S >> 32); }
		int32 Range(int32 N) { return N > 0 ? (int32)(Next() % (uint32)N) : 0; }
		bool Coin() { return (Next() & 1u) != 0; }
		// Целое «с границами»: 0, ±1, края i16/i32 и случайные — там, где усечение байтов видно.
		int32 I32()
		{
			static const int32 Edge[] = { 0, 1, -1, 127, 128, 255, 256, -128, -129, 32767, 32768, -32768, -32769, 65535, 65536, MAX_int32, MIN_int32 };
			return Coin() ? Edge[Range(UE_ARRAY_COUNT(Edge))] : (int32)Next();
		}
		uint8 U8() { return (uint8)Next(); }
	};

	struct FTally
	{
		int32 Ok = 0, Fail = 0;
		// Проверка одной группы: N сравнений, из них Bad провалов; первый провал — с hex-дампом.
		void Group(const TCHAR* What, int32 N, int32 Bad, const FString& FirstBad)
		{
			if (Bad == 0 && N > 0)
			{
				++Ok;
				UE_LOG(LogAsdaDedupW1A, Display, TEXT("САМОТЕСТ-В1А ok %s: совпало %d из %d"), What, N, N);
			}
			else
			{
				++Fail;
				UE_LOG(LogAsdaDedupW1A, Error, TEXT("САМОТЕСТ-В1А FAIL %s: расхождений %d из %d%s%s"), What, Bad, N,
					FirstBad.IsEmpty() ? TEXT("") : TEXT("; первое: "), *FirstBad);
			}
		}
	};

	static FString Hex(const TArray<uint8>& A)
	{
		FString S;
		for (int32 i = 0; i < A.Num(); ++i) { S += FString::Printf(TEXT("%02X"), A[i]); }
		return S;
	}
	// Сравнение двух тел; при расхождении — строка для лога (только первая).
	static bool Same(const TArray<uint8>& A, const TArray<uint8>& B, const FString& Ctx, int32& Bad, FString& FirstBad)
	{
		if (A == B) { return true; }
		if (Bad++ == 0) { FirstBad = FString::Printf(TEXT("%s\n  было: %s\n  стало: %s"), *Ctx, *Hex(A), *Hex(B)); }
		return false;
	}
	static TArray<uint8> RandBytes(FRng& R, int32 N)
	{
		TArray<uint8> B; B.SetNumUninitialized(N);
		const int32 Mode = R.Range(4);
		for (int32 i = 0; i < N; ++i) { B[i] = (Mode == 0) ? 0 : (Mode == 1) ? 0xFF : R.U8(); }
		return B;
	}
	static FString RandName(FRng& R)
	{
		static const TCHAR* Pool[] = { TEXT(""), TEXT("Рыбак"), TEXT("sitxovski"), TEXT("ДлинноеИмяПерсонажаБольшеДвадцатиБукв"),
			TEXT("Ab'cd`ef"), TEXT("Ёжик Ё"), TEXT("ArabicعربيName"), TEXT("12345678901234567890123") };
		return Pool[R.Range(UE_ARRAY_COUNT(Pool))];
	}

	// ═══════════════ ПРЕЖНИЕ СБОРКИ (дословно до правки В1) ═══════════════
	// U17 — `UAsdaItemTooltipSubsystem::ShowPreviewSlotTooltip`.
	static TArray<uint8> OldPreview(const UAsdaItemTableSubsystem* IT, int32 ItemCode)
	{
		TArray<uint8> Rec;
		Rec.SetNumZeroed(FAsdaItemInstance::kRecordSize);
		AsdaBytes::WrI32(Rec.GetData() + 0, ItemCode);
		const FAsdaItemRecord* R = IT->GetItem(ItemCode);
		FAsdaSpecialItemlistRow Sp;
		if (R)
		{
			Rec[13] = (uint8)FMath::Clamp(R->DurabilityMax, 0, 255);
			AsdaBytes::WrI16(Rec.GetData() + 14, R->Weight);
			Rec[50] = (uint8)FMath::Clamp(R->TooltipFlag200, 0, 255);
		}
		else if (IT->GetSpecialItem(ItemCode, Sp))
		{
			AsdaBytes::WrI32(Rec.GetData() + 9, Sp.Field88);
			AsdaBytes::WrI16(Rec.GetData() + 14, Sp.RequiredLevel);
			Rec[50] = (uint8)FMath::Clamp(Sp.Flag124, 0, 255);
		}
		return Rec;
	}
	// U17 — «только код» (`ShowStampSlotTooltip`, `ShowGachaSlotTooltip`).
	static TArray<uint8> OldByCode(int32 ItemCode)
	{
		TArray<uint8> Rec;
		Rec.SetNumZeroed(FAsdaItemInstance::kRecordSize);
		AsdaBytes::WrI32(Rec.GetData() + 0, ItemCode);
		return Rec;
	}
	// U17 — «код + количество» (тултип по коду с количеством; HighRefine с количеством 1).
	static TArray<uint8> OldByCodeQty(int32 ItemCode, int32 Qty)
	{
		TArray<uint8> Rec; Rec.SetNumZeroed(57);
		AsdaBytes::WrI32(Rec.GetData() + 0, ItemCode);
		AsdaBytes::WrI32(Rec.GetData() + 9, Qty);
		return Rec;
	}
	// U17 — очистка ячейки (Sowel ×2: контейнер 2; PremiumGacha: контейнер из пакета).
	static TArray<uint8> OldClearSowel(int32 Slot)
	{
		TArray<uint8> Empty; Empty.SetNumZeroed(57);
		Empty[4] = 2;
		Empty[5] = (uint8)((uint16)(int16)Slot & 0xFF);
		Empty[6] = (uint8)(((uint16)(int16)Slot >> 8) & 0xFF);
		Empty[9] = 0xFF; Empty[10] = 0xFF; Empty[11] = 0xFF; Empty[12] = 0xFF;
		return Empty;
	}
	static TArray<uint8> OldClearGacha(int32 OldCont, int32 OldSlot)
	{
		TArray<uint8> Clear;
		Clear.SetNumZeroed(57);
		Clear[4] = (uint8)OldCont;
		*reinterpret_cast<int16*>(Clear.GetData() + 5) = (int16)OldSlot;
		*reinterpret_cast<int32*>(Clear.GetData() + 9) = -1;
		return Clear;
	}
	// U17 — запись ячейки самотестов (FishBook/Wave2D/CharInfo; Disassembly — с гнездом и процентом; CharInfo — с парами).
	static TArray<uint8> OldCell(int32 Code, int32 Cont, int32 Slot, int32 Amount, int32 Gem, int32 Pct, const TArray<FIntVector>& Opts)
	{
		TArray<uint8> R; R.SetNumZeroed(57);
		AsdaBytes::WrI32(R.GetData(), Code); R[4] = (uint8)Cont; AsdaBytes::WrI16(R.GetData() + 5, Slot);
		AsdaBytes::WrI16(R.GetData() + 7, -1); AsdaBytes::WrI32(R.GetData() + 9, Amount);
		if (Gem != 0 || Pct != 0) { AsdaBytes::WrI16(R.GetData() + 16, Gem); AsdaBytes::WrI16(R.GetData() + 27, Pct); }
		for (const FIntVector& O : Opts)
		{
			AsdaBytes::WrI16(R.GetData() + 29 + 4 * O.X, O.Y);
			AsdaBytes::WrI16(R.GetData() + 31 + 4 * O.X, O.Z);
		}
		return R;
	}
	// U17 — консоль `asda.ItemTip.Code` (запись поверх `Clear()`).
	static void OldTipCode(FAsdaItemInstance& It, int32 Code, int32 NArgs, int32 Gem, int32 PctArg, int32 SwArg)
	{
		It.Clear();
		It.Raw.SetNumZeroed(FAsdaItemInstance::kRecordSize);
		FMemory::Memcpy(It.Raw.GetData(), &Code, 4);
		It.Raw[9] = 1;
		if (NArgs > 1) { It.Raw[24] = (uint8)Gem; }
		if (NArgs > 2)
		{
			const int16 Pct = (int16)PctArg;
			FMemory::Memcpy(It.Raw.GetData() + 27, &Pct, 2);
		}
		if (NArgs > 3)
		{
			const int16 Sw = (int16)SwArg;
			FMemory::Memcpy(It.Raw.GetData() + 16, &Sw, 2);
		}
		It.Raw[13] = 100;
		It.Raw[26] = 5;
		It.SyncFromRaw();
	}
	// U17 — консоль `asda.ItemTip.Pipelines` и `asda.ItemTip.Sample` (поверх `Clear()`).
	static void OldTipPipelines(FAsdaItemInstance& It, int32 Code, int32 Qty)
	{
		It.Clear();
		It.Raw.SetNumZeroed(FAsdaItemInstance::kRecordSize);
		FMemory::Memcpy(It.Raw.GetData(), &Code, 4);
		FMemory::Memcpy(It.Raw.GetData() + 9, &Qty, 4);
		It.SyncFromRaw();
	}
	static void OldTipSample(FAsdaItemInstance& It, int32 Code)
	{
		It.Clear();
		It.Raw.SetNumZeroed(FAsdaItemInstance::kRecordSize);
		FMemory::Memcpy(It.Raw.GetData(), &Code, 4);
		It.Raw[9] = 1; It.Raw[13] = 100; It.Raw[26] = 5;
		It.SyncFromRaw();
	}
	// U17 — эмуляция «показа предмета» (`UAsdaShowOffSubsystem::SimulateIncoming`).
	static TArray<uint8> OldShowOff(int32 ItemCode)
	{
		TArray<uint8> Rec;
		Rec.SetNumZeroed(FAsdaItemInstance::kRecordSize);
		Rec[0] = (uint8)( (uint32)ItemCode        & 0xFF);
		Rec[1] = (uint8)(((uint32)ItemCode >>  8) & 0xFF);
		Rec[2] = (uint8)(((uint32)ItemCode >> 16) & 0xFF);
		Rec[3] = (uint8)(((uint32)ItemCode >> 24) & 0xFF);
		Rec[4] = 1;
		Rec[9] = 1;
		return Rec;
	}
	// U17 — лавка (`UAsdaSelfStoreSubsystem::ShowLotTooltip`).
	static TArray<uint8> OldLot(const UAsdaSelfStoreSubsystem::FLot& Lot, bool bFull)
	{
		TArray<uint8> R; R.SetNumZeroed(57);
		AsdaBytes::WrI16(R.GetData() + 5, -1);
		AsdaBytes::WrI32(R.GetData() + 7, -1);
		AsdaBytes::WrI32(R.GetData() + 0, Lot.ItemId);
		AsdaBytes::WrI32(R.GetData() + 9, bFull ? Lot.Stock : FMath::Max(0, Lot.Stock));
		R[13] = Lot.Durability;
		AsdaBytes::WrI16(R.GetData() + 14, Lot.StackWeight);
		for (int32 k = 0; k < 4; ++k) { AsdaBytes::WrI16(R.GetData() + 16 + 2 * k, Lot.Sowel[k]); }
		R[24] = Lot.EnchantFlag;
		R[25] = Lot.Ench73;
		R[26] = Lot.Ench74;
		AsdaBytes::WrI16(R.GetData() + 27, (int16)Lot.EnchantPct);
		for (int32 k = 0; k < 5; ++k) { AsdaBytes::WrI32(R.GetData() + 29 + 4 * k, Lot.Ench5[k]); }
		return R;
	}
	// U17 — лоток партнёра обмена (`PartnerTo57` окна 2008).
	static TArray<uint8> OldPartner(const uint8* P, bool bGems)
	{
		TArray<uint8> R; R.SetNumZeroed(57);
		if (!P) { return R; }
		AsdaBytes::WrI32(R.GetData() + 0, AsdaBytes::RdI32(P + 0));
		AsdaBytes::WrI32(R.GetData() + 9, AsdaBytes::RdI32(P + 7));
		R[13] = P[4];
		AsdaBytes::WrI16(R.GetData() + 14, AsdaBytes::RdI16(P + 5));
		R[24] = P[11];
		R[25] = P[13];
		R[26] = P[12];
		AsdaBytes::WrI16(R.GetData() + 27, AsdaBytes::RdI16(P + 14));
		for (int32 i = 0; i < 5; ++i) { AsdaBytes::WrI32(R.GetData() + 29 + 4 * i, AsdaBytes::RdI32(P + 22 + 4 * i)); }
		if (bGems) { for (int32 j = 0; j < 4; ++j) { AsdaBytes::WrI16(R.GetData() + 16 + 2 * j, AsdaBytes::RdI16(P + 43 + 2 * j)); } }
		R[50] = 0;
		return R;
	}
	// U6 — 9901.
	static TArray<uint8> Old9901(const FAsdaExchangeRegRow& R, const FAsdaExchRegClass& Cls, const FString& SelfName)
	{
		TArray<uint8> Rec; Rec.Init(0, 98);
		AsdaBytes::WrI32(Rec.GetData() + 0,  R.Item.ItemCode);
		Rec[4] = (uint8)R.SrcContainer;
		AsdaBytes::WrI16(Rec.GetData() + 5,  R.SrcCell);
		Rec[7] = (uint8)R.Item.Durability;
		Rec[8] = (uint8)R.Item.GemGrade;
		if (R.Item.Raw.IsValidIndex(26)) { Rec[9]  = R.Item.Raw[26]; }
		if (R.Item.Raw.IsValidIndex(25)) { Rec[10] = R.Item.Raw[25]; }
		AsdaBytes::WrI16(Rec.GetData() + 11, R.Item.EnchantPercent);
		AsdaBytes::WrI32(Rec.GetData() + 19, R.Item.Amount);
		for (int32 S = 0; S < 5; ++S)
		{
			const int32 Code = R.Item.EnchantCode.IsValidIndex(S)  ? R.Item.EnchantCode[S]  : 0;
			const int32 Val  = R.Item.EnchantValue.IsValidIndex(S) ? R.Item.EnchantValue[S] : 0;
			AsdaBytes::WrI16(Rec.GetData() + 29 + S * 4,     Code);
			AsdaBytes::WrI16(Rec.GetData() + 29 + S * 4 + 2, Val);
		}
		if (Cls.bValid)
		{
			Rec[50] = (uint8)Cls.ItemType;
			Rec[51] = (uint8)Cls.SubType;
			Rec[53] = (uint8)Cls.LevelTier;
			Rec[55] = (uint8)Cls.CategoryIndex;
		}
		AsdaBytes::WrI32(Rec.GetData() + 57, (int32)FMath::Clamp<int64>(R.Price, 0, MAX_int32));
		Rec[61] = (uint8)UAsdaExchangeSubsystem::DurationCodeForDays(R.DueDays);
		FAsdaLoginData::EncodeCp1251(SelfName, Rec.GetData() + 62, 20);
		return Rec;
	}
	// U6 — 9909.
	static TArray<uint8> Old9909(const FAsdaExchangeMyLot& L)
	{
		TArray<uint8> Rec; Rec.Init(0, 45);
		AsdaBytes::WrI32(Rec.GetData() + 0,  L.Serial);
		AsdaBytes::WrI32(Rec.GetData() + 4,  L.ItemCode);
		AsdaBytes::WrI32(Rec.GetData() + 8,  L.Amount);
		Rec[12] = (uint8)L.Item.Durability;
		Rec[13] = (uint8)L.Item.GemGrade;
		Rec[14] = (uint8)L.ItemRec26;
		Rec[15] = (uint8)L.ItemRec25;
		AsdaBytes::WrI16(Rec.GetData() + 16, L.Item.EnchantPercent);
		for (int32 S = 0; S < 5; ++S)
		{
			const int32 V = L.EnchantOpts.IsValidIndex(S) ? L.EnchantOpts[S] : 0;
			AsdaBytes::WrI32(Rec.GetData() + 24 + S * 4, V);
		}
		return Rec;
	}
	// U6 — 9905.
	static TArray<uint8> Old9905(const FAsdaExchangeListing& L)
	{
		TArray<uint8> Rec; Rec.Init(0, 45);
		AsdaBytes::WrI32(Rec.GetData() + 0, L.Serial);
		AsdaBytes::WrI32(Rec.GetData() + 4, L.ItemCode);
		AsdaBytes::WrI32(Rec.GetData() + 8, L.Amount);
		Rec[12] = (uint8)L.Item.Durability;
		Rec[13] = (uint8)L.Item.GemGrade;
		for (int32 S = 0; S < 5; ++S)
		{
			const int32 V = L.EnchantOpts.IsValidIndex(S) ? L.EnchantOpts[S] : 0;
			AsdaBytes::WrI32(Rec.GetData() + 24 + S * 4, V);
		}
		return Rec;
	}
	// U6 — одна ссылка 5018 (тело цикла `UAsdaNpcShopSubsystem::SendSell`).
	static void Old5018Ref(TArray<uint8>& Refs, int32 Packed, int32 SlotIndex, int32 Second, int32 Container, int32 Amount, int32 StackW)
	{
		uint8* P = Refs.GetData() + 11 * Packed;
		AsdaBytes::WrI16(P + 0, SlotIndex);
		AsdaBytes::WrI16(P + 2, Second);
		P[4] = (uint8)Container;
		AsdaBytes::WrI32(P + 5, Amount);
		AsdaBytes::WrI16(P + 9, StackW);
	}
	// U6 — 11006.
	static TArray<uint8> Old11006(int32 Order, int32 ItemIndex, int32 Count, int32 BuyType)
	{
		uint8 Rec10[10];
		FMemory::Memzero(Rec10, sizeof(Rec10));
		AsdaBytes::WrI32(&Rec10[0], Order);
		AsdaBytes::WrI32(&Rec10[4], ItemIndex);
		Rec10[8] = (uint8)FMath::Clamp(Count, 0, UAsdaPremiumPostSubsystem::kCols);
		Rec10[9] = (uint8)BuyType;
		return TArray<uint8>(Rec10, 10);
	}
	// U6 — `UAsdaInventorySubsystem::ReservePackageSlots` до правки (запись прямо в массив).
	static bool OldReserve(const UAsdaInventorySubsystem* Inv, const UAsdaItemTableSubsystem* IT, int32 ItemCode, TArray<uint8>& OutSlots15,
		int32& OutCount, int32& OutShortSpend, int32& OutShortBag)
	{
		OutSlots15.Reset();
		OutSlots15.SetNumZeroed(15);
		OutCount = 0; OutShortSpend = 0; OutShortBag = 0;
		if (!IT) { return false; }
		TArray<FAsdaPackageEntry> Contents;
		// ★ 11.11.2026 (хвост А1): при ключе `asda.Package.EmptyRowShowsPanel` = 1 найденная, но ПУСТАЯ строка packagetable —
		//   «место есть» (эталон `itemFindSowelSlots` возвращает 1, item_inventory.c:53936): слоты — исходное состояние
		//   цикла ниже, позиций 0. Ключ 0 — прежний отказ. Остальные коробки ключ не меняет.
		const bool bEmptyRowOk = AsdaPackageEmptyRowShowsPanelOn() && IT->IsPackageItem(ItemCode);   // правило ключа А1
		if (!IT->GetPackageContents(ItemCode, Contents) && !bEmptyRowOk) { return false; }
		for (int32 i = 0; i < 5; ++i)
		{
			OutSlots15[3 * i] = 0xFF;
			AsdaBytes::WrI16(&OutSlots15[3 * i + 1], -1);
		}
		TSet<int32> TakenBag, TakenSpend;
		const TArray<FAsdaItemInstance>* Bag    = Inv->Container(1);
		const TArray<FAsdaItemInstance>* Spend2 = Inv->Container(2);
		for (int32 i = 0; i < 5; ++i)
		{
			AsdaBytes::WrI16(&OutSlots15[3 * i + 1], -100);
			if (!Contents.IsValidIndex(i)) { continue; }
			const int32 Code = Contents[i].ItemCode;
			const FAsdaItemRecord* Rec = IT->GetItem(Code);
			int32 Kind = -1;
			if (Rec)                        { Kind = (Rec->ItemType <= 9 || Rec->CategoryFlag == 3) ? 1 : 0; }
			if (IT->IsSpecialItemCode(Code)) { Kind = 1; }
			if (IT->IsSowelCode(Code))       { Kind = 0; }
			if (Kind < 0) { continue; }
			if (Kind == 1)
			{
				OutSlots15[3 * i] = 1;
				int32 Found = -1;
				if (Bag) { for (int32 s = 0; s < Bag->Num(); ++s) { if ((*Bag)[s].IsEmpty() && !TakenBag.Contains(s)) { Found = s; break; } } }
				if (Found >= 0) { TakenBag.Add(Found); AsdaBytes::WrI16(&OutSlots15[3 * i + 1], Found); }
				else            { ++OutShortBag; }
			}
			else
			{
				OutSlots15[3 * i] = 2;
				int32 Found = -1;
				if (Rec && Rec->TradableFlag == 1 && Rec->ItemType != 10 && Spend2)
				{
					for (int32 s = 0; s < Spend2->Num(); ++s) { if ((*Spend2)[s].ItemCode == Code) { Found = s; break; } }
				}
				if (Found < 0 && Spend2)
				{
					for (int32 s = 0; s < Spend2->Num(); ++s) { if ((*Spend2)[s].IsEmpty() && !TakenSpend.Contains(s)) { Found = s; break; } }
				}
				if (Found >= 0) { TakenSpend.Add(Found); AsdaBytes::WrI16(&OutSlots15[3 * i + 1], Found); }
				else            { ++OutShortSpend; }
			}
			++OutCount;
		}
		return OutCount > 0 || (bEmptyRowOk && Contents.Num() == 0);   // пустая найденная строка при ключе 1 — «место есть»
	}

	// Случайная запись предмета (через сырые байты — так же, как приходят с провода).
	static FAsdaItemInstance RandItem(FRng& R)
	{
		FAsdaItemInstance It;
		const TArray<uint8> Raw = RandBytes(R, FAsdaItemInstance::kRecordSize);
		if (R.Range(8) == 0) { It.Clear(); } else { It.SetRaw(Raw.GetData(), Raw.Num()); }
		return It;
	}
	static TArray<int32> RandOpts(FRng& R)
	{
		TArray<int32> O;
		const int32 N = R.Range(7);   // 0…6: короче и длиннее пяти
		for (int32 i = 0; i < N; ++i) { O.Add(R.I32()); }
		return O;
	}
};

void FAsdaDedupW1ASelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	int32 Cases = 2000;
	bool bQuit = false;
	for (const FString& A : Args)
	{
		if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		else if (A.IsNumeric()) { Cases = FMath::Max(1, FCString::Atoi(*A)); }
	}
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	FTally T;
	if (!IT || !Tip || !Inv)
	{
		UE_LOG(LogAsdaDedupW1A, Error, TEXT("САМОТЕСТ-В1А FAIL нет подсистем (таблицы %d, подсказка %d, инвентарь %d)"), IT ? 1 : 0, Tip ? 1 : 0, Inv ? 1 : 0);
		++T.Fail;
	}
	else
	{
		// ── Коды для записей по коду: ВСЕ item_list, special_itemlist, sowell_list + особые ──
		TArray<int32> Codes = { 0, -1, 999999999 };
		int32 NItem = 0, NSpec = 0, NSow = 0;
		for (const FAsdaItemRecord& Rc : IT->GetAllItems()) { Codes.Add(Rc.ItemCode); ++NItem; }
		if (const UDataTable* Dt = IT->GetDataTable(TEXT("special_itemlist")))
		{
			TArray<FAsdaSpecialItemlistRow*> Rows; Dt->GetAllRows<FAsdaSpecialItemlistRow>(TEXT("W1A"), Rows);
			for (const FAsdaSpecialItemlistRow* Rw : Rows) { if (Rw) { Codes.Add(Rw->Code); ++NSpec; } }
		}
		if (const UDataTable* Dt = IT->GetDataTable(TEXT("sowell_list")))
		{
			TArray<FAsdaSowellRow*> Rows; Dt->GetAllRows<FAsdaSowellRow>(TEXT("W1A"), Rows);
			for (const FAsdaSowellRow* Rw : Rows) { if (Rw) { Codes.Add(Rw->Id); ++NSow; } }
		}
		UE_LOG(LogAsdaDedupW1A, Display, TEXT("САМОТЕСТ-В1А коды: item_list %d, special_itemlist %d, sowell_list %d, особых 3"), NItem, NSpec, NSow);
		const bool bTablesOk = (NItem > 0 && NSpec > 0 && NSow > 0);
		if (!bTablesOk) { UE_LOG(LogAsdaDedupW1A, Error, TEXT("САМОТЕСТ-В1А FAIL таблицы не загружены — сверка по кодам неполная")); ++T.Fail; }

		{   // U17 предпросмотр по коду (оружие, стопки, самоцветы, special_itemlist, код 0)
			int32 Bad = 0; FString First;
			for (const int32 C : Codes) { Same(OldPreview(IT, C), Tip->BuildPreviewSlotRecord(C), FString::Printf(TEXT("код %d"), C), Bad, First); }
			T.Group(TEXT("U17 предпросмотр по коду (ShowPreviewSlotTooltip)"), Codes.Num(), Bad, First);
		}
		{   // U17 «только код» и «код + количество»
			FRng R(0x17A1);
			int32 Bad = 0, N = 0; FString First;
			for (const int32 C : Codes)
			{
				Same(OldByCode(C), FAsdaItemRecordWriter::ByCode(C), FString::Printf(TEXT("ByCode %d"), C), Bad, First); ++N;
				const int32 Q = R.I32();
				Same(OldByCodeQty(C, Q), FAsdaItemRecordWriter::ByCodeQty(C, Q), FString::Printf(TEXT("ByCodeQty %d ×%d"), C, Q), Bad, First); ++N;
				Same(OldByCodeQty(C, FMath::Max(0, Q)), FAsdaItemRecordWriter::ByCodeQty(C, FMath::Max(0, Q)), TEXT("ByCodeQty Max(0)"), Bad, First); ++N;
				Same(OldByCodeQty(C, 1), FAsdaItemRecordWriter::ByCodeQty(C, 1), TEXT("ByCodeQty 1 (HighRefine)"), Bad, First); ++N;
				Same(OldShowOff(C), FAsdaItemRecordWriter().Code(C).Container(1).Amount(1).ToArray(), TEXT("ShowOff"), Bad, First); ++N;
			}
			T.Group(TEXT("U17 только код / код+количество / показ предмета"), N, Bad, First);
		}
		{   // U17 очистка ячейки и запись ячейки самотестов
			FRng R(0x17A2);
			int32 Bad = 0, N = 0; FString First;
			for (int32 k = 0; k < Cases; ++k)
			{
				const int32 Slot = R.I32(), Cont = R.I32(), Code = R.I32(), Amt = R.I32();
				Same(OldClearSowel(Slot), FAsdaItemRecordWriter::ClearCell(2, Slot), FString::Printf(TEXT("ClearCell(2,%d)"), Slot), Bad, First); ++N;
				Same(OldClearGacha(Cont, Slot), FAsdaItemRecordWriter::ClearCell(Cont, Slot), FString::Printf(TEXT("ClearCell(%d,%d)"), Cont, Slot), Bad, First); ++N;
				Same(OldCell(Code, Cont, Slot, Amt, 0, 0, {}), FAsdaItemRecordWriter::Cell(Code, Cont, Slot, Amt), TEXT("Cell"), Bad, First); ++N;
				const int32 Gem = R.I32(), Pct = R.I32();
				{
					FAsdaItemRecordWriter Rw;
					Rw.Code(Code).Container(Cont).Slot(Slot).Second(-1).Amount(Amt).Sowel(0, Gem).EnchantPercent(Pct);
					Same(OldCell(Code, Cont, Slot, Amt, Gem, Pct, {}), Rw.ToArray(), TEXT("Cell+гнездо+процент (Disassembly)"), Bad, First); ++N;
				}
				{
					TArray<FIntVector> Opts;
					const int32 NO = R.Range(4);
					for (int32 o = 0; o < NO; ++o) { Opts.Add(FIntVector(R.Range(5), R.I32(), R.I32())); }
					FAsdaItemRecordWriter Rw;
					Rw.Code(Code).Container(Cont).Slot(Slot).Second(-1).Amount(Amt);
					for (const FIntVector& O : Opts) { Rw.EnchantPair(O.X, O.Y, O.Z); }
					Same(OldCell(Code, Cont, Slot, Amt, 0, 0, Opts), Rw.ToArray(), TEXT("Cell+пары (CharInfo)"), Bad, First); ++N;
				}
			}
			T.Group(TEXT("U17 очистка ячейки / запись ячейки"), N, Bad, First);
		}
		{   // U17 консольные записи поверх Clear(): сырые байты И разобранные поля
			FRng R(0x17A3);
			int32 Bad = 0, N = 0; FString First;
			// Разобранные поля целиком (сверка того, что `SetRaw` даёт то же, что прежние `Clear()` + `SyncFromRaw()`).
			auto Fields = [](const FAsdaItemInstance& I)
			{
				FString S = FString::Printf(TEXT("%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d"), I.ItemCode, (int32)I.ContainerType, I.SlotIndex,
					I.SecondIndex, I.Amount, (int32)I.Durability, I.StackWeight, I.Sowel0, I.Sowel1, I.Sowel2, I.Sowel3, (int32)I.GemGrade,
					I.EnchantPercent, I.Raw.Num());
				for (int32 q = 0; q < I.EnchantCode.Num(); ++q) { S += FString::Printf(TEXT(" c%d"), I.EnchantCode[q]); }
				for (int32 q = 0; q < I.EnchantValue.Num(); ++q) { S += FString::Printf(TEXT(" v%d"), I.EnchantValue[q]); }
				return S;
			};
			for (int32 k = 0; k < Cases; ++k)
			{
				const int32 Code = R.I32(), NArgs = 1 + R.Range(4), Gem = R.I32(), Pct = R.I32(), Sw = R.I32(), Qty = R.I32();
				FAsdaItemInstance A, B;
				OldTipCode(A, Code, NArgs, Gem, Pct, Sw);
				{
					FAsdaItemRecordWriter Rw; Rw.PortClearSlots(); Rw.Code(Code); Rw.Amount(1);
					if (NArgs > 1) { Rw.GemGrade(Gem); }
					if (NArgs > 2) { Rw.EnchantPercent(Pct); }
					if (NArgs > 3) { Rw.Sowel(0, Sw); }
					Rw.Durability(100); Rw.Byte26(5); Rw.ApplyTo(B);
				}
				if (!Same(A.Raw, B.Raw, TEXT("asda.ItemTip.Code"), Bad, First) || Fields(A) != Fields(B)) { if (A.Raw == B.Raw && Bad++ == 0) { First = Fields(A) + TEXT(" | ") + Fields(B); } }
				++N;
				FAsdaItemInstance C, D;
				OldTipPipelines(C, Code, Qty);
				FAsdaItemRecordWriter().PortClearSlots().Code(Code).Amount(Qty).ApplyTo(D);
				if (!Same(C.Raw, D.Raw, TEXT("asda.ItemTip.Pipelines"), Bad, First) || Fields(C) != Fields(D)) { if (C.Raw == D.Raw && Bad++ == 0) { First = Fields(C) + TEXT(" | ") + Fields(D); } }
				++N;
				FAsdaItemInstance E, F;
				OldTipSample(E, Code);
				FAsdaItemRecordWriter().PortClearSlots().Code(Code).Amount(1).Durability(100).Byte26(5).ApplyTo(F);
				if (!Same(E.Raw, F.Raw, TEXT("asda.ItemTip выборка"), Bad, First) || Fields(E) != Fields(F)) { if (E.Raw == F.Raw && Bad++ == 0) { First = Fields(E) + TEXT(" | ") + Fields(F); } }
				++N;
			}
			T.Group(TEXT("U17 консольные записи тултипа поверх Clear() (байты и поля)"), N, Bad, First);
		}
		{   // U17 лавка
			FRng R(0x17A4);
			int32 Bad = 0, N = 0; FString First;
			for (int32 k = 0; k < Cases; ++k)
			{
				UAsdaSelfStoreSubsystem::FLot L;
				L.ItemId = R.I32(); L.Stock = R.I32();
				for (int32 j = 0; j < 4; ++j) { L.Sowel[j] = (int16)R.I32(); }
				for (int32 j = 0; j < 5; ++j) { L.Ench5[j] = R.I32(); }
				L.EnchantFlag = R.U8(); L.Ench73 = R.U8(); L.Ench74 = R.U8(); L.EnchantPct = R.I32();
				L.StackWeight = (int16)R.I32(); L.Durability = R.U8();
				for (const bool bFull : { false, true })
				{
					Same(OldLot(L, bFull), UAsdaSelfStoreSubsystem::BuildLotTipRecord(L, bFull), FString::Printf(TEXT("лот %d полный=%d"), L.ItemId, bFull ? 1 : 0), Bad, First); ++N;
				}
			}
			T.Group(TEXT("U17 запись лота лавки (ShowLotTooltip)"), N, Bad, First);
		}
		{   // U17 лоток партнёра обмена
			FRng R(0x17A5);
			int32 Bad = 0, N = 0; FString First;
			for (const bool bGems : { false, true })
			{
				Same(OldPartner(nullptr, bGems), UAsdaExchangeP2PSubsystem::PartnerRecordTo57(nullptr, bGems), TEXT("nullptr"), Bad, First); ++N;
			}
			for (int32 k = 0; k < Cases; ++k)
			{
				const TArray<uint8> P = RandBytes(R, 64);
				const bool bGems = R.Coin();
				Same(OldPartner(P.GetData(), bGems), UAsdaExchangeP2PSubsystem::PartnerRecordTo57(P.GetData(), bGems), Hex(P), Bad, First); ++N;
			}
			T.Group(TEXT("U17 запись лотка партнёра обмена (PartnerTo57)"), N, Bad, First);
		}
		// ═══ U6 ═══
		{   // 9901
			FRng R(0x6901);
			int32 Bad = 0; FString First;
			for (int32 k = 0; k < Cases; ++k)
			{
				FAsdaExchangeRegRow Row;
				Row.SrcContainer = R.I32(); Row.SrcCell = R.I32(); Row.Item = RandItem(R);
				if (R.Range(10) == 0) { Row.Item.Raw.SetNum(R.Range(30)); }   // короткая сырая запись: ветки IsValidIndex(25/26)
				if (R.Range(10) == 0) { Row.Item.EnchantCode.SetNum(R.Range(5)); Row.Item.EnchantValue.SetNum(R.Range(5)); }
				Row.Price = R.Coin() ? (int64)R.I32() : ((int64)R.Next() << 8);
				Row.DueDays = R.Range(9) - 1;
				FAsdaExchRegClass Cls; Cls.bValid = R.Coin(); Cls.bError = false;
				Cls.ItemType = R.I32(); Cls.SubType = R.I32(); Cls.LevelTier = R.I32(); Cls.CategoryIndex = R.I32();
				const FString Name = RandName(R);
				Same(Old9901(Row, Cls, Name), UAsdaExchangeSubsystem::BuildRegisterRow9901(Row, Cls, Name), FString::Printf(TEXT("случай %d"), k), Bad, First);
			}
			T.Group(TEXT("U6 запись выставления 9901 (98 б)"), Cases, Bad, First);
		}
		{   // 9909 и 9905
			FRng R(0x6909);
			int32 Bad = 0, N = 0; FString First;
			for (int32 k = 0; k < Cases; ++k)
			{
				FAsdaExchangeMyLot L;
				L.Serial = R.I32(); L.ItemCode = R.I32(); L.Amount = R.I32(); L.Item = RandItem(R);
				L.ItemRec25 = R.I32(); L.ItemRec26 = R.I32(); L.EnchantOpts = RandOpts(R);
				Same(Old9909(L), UAsdaExchangeSubsystem::BuildDeleteRow9909(L), FString::Printf(TEXT("9909 случай %d"), k), Bad, First); ++N;
				FAsdaExchangeListing G;
				G.Serial = R.I32(); G.ItemCode = R.I32(); G.Amount = R.I32(); G.Item = RandItem(R); G.EnchantOpts = RandOpts(R);
				Same(Old9905(G), UAsdaExchangeSubsystem::BuildBuyRow9905(G), FString::Printf(TEXT("9905 случай %d"), k), Bad, First); ++N;
			}
			T.Group(TEXT("U6 записи забора 9909 и покупки 9905 (45 б)"), N, Bad, First);
		}
		{   // 5018: весь буфер ссылок (55 б) после заполнения случайного числа позиций
			FRng R(0x5018);
			int32 Bad = 0; FString First;
			for (int32 k = 0; k < Cases; ++k)
			{
				TArray<uint8> Old; Old.SetNumZeroed(11 * UAsdaNpcShopSubsystem::kCartCells);
				FAsdaPacketWriter New(11 * UAsdaNpcShopSubsystem::kCartCells);
				const int32 NP = R.Range(UAsdaNpcShopSubsystem::kCartCells + 1);
				for (int32 p = 0; p < NP; ++p)
				{
					const int32 Sl = R.I32(), Se = R.I32(), Co = R.I32(), Am = R.I32(), Sw = R.I32();
					Old5018Ref(Old, p, Sl, Se, Co, Am, Sw);
					UAsdaNpcShopSubsystem::WriteSellRef11(New, 11 * p, Sl, Se, Co, Am, Sw);
				}
				Same(Old, New.ToArray(), FString::Printf(TEXT("позиций %d"), NP), Bad, First);
			}
			T.Group(TEXT("U6 ссылки продажи 5018 (5 × 11 б)"), Cases, Bad, First);
		}
		{   // 11006
			FRng R(0x11006);
			int32 Bad = 0; FString First;
			for (int32 k = 0; k < Cases; ++k)
			{
				const int32 O = R.I32(), I = R.I32(), C = (R.Coin() ? R.Range(20) - 5 : R.I32()), B = R.I32();
				Same(Old11006(O, I, C, B), UAsdaPremiumPostSubsystem::BuildToInvenRec11006(O, I, C, B), FString::Printf(TEXT("кол-во %d"), C), Bad, First);
			}
			T.Group(TEXT("U6 запись элитного ящика 11006 (10 б)"), Cases, Bad, First);
		}
		{   // 6096: слоты-приёмники по ВСЕМ коробкам при текущем инвентаре
			int32 Bad = 0, N = 0; FString First;
			TArray<int32> Packs;
			for (const FAsdaItemRecord& Rc : IT->GetAllItems()) { if (IT->IsPackageItem(Rc.ItemCode)) { Packs.Add(Rc.ItemCode); } }
			Packs.Add(0); Packs.Add(-1);
			// ★ 01.11.2026 (клиент 5.1.4): `OldReserve` — эталонное правило «под каждый спецпредмет свободная ячейка»; правило
			//   P100 (`asda.Item.ChargeStackReserve` = 1) его сознательно меняет и проверяется своим самотестом
			//   `asda.SelfTest.ChargeStackReserve`. Сверка равенства дедупа идёт при ключе 0; прежнее значение — назад после цикла.
			IConsoleVariable* CvCharge = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Item.ChargeStackReserve"));
			const int32 WasCharge = CvCharge ? CvCharge->GetInt() : -1;   // значение ключа до сверки
			// Запись с ТЕКУЩИМ приоритетом (как `AsdaCVarSave::Restore`): значение меняется, приоритет ключа — нет.
			auto SetCharge = [CvCharge](int32 V)
			{
				if (CvCharge) { CvCharge->Set(V, (EConsoleVariableFlags)((uint32)CvCharge->GetFlags() & (uint32)ECVF_SetByMask)); }
			};
			SetCharge(0);
			ON_SCOPE_EXIT { SetCharge(WasCharge); };
			for (const int32 C : Packs)
			{
				TArray<uint8> SO, SN; int32 CO = 0, SSO = 0, SBO = 0, CN = 0, SSN = 0, SBN = 0;
				const bool RO = OldReserve(Inv, IT, C, SO, CO, SSO, SBO);
				const bool RN = Inv->ReservePackageSlots(C, SN, CN, SSN, SBN);
				++N;
				if (!Same(SO, SN, FString::Printf(TEXT("коробка %d"), C), Bad, First)) { continue; }
				if (RO != RN || CO != CN || SSO != SSN || SBO != SBN)
				{
					if (Bad++ == 0) { First = FString::Printf(TEXT("коробка %d: было %d/%d/%d/%d стало %d/%d/%d/%d"), C, RO ? 1 : 0, CO, SSO, SBO, RN ? 1 : 0, CN, SSN, SBN); }
				}
			}
			UE_LOG(LogAsdaDedupW1A, Display, TEXT("САМОТЕСТ-В1А коробок в item_list: %d"), Packs.Num() - 2);
			T.Group(TEXT("U6 слоты-приёмники 6096 (ReservePackageSlots, 15 б + счётчики)"), N, Bad, First);
		}
	}
	{   // N10: RemoveIf ≡ Drain → RemoveAll → Push
		FRng R(0x4E10);
		int32 Bad = 0; FString First;
		for (int32 k = 0; k < 200; ++k)
		{
			TArray<int32> Src; const int32 Num = R.Range(30);
			for (int32 i = 0; i < Num; ++i) { Src.Add(R.Range(10)); }
			const int32 Keep = R.Range(10);
			TAsdaNetQueue<int32> QOld, QNew;
			for (const int32 V : Src) { QOld.Push(V); QNew.Push(V); }
			{
				TArray<int32> Tmp; QOld.Drain(Tmp);
				Tmp.RemoveAll([Keep](int32 V) { return V != Keep; });
				for (int32 V : Tmp) { QOld.Push(V); }
			}
			QNew.RemoveIf([Keep](int32 V) { return V != Keep; });
			TArray<int32> A, B; QOld.Drain(A); QNew.Drain(B);
			TArray<uint8> AB((const uint8*)A.GetData(), A.Num() * 4), BB((const uint8*)B.GetData(), B.Num() * 4);
			Same(AB, BB, FString::Printf(TEXT("случай %d"), k), Bad, First);
		}
		T.Group(TEXT("N10 TAsdaNetQueue::RemoveIf ≡ Drain→RemoveAll→Push"), 200, Bad, First);
	}
	UE_LOG(LogAsdaDedupW1A, Display, TEXT("САМОТЕСТ-В1А ИТОГ: ok=%d fail=%d"), T.Ok, T.Fail);
	if (bQuit) { AsdaSelfTestBatch::Quit(W); }
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaDedupW1ASelfTest(
	TEXT("asda.Dedup.W1ASelfTest"),
	TEXT("Самотест В1 (часть A) дедупа №2: побайтная сверка вынесенных сборщиков (U6/U17/N10) с прежней ручной сборкой. ")
	TEXT("asda.Dedup.W1ASelfTest [случаев=2000] [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		FAsdaDedupW1ASelfTest::Run(Args, W);
	}));

#endif // !UE_BUILD_SHIPPING
