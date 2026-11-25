// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ FAsdaItemRecordWriter — СБОРЩИК СИНТЕТИЧЕСКОЙ 57-БАЙТОВОЙ ЗАПИСИ ПРЕДМЕТА (U17 аудита дублей №2, волна В1, 11.10.2026;
//   `Reports/dedup_audit2/01_cpp_ui.md`, отчёт `wave1_A.md`).
//
// ★ ЗАЧЕМ. Около двадцати мест (тултипы по коду, витрины, лавка, обмен, очистка ячейки после самоцвета/гачи, самотесты)
//   собирали запись предмета руками: `TArray<uint8> R; R.SetNumZeroed(57); WrI32(+0); R[13] = …; WrI16(+14) …`.
//   Литерал 57 жил в дюжине мест вместо `FAsdaItemInstance::kRecordSize`, смещения полей — числами без имён, а одно
//   место писало `WrI32(+7, −1)` и затирало поле +9 следующей строкой (итог верный, запись хрупкая).
//   Здесь — ОДНА раскладка с именованными полями поверх `FAsdaPacketWriter(kRecordSize)` (границы проверяет писатель).
//
// ★★★ ЧЕГО ЗДЕСЬ НЕТ (закон `never-invent-only-original`). Писатель НЕ выбирает значения полей и НЕ решает, какие поля
//   заполнять: это делает вызывающий по своему эталонному обработчику (`CSlot_Init` + поля конкретного окна). Байты,
//   которых вызывающий не коснулся, остаются нулями — ровно как у прежнего `SetNumZeroed(57)`.
//   Раскладка (`FAsdaItemInstance`, `UI/AsdaInventorySubsystem.h`):
//     +0 i32 код · +4 u8 контейнер · +5 i16 слот · +7 i16 второй индекс · +9 i32 количество · +13 u8 прочность ·
//     +14 i16 вес стопки · +16/+18/+20/+22 i16 гнёзда · +24 u8 уровень заточки · +25/+26 u8 (смысл не выведен) ·
//     +27 i16 процент заточки · +29 пять пар (i16 код, i16 значение) с шагом 4 · +50 u8 флаг подсказки.
//
// ★ ДВА НАЧАЛЬНЫХ СОСТОЯНИЯ — НЕ ПУТАТЬ (оба живые, у каждого свои потребители):
//   • конструктор — ВСЕ 57 байт нули (прежний `SetNumZeroed(57)`);
//   • `PortClearSlots()` — то, что оставляет `FAsdaItemInstance::Clear()` порта: +5 и +7 = −1, прочее нули
//     (консольные команды тултипа строили запись поверх `Clear()`).
//
// Заголовок header-only (всё inline).
#pragma once
#include "CoreMinimal.h"
#include "AsdaPacketWriter.h"
#include "UI/AsdaInventorySubsystem.h"   // FAsdaItemInstance::kRecordSize и SetRaw

class FAsdaItemRecordWriter
{
public:
	// Длина записи — одна на весь порт.
	static constexpr int32 kSize = FAsdaItemInstance::kRecordSize;

	// Запись из 57 нулей (эталонный `CSlot_Init` в форме, которую порт использует для синтетических записей).
	FAsdaItemRecordWriter() : W(kSize, 0) {}

