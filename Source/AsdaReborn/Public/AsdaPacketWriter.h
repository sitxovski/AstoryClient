// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ FAsdaPacketWriter — ЕДИНЫЙ СБОРЩИК ТЕЛА (payload) ИСХОДЯЩЕГО ПАКЕТА (волна 3 плана дублей, п.10;
//   `Reports/dedup_audit/00_PLAN.md`, кластер K2 аудита `02_cpp_core.md`, отчёт `wave3_packet_writer.md`).
//
// ★ ЗАЧЕМ. Сто отправителей `UAsdaNetSubsystem::Send*` повторяли одно и то же руками:
//   `uint8 P[N]; Memzero; AsdaBytes::WrI16(&P[o], (int32)(uint16)(int16)V); …; SendSecured(Op, P, N)`.
//   Ошибки этого шаблона видны только глазами: длина в вызове ≠ размер буфера, запись за границу
//   массива, забытое обнуление. Писатель убирает все три: размер задаётся ОДИН раз в конструкторе и
//   уходит в отправку через `Num()`, каждая запись проверяет границы, буфер всегда заполнен.
//
// ★★★ ЧЕГО ПИСАТЕЛЬ НЕ ДЕЛАЕТ (закон `old-server-protocol-compat`: сервер старый, провод не меняем):
//   • НЕ выравнивает и НЕ дописывает паддинг: каждое поле ложится ровно по смещению, которое передал
//     вызывающий; байты, которых не коснулись, остаются байтом заполнения конструктора.
//     Паддинг кадра (`a6`/`bPad`) — забота конверта (`SendSecured`/`SendMd5Signed`), не тела
//     ([[packet-size-check-no-padding]]: пад только там, где он был у эталона).
//   • НЕ выбирает конверт (с секретом / без — [[packet-envelope-two-kinds]]) и НЕ ставит гейты:
//     отправитель сам зовёт `SendSecured`/`SendMd5Signed`/`SendMenuPacket` (там `FAsdaOutgoingGate`).
//   • НЕ ставит терминатор строки: `Cp1251` пишет не более MaxBytes байт и возвращает сколько записал —
//     правило «имя в char[20] = 19 байт + ноль из заполнения» остаётся у вызывающего, как было.
//   • НЕ знает смысла полей: charId (i32) и userNum (i16) — разные поля ([[charid-vs-usernum]]), какой
//     метод звать, решает вызывающий по эталонному сборщику.
//
// ★ ЦЕЛЫЕ. Все записи little-endian через `AsdaBytes::WrI16/WrI32`. `I16` берёт МЛАДШИЕ 16 бит значения —
//   ровно то же, что давали все прежние формы (`WrI16(&P[o], V)`, `WrI16(&P[o], (int32)(uint16)(int16)V)`,
//   `uint16 U = (uint16)V; Memcpy(&P[o], &U, 2)`), поэтому приведения у вызова не нужны; смысловые
//   (кламп, «−1 = пусто», усечение charId до слова) вызывающий оставляет у себя.
//
// ★ ВЫХОД ЗА ГРАНИЦУ = ошибка программиста: `ensure` + запись пропускается + `IsOk()` = false. Прежний код
//   в этом случае портил стек. Для фиксированных раскладок это не наступает (доказано стендом
//   `asda.Net.PacketStand`, см. `AsdaPacketStand.cpp`).
//   ★ Каждый отправитель проверяет `IsOk()` ПЕРЕД конвертом (`AsdaPacketLayoutBroken` в `AsdaNetSubsystem.cpp`):
//   при ошибке пакет НЕ уходит (в Shipping `ensure` пуст — иначе тело ушло бы с байтом заполнения на месте поля),
//   отправитель возвращает false и пишет `LogAsdaNet` Error с опкодом.
//
// Заголовок header-only (всё inline): ни .cpp, ни экспорта не требуется.
#pragma once
#include "CoreMinimal.h"
#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"   // FAsdaLoginData::EncodeCp1251 — единственный энкодер cp1251 порта

class FAsdaPacketWriter
{
public:
	// Тело ровно InSize байт, каждый байт = FillByte. 0 — как прежний `Memzero`; 0xFF — как `Memset(-1)`
	//   эталонных конструкторов («пустые поля = −1»).
	explicit FAsdaPacketWriter(int32 InSize, uint8 FillByte = 0)
	{
		Buf.Init(FillByte, FMath::Max(0, InSize));
	}

	// u8 по смещению Off: младшие 8 бит V — ровно то, что давало прежнее присваивание `P[o] = V` в uint8
	//   (параметр int32, чтобы выражения `bAccept ? 1 : 0`, `Containers[i]`, `CurrentChannel + 1` не требовали
	//   приведений у вызова; усечение то же, что у I16 для 16 бит).
	FAsdaPacketWriter& U8(int32 Off, int32 V)
	{
		if (Fits(Off, 1)) { Buf[Off] = (uint8)V; }
		return *this;
	}

	// 16 бит LE: младшие 16 бит V (знаковое и беззнаковое на проводе совпадают).
	FAsdaPacketWriter& I16(int32 Off, int32 V)
	{
		if (Fits(Off, 2)) { AsdaBytes::WrI16(Buf.GetData() + Off, V); }
		return *this;
	}