	// ── Поля записи (смещения — у объявления `FAsdaItemInstance`) ──
	FAsdaItemRecordWriter& Code(int32 V)           { W.I32(0, V);  return *this; }   // +0 i32 код предмета
	FAsdaItemRecordWriter& Container(int32 V)      { W.U8(4, V);   return *this; }   // +4 u8 контейнер
	FAsdaItemRecordWriter& Slot(int32 V)           { W.I16(5, V);  return *this; }   // +5 i16 слот
	FAsdaItemRecordWriter& Second(int32 V)         { W.I16(7, V);  return *this; }   // +7 i16 второй индекс
	FAsdaItemRecordWriter& Amount(int32 V)         { W.I32(9, V);  return *this; }   // +9 i32 количество (−1 — «очистить ячейку»)
	FAsdaItemRecordWriter& Durability(int32 V)     { W.U8(13, V);  return *this; }   // +13 u8 прочность
	FAsdaItemRecordWriter& StackWeight(int32 V)    { W.I16(14, V); return *this; }   // +14 i16 вес стопки
	FAsdaItemRecordWriter& Sowel(int32 K, int32 V) { W.I16(16 + 2 * K, V); return *this; }   // +16+2K i16 гнездо K (0…3)
	FAsdaItemRecordWriter& GemGrade(int32 V)       { W.U8(24, V);  return *this; }   // +24 u8 уровень заточки
	FAsdaItemRecordWriter& Byte25(int32 V)         { W.U8(25, V);  return *this; }   // +25 u8 (смысл не выведен)
	FAsdaItemRecordWriter& Byte26(int32 V)         { W.U8(26, V);  return *this; }   // +26 u8 (смысл не выведен; у тултипа «улучшений всего»)
	FAsdaItemRecordWriter& EnchantPercent(int32 V) { W.I16(27, V); return *this; }   // +27 i16 процент заточки
	// +29+4K: пара заточки K (0…4) — i16 код и i16 значение.
	FAsdaItemRecordWriter& EnchantPair(int32 K, int32 OptCode, int32 OptValue)
	{
		W.I16(29 + 4 * K, OptCode);
		W.I16(31 + 4 * K, OptValue);
		return *this;
	}
	// +29+4K: та же пара одним dword (источники, где пара уже упакована: лавка, обмен, биржа).
	FAsdaItemRecordWriter& EnchantDword(int32 K, int32 V) { W.I32(29 + 4 * K, V); return *this; }
	FAsdaItemRecordWriter& Flag50(int32 V)         { W.U8(50, V);  return *this; }   // +50 u8 флаг подсказки (`TooltipFlag200`/`Flag124`)

	// Состояние `FAsdaItemInstance::Clear()` порта: +5 и +7 = −1 (поверх нулей).
	FAsdaItemRecordWriter& PortClearSlots() { return Slot(-1).Second(-1); }

	// ── Выход ──
	TArray<uint8> ToArray() const { return W.ToArray(); }
	// Положить в экземпляр предмета (разбор полей — `SetRaw`/`SyncFromRaw`).
	void ApplyTo(FAsdaItemInstance& It) const { It.SetRaw(W.Data(), W.Num()); }
	// Байт записи (для строк лога).
	uint8 operator[](int32 I) const { return W[I]; }
	// false — запись за границу записи (ошибка индекса гнезда/пары у вызывающего).
	bool IsOk() const { return W.IsOk(); }

	// ── Готовые формы, которые повторялись дословно ──
	// Только код (тултип состава пака, витрина гачи): `CSlot_Init` + `[0] = код`.
	static TArray<uint8> ByCode(int32 ItemCode) { return FAsdaItemRecordWriter().Code(ItemCode).ToArray(); }
	// Код и количество (+9): простой билдер подсказки читает количество отсюда.
	static TArray<uint8> ByCodeQty(int32 ItemCode, int32 Qty) { return FAsdaItemRecordWriter().Code(ItemCode).Amount(Qty).ToArray(); }
	// Запись «очистить ячейку» для `UAsdaInventorySubsystem::ApplyExternalRecord`: контейнер, слот, количество −1
	//   (контракт общей точки: `amount(+9) == −1` ⇒ ячейка чистится; эталон — `memset(запись, 0, 0x39)`).
	static TArray<uint8> ClearCell(int32 Cont, int32 CellSlot) { return FAsdaItemRecordWriter().Container(Cont).Slot(CellSlot).Amount(-1).ToArray(); }
	// Запись ячейки для самотестов и эмуляций: код, контейнер, слот, второй индекс −1, количество.
	static TArray<uint8> Cell(int32 ItemCode, int32 Cont, int32 CellSlot, int32 Qty)
	{
		return FAsdaItemRecordWriter().Code(ItemCode).Container(Cont).Slot(CellSlot).Second(-1).Amount(Qty).ToArray();
	}

	// Прямой доступ к писателю — для полей, которых нет в именованном списке.
	FAsdaPacketWriter& Raw() { return W; }

private:
	FAsdaPacketWriter W;   // само тело записи (57 б)
};