	// То же, что I16, для беззнаковых источников (номера, маски) — читается честнее у вызова.
	FAsdaPacketWriter& U16(int32 Off, uint32 V)
	{
		return I16(Off, (int32)(uint16)V);
	}

	// 32 бита LE со знаком.
	FAsdaPacketWriter& I32(int32 Off, int32 V)
	{
		if (Fits(Off, 4)) { AsdaBytes::WrI32(Buf.GetData() + Off, V); }
		return *this;
	}

	// 32 бита LE без знака (маски карт, userIdx).
	FAsdaPacketWriter& U32(int32 Off, uint32 V)
	{
		return I32(Off, (int32)V);
	}

	// float IEEE-754 LE (побайтовая копия, как `Memcpy(&P[o], &F, 4)` на x64).
	FAsdaPacketWriter& F32(int32 Off, float V)
	{
		if (Fits(Off, 4)) { FMemory::Memcpy(Buf.GetData() + Off, &V, 4); }
		return *this;
	}

	// Сырые байты (копия записи предмета, критерии поиска). N <= 0 — ничего не пишется.
	FAsdaPacketWriter& Bytes(int32 Off, const uint8* Src, int32 N)
	{
		if (N <= 0) { return *this; }
		if (!Src) { Fail(Off, N); return *this; }
		if (Fits(Off, N)) { FMemory::Memcpy(Buf.GetData() + Off, Src, N); }
		return *this;
	}

	// N байт значением V (эталонные «−1 во все слоты» поверх нулевого тела).
	FAsdaPacketWriter& Fill(int32 Off, int32 N, uint8 V)
	{
		if (N <= 0) { return *this; }
		if (Fits(Off, N)) { FMemory::Memset(Buf.GetData() + Off, V, N); }
		return *this;
	}

	// Строка в cp1251 (`FAsdaLoginData::EncodeCp1251`): не более MaxBytes байт с Off, БЕЗ терминатора
	//   и без обнуления хвоста. Возвращает число записанных байт (как прежние вызовы энкодера).
	int32 Cp1251(int32 Off, const FString& S, int32 MaxBytes)
	{
		if (MaxBytes <= 0 || !Fits(Off, MaxBytes)) { return 0; }
		return FAsdaLoginData::EncodeCp1251(S, Buf.GetData() + Off, MaxBytes);
	}

	// Замена байта From на To в диапазоне [Off, Off+N) — эталонный `bufferReplaceByte` (апостроф → `).
	FAsdaPacketWriter& ReplaceByte(int32 Off, int32 N, uint8 From, uint8 To)
	{
		if (N <= 0 || !Fits(Off, N)) { return *this; }
		for (int32 i = Off; i < Off + N; ++i) { if (Buf[i] == From) { Buf[i] = To; } }
		return *this;
	}

	// Окно [Off, Off+N) для ОБЩИХ сборщиков, пишущих в сырой указатель (`AsdaBuildItemRef11`,
	//   `BuildExchangeHeader15`). При выходе за границу — ensure и указатель на отдельный черновик
	//   того же размера: общий сборщик не упадёт, а тело останется нетронутым (`IsOk()` = false).
	uint8* Raw(int32 Off, int32 N)
	{
		if (Fits(Off, N)) { return Buf.GetData() + Off; }
		Scratch.Init(0, FMath::Max(1, N));
		return Scratch.GetData();
	}

	// Готовое тело для конверта: указатель и РОВНО размер конструктора.
	const uint8* Data() const { return Buf.GetData(); }
	int32 Num() const { return Buf.Num(); }

	// ★ U6 (В1 дедупа №2, 11.10.2026): копия тела обычным `TArray<uint8>` — для ЗАПИСЕЙ, которые окно собирает
	//   в список (`Rows.Add(W.ToArray())`) и отдаёт отправителю пакета, и для синтетических записей предмета.
	//   Байты те же, что `Data()/Num()`; при ошибке раскладки (`!IsOk()`) копия всё равно полного размера —
	//   решение «слать/не слать» остаётся у вызывающего, как у прежних ручных буферов.
	TArray<uint8> ToArray() const { return TArray<uint8>(Buf.GetData(), Buf.Num()); }

	// Байт тела (для строк лога); вне границ — 0.
	uint8 operator[](int32 I) const { return Buf.IsValidIndex(I) ? Buf[I] : 0; }

	// false — была попытка записи за границу тела (ошибка раскладки у вызывающего).
	bool IsOk() const { return bOk; }

private:
	// Проверка окна записи; нарушение фиксируется один раз на запись.
	bool Fits(int32 Off, int32 N)
	{
		if (Off >= 0 && N >= 0 && Off <= Buf.Num() - N) { return true; }
		Fail(Off, N);
		return false;
	}

	// Учёт ошибки раскладки: ensure (в логе со стеком) + признак для вызывающего.
	void Fail(int32 Off, int32 N)
	{
		bOk = false;
		ensureMsgf(false, TEXT("FAsdaPacketWriter: запись [%d, +%d) вне тела %d б"), Off, N, Buf.Num());
	}

	TArray<uint8, TInlineAllocator<256>> Buf;   // само тело
	TArray<uint8> Scratch;                      // черновик для Raw() при ошибке раскладки
	bool bOk = true;                            // не было записи за границу
};
