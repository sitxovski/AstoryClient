// Copyright (c) 2026 @sitxovski (Discord)
#include "Login/AsdaLoginData.h"
#include "Login/AsdaServerAddress.h"   // ★ 16.10.2026: домен сервера (LoadLoginServer)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ 09.10.2026: язык сессии для Loc/LocRaw
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached
#include "Login/AsdaGameText.h"
#include "UI/AsdaMarkup.h"               // ★ единый парсер разметки (StripTags)
#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"     // [ПОРТ] 08.10.2026: asda.Text.WireCodec (кодек свободного текста)

// ★ [ПОРТ] 08.10.2026: НАСТОЯЩИЙ cp1251 (бывший `DecodeCp1251`). Публичный `DecodeCp1251` ниже теперь — гибрид.
FString FAsdaLoginData::DecodeCp1251Strict(const uint8* Data, int32 MaxLen)
{
	FString S;
	S.Reserve(MaxLen);
	for (int32 i = 0; i < MaxLen && Data[i]; ++i)
	{
		const uint8 c = Data[i];
		TCHAR w;
		if (c < 0x80)             w = (TCHAR)c;
		else if (c >= 0xC0)       w = (TCHAR)(0x0410 + (c - 0xC0));  // А..я
		else if (c == 0xA8)       w = (TCHAR)0x0401;                 // Ё
		else if (c == 0xB8)       w = (TCHAR)0x0451;                 // ё
		else if (c == 0xAB)       w = (TCHAR)0x00AB;                 // «
		else if (c == 0xBB)       w = (TCHAR)0x00BB;                 // »
		else if (c == 0x96)       w = (TCHAR)0x2013;                 // –
		else if (c == 0x97)       w = (TCHAR)0x2014;                 // —
		else if (c == 0x85)       w = (TCHAR)0x2026;                 // …
		else if (c == 0xB9)       w = (TCHAR)0x2116;                 // №
		else                      w = TEXT('?');
		S.AppendChar(w);
	}
	return S;
}

// ★ D9 (аудит `code_dedup_audit_2.md`): ЕДИНСТВЕННЫЙ энкодер cp1251 порта — обратный к декодеру выше.
//   Таблица: ASCII как есть, А..я → 0xC0.., Ё → 0xA8, ё → 0xB8, всё прочее → '?'. Буфер не обнуляется
//   и не терминируется здесь: у чата длина уезжает отдельным полем, а у имени в char[20] терминатор
//   ставит вызывающий.
//   ★ ИСПРАВЛЕНИЕ АСИММЕТРИИ (`fix_helpers_dedup.md`): декодер отображает « » – — … № (cp1251
//   0xAB/0xBB/0x96/0x97/0x85/0xB9), а энкодер их терял в '?'. Это те же валидные cp1251-байты, что
//   принимает старый сервер, поэтому «туда-обратно» теперь без потерь. Добавлены обратные ветки.
// ★ [ПОРТ] 08.10.2026: НАСТОЯЩИЙ cp1251 (бывший `EncodeCp1251`). Публичный `EncodeCp1251` ниже теперь — гибрид.
int32 FAsdaLoginData::EncodeCp1251Strict(const FString& In, uint8* Out, int32 MaxBytes)
{
	if (!Out || MaxBytes <= 0) { return 0; }
	int32 N = 0;
	for (int32 i = 0; i < In.Len() && N < MaxBytes; ++i)
	{
		const TCHAR C = In[i];
		uint8 B;
		if (C < 128)                          { B = (uint8)C; }
		else if (C >= 0x0410 && C <= 0x044F)  { B = (uint8)(C - 0x0410 + 0xC0); }   // А..я
		else if (C == 0x0401)                 { B = 0xA8; }                          // Ё
		else if (C == 0x0451)                 { B = 0xB8; }                          // ё
		else if (C == 0x00AB)                 { B = 0xAB; }                          // «
		else if (C == 0x00BB)                 { B = 0xBB; }                          // »
		else if (C == 0x2013)                 { B = 0x96; }                          // –
		else if (C == 0x2014)                 { B = 0x97; }                          // —
		else if (C == 0x2026)                 { B = 0x85; }                          // …
		else if (C == 0x2116)                 { B = 0xB9; }                          // №
		else                                  { B = (uint8)'?'; }
		Out[N++] = B;
	}
	return N;
}

// ═══ [ПОРТ] МНОГОЯЗЫЧНЫЙ СВОБОДНЫЙ ТЕКСТ (08.10.2026) — разбор в шапке объявлений (AsdaLoginData.h) ═══

// Переключатель кодека провода. 1 — гибрид cp1251/UTF-8 (умолчание), 0 — откат на чистый cp1251.
static ASDA_DEV_CVAR(int32, CVarAsdaTextWireCodec,
	TEXT("asda.Text.WireCodec"),
	1,
	TEXT("[ПОРТ] Кодек текста игрока на проводе (RU+EN+AR):\n")
	TEXT(" 1 = полный гибрид: представимое в cp1251 — cp1251 байт-в-байт (как раньше); иначе представимое в cp1256\n")
	TEXT("     (арабский+латиница) — маркер 0x98 + cp1256 (1 байт на букву); иначе UTF-8 (смешанный RU+AR и прочее).\n")
	TEXT("     Приём: 0x98 в начале ⇒ cp1256; строгий валидный UTF-8 ⇒ UTF-8; иначе cp1251.\n")
	TEXT(" 2 = без маркера cp1256: cp1251, иначе UTF-8 (арабский — 2 байта на букву).\n")
	TEXT(" 0 = откат: только cp1251 (арабский → '?', как до 08.10.2026)."),
	ECVF_Default);

bool FAsdaLoginData::IsWireUtf8Enabled()
{
	return CVarAsdaTextWireCodec.GetValueOnAnyThread() != 0;
}

// Представим ли символ в cp1251 — РОВНО таблица `EncodeCp1251` (иначе гибрид и энкодер разойдутся).
static bool AsdaIsCp1251Char(TCHAR C)
{
	return C < 128
		|| (C >= 0x0410 && C <= 0x044F)
		|| C == 0x0401 || C == 0x0451
		|| C == 0x00AB || C == 0x00BB
		|| C == 0x2013 || C == 0x2014 || C == 0x2026 || C == 0x2116;
}

bool FAsdaLoginData::IsCp1251Representable(const FString& In)
{
	for (int32 i = 0; i < In.Len(); ++i)
	{
		if (!AsdaIsCp1251Char(In[i])) { return false; }
	}
	return true;
}

// Режим кодека: 0 — только cp1251, 1 — полный гибрид (cp1251 / 0x98+cp1256 / UTF-8), 2 — cp1251 / UTF-8.
static int32 AsdaWireCodecMode()
{
	return CVarAsdaTextWireCodec.GetValueOnAnyThread();
}

// cp1256 (Windows-1256, арабская) 0x80..0xFF → Юникод. Таблица Python `codecs` cp1256 — все 128 байт определены и
//   различны (поэтому круговой путь P17 «байты → cp1256 → байты» на сервере точен).
static const uint16 GAsdaCp1256Hi[128] =
{
	0x20AC, 0x067E, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0679, 0x2039, 0x0152, 0x0686, 0x0698, 0x0688,
	0x06AF, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x06A9, 0x2122, 0x0691, 0x203A, 0x0153, 0x200C, 0x200D, 0x06BA,
	0x00A0, 0x060C, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x06BE, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
	0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x00B9, 0x061B, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x061F,
	0x06C1, 0x0621, 0x0622, 0x0623, 0x0624, 0x0625, 0x0626, 0x0627,
	0x0628, 0x0629, 0x062A, 0x062B, 0x062C, 0x062D, 0x062E, 0x062F,
	0x0630, 0x0631, 0x0632, 0x0633, 0x0634, 0x0635, 0x0636, 0x00D7,
	0x0637, 0x0638, 0x0639, 0x063A, 0x0640, 0x0641, 0x0642, 0x0643,
	0x00E0, 0x0644, 0x00E2, 0x0645, 0x0646, 0x0647, 0x0648, 0x00E7,
	0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x0649, 0x064A, 0x00EE, 0x00EF,
	0x064B, 0x064C, 0x064D, 0x064E, 0x00F4, 0x064F, 0x0650, 0x00F7,
	0x0651, 0x00F9, 0x0652, 0x00FB, 0x00FC, 0x200E, 0x200F, 0x06D2,
};

// Символ → байт cp1256 (−1 — непредставим). ASCII — как есть.
static int32 AsdaCp1256Byte(TCHAR C)
{
	if (C < 0x80) { return (int32)C; }
	for (int32 i = 0; i < 128; ++i) { if ((TCHAR)GAsdaCp1256Hi[i] == C) { return 0x80 + i; } }
	return -1;
}

bool FAsdaLoginData::IsCp1256Representable(const FString& In)
{
	for (int32 i = 0; i < In.Len(); ++i)
	{
		if (In[i] == 0 || AsdaCp1256Byte(In[i]) < 0) { return false; }
	}
	return true;
}

// ★ [ПОРТ] Ревизия arabic_text_REVIEW С1 (08.10.2026): байты cp1251 (Out[0..N)), которые приёмник `DecodeWireText` примет
//   за UTF-8 — ровно его условие: строгий UTF-8 с допуском обрубка-хвоста И хотя бы одна многобайтная последовательность.
//   Такие есть у редких русских строк: «Я…» = DF 85, «Сё» = D1 B8, «Х«» = D5 AB, «Сё Я» = D1 B8 20 DF (обрубок-хвост → «Я»
//   терялась). Их отправляем веткой UTF-8 — приём её поймёт однозначно. Откат: `asda.Text.WireCodec 0` (чистый cp1251).
static bool AsdaCp1251BytesLookUtf8(const uint8* Out, int32 N)
{
	bool bMulti = false;
	int32 Valid = 0;
	return FAsdaLoginData::IsStrictUtf8(Out, N, /*bAllowTruncTail*/true, bMulti, Valid) && bMulti;
}

// То же для строки целиком (представимой в cp1251): закодировать без обрезки и проверить.
static bool AsdaCp1251StringLooksUtf8(const FString& In)
{
	TArray<uint8> Tmp;
	Tmp.SetNumUninitialized(In.Len() + 1);
	const int32 N = FAsdaLoginData::EncodeCp1251Strict(In, Tmp.GetData(), Tmp.Num());
	return AsdaCp1251BytesLookUtf8(Tmp.GetData(), N);
}

// Ветка кодека для строки (без записи): 0 — cp1251, 1 — маркер 0x98 + cp1256, 2 — UTF-8.
static int32 AsdaWireBranch(const FString& In)
{
	const int32 Mode = AsdaWireCodecMode();
	if (Mode == 0) { return 0; }
	// С1: cp1251-представимая строка, байты которой приёмник счёл бы UTF-8, — веткой UTF-8.
	if (FAsdaLoginData::IsCp1251Representable(In)) { return AsdaCp1251StringLooksUtf8(In) ? 2 : 0; }
	if (Mode == 1 && FAsdaLoginData::IsCp1256Representable(In)) { return 1; }
	return 2;
}

// Кодпойнт в позиции i (UTF-16 → UTF-32, с суррогатной парой); OutUnits — сколько TCHAR съедено.
//   Одиночный (битый) суррогат → U+FFFD, чтобы в UTF-8 не уехала невалидная последовательность.
static uint32 AsdaReadCodepoint(const FString& S, int32 i, int32& OutUnits)
{
	const uint32 C = (uint32)S[i];
	OutUnits = 1;
	if (C >= 0xD800 && C <= 0xDBFF && i + 1 < S.Len())
	{
		const uint32 Lo = (uint32)S[i + 1];
		if (Lo >= 0xDC00 && Lo <= 0xDFFF) { OutUnits = 2; return 0x10000 + ((C - 0xD800) << 10) + (Lo - 0xDC00); }
	}
	if (C >= 0xD800 && C <= 0xDFFF) { return 0xFFFD; }
	return C;
}

// Кодпойнт → UTF-8 (1..4 байта) в Tmp; возвращает длину.
static int32 AsdaUtf8Bytes(uint32 Cp, uint8 Tmp[4])
{
	if (Cp < 0x80)    { Tmp[0] = (uint8)Cp; return 1; }
	if (Cp < 0x800)   { Tmp[0] = (uint8)(0xC0 | (Cp >> 6)); Tmp[1] = (uint8)(0x80 | (Cp & 0x3F)); return 2; }
	if (Cp < 0x10000) { Tmp[0] = (uint8)(0xE0 | (Cp >> 12)); Tmp[1] = (uint8)(0x80 | ((Cp >> 6) & 0x3F));
	                    Tmp[2] = (uint8)(0x80 | (Cp & 0x3F)); return 3; }
	Tmp[0] = (uint8)(0xF0 | (Cp >> 18)); Tmp[1] = (uint8)(0x80 | ((Cp >> 12) & 0x3F));
	Tmp[2] = (uint8)(0x80 | ((Cp >> 6) & 0x3F)); Tmp[3] = (uint8)(0x80 | (Cp & 0x3F)); return 4;
}

int32 FAsdaLoginData::EncodeWireText(const FString& In, uint8* Out, int32 MaxBytes)
{
	if (!Out || MaxBytes <= 0) { return 0; }
	// Гибрид: представимое в cp1251 (или кодек выключен) — прежний путь байт-в-байт.
	const int32 Branch = AsdaWireBranch(In);
	if (Branch == 0)
	{
		const int32 N0 = EncodeCp1251Strict(In, Out, MaxBytes);
		// С1: строка целиком не похожа на UTF-8, а её ОБРЕЗАННАЯ полем голова может оказаться похожей («Сё Яблоко» → «Сё Я») —
		//   тогда та же голова уходит веткой UTF-8 (ниже), иначе приём отрежет последнюю букву.
		if (AsdaWireCodecMode() == 0 || !AsdaCp1251BytesLookUtf8(Out, N0)) { return N0; }
	}
	else if (Branch == 1)
	{
		// Маркер 0x98 (единственный НЕопределённый байт cp1251 — его не выдаёт ни один cp1251-энкодер, и он не может начинать
		//   UTF-8) + cp1256 по байту на символ. Буквы без маркера смысла не имеют — нет места хотя бы на одну, пишем 0 байт.
		if (MaxBytes < 2) { return 0; }
		Out[0] = kCp1256Marker;
		int32 N = 1;
		for (int32 i = 0; i < In.Len() && N < MaxBytes; ++i) { Out[N++] = (uint8)AsdaCp1256Byte(In[i]); }
		return N;
	}
	int32 N = 0;
	for (int32 i = 0; i < In.Len(); )
	{
		int32 Units = 1;
		const uint32 Cp = AsdaReadCodepoint(In, i, Units);
		if (Cp == 0) { break; }                       // NUL внутри строки — конец (как strcpy у оригинала)
		uint8 Tmp[4];
		const int32 L = AsdaUtf8Bytes(Cp, Tmp);
		if (N + L > MaxBytes) { break; }              // ★ символ не влезает ЦЕЛИКОМ — стоп, посреди не режем
		FMemory::Memcpy(Out + N, Tmp, L);
		N += L;
		i += Units;
	}
	return N;
}

int32 FAsdaLoginData::WireTextByteLen(const FString& In)
{
	const int32 Branch = AsdaWireBranch(In);
	if (Branch == 0) { return In.Len(); }       // cp1251: символ = байт
	if (Branch == 1) { return In.Len() + 1; }   // маркер + cp1256: символ = байт
	int32 N = 0;
	for (int32 i = 0; i < In.Len(); )
	{
		int32 Units = 1;
		uint8 Tmp[4];
		N += AsdaUtf8Bytes(AsdaReadCodepoint(In, i, Units), Tmp);
		i += Units;
	}
	return N;
}

FString FAsdaLoginData::TruncateToWireBytes(const FString& In, int32 MaxBytes)
{
	if (WireTextByteLen(In) <= MaxBytes) { return In; }
	// Длина на проводе монотонна по префиксу только внутри одного режима; режим может смениться (голова без арабского
	//   уйдёт в cp1251), поэтому ищем самую длинную голову прямым перебором по границам символов.
	FString Best;
	for (int32 i = 0; i < In.Len(); )
	{
		int32 Units = 1;
		AsdaReadCodepoint(In, i, Units);
		const FString Head = In.Left(i + Units);
		if (WireTextByteLen(Head) > MaxBytes) { break; }
		Best = Head;
		i += Units;
	}
	return Best;
}

bool FAsdaLoginData::IsStrictUtf8(const uint8* D, int32 Len, bool bAllowTruncTail, bool& OutMulti, int32& OutValidLen)
{
	OutMulti = false;
	OutValidLen = 0;
	int32 i = 0;
	while (i < Len)
	{
		const uint8 c = D[i];
		if (c < 0x80) { ++i; continue; }
		int32 Need; uint32 Cp; uint32 Min;
		if (c >= 0xC2 && c <= 0xDF)      { Need = 1; Cp = c & 0x1F; Min = 0x80; }
		else if (c >= 0xE0 && c <= 0xEF) { Need = 2; Cp = c & 0x0F; Min = 0x800; }
		else if (c >= 0xF0 && c <= 0xF4) { Need = 3; Cp = c & 0x07; Min = 0x10000; }
		else { OutValidLen = i; return false; }                     // 0x80..0xC1, 0xF5..0xFF — не ведущий байт
		if (i + Need >= Len)                                         // последовательность не помещается в буфер
		{
			// Обрубок в самом конце (длину зажал сервер) — допустим только он, и только если все оставшиеся байты — продолжения.
			bool bTail = bAllowTruncTail;
			for (int32 k = i + 1; k < Len && bTail; ++k) { bTail = (D[k] & 0xC0) == 0x80; }
			OutValidLen = i;
			return bTail;
		}
		for (int32 k = 1; k <= Need; ++k)
		{
			const uint8 t = D[i + k];
			if ((t & 0xC0) != 0x80) { OutValidLen = i; return false; }
			Cp = (Cp << 6) | (t & 0x3F);
		}
		if (Cp < Min || Cp > 0x10FFFF || (Cp >= 0xD800 && Cp <= 0xDFFF)) { OutValidLen = i; return false; }
		OutMulti = true;
		i += Need + 1;
	}
	OutValidLen = Len;
	return true;
}

FString FAsdaLoginData::DecodeWireText(const uint8* Data, int32 MaxLen)
{
	if (!Data || MaxLen <= 0) { return FString(); }
	int32 Len = 0;
	while (Len < MaxLen && Data[Len]) { ++Len; }                    // как strlen в пределах поля
	if (!IsWireUtf8Enabled()) { return DecodeCp1251Strict(Data, Len); }
	if (Len >= 1 && Data[0] == kCp1256Marker)
	{
		// cp1256 после маркера (в режиме 2 маркер тоже понимаем — приём не должен зависеть от того, как настроен отправитель).
		FString S; S.Reserve(Len - 1);
		for (int32 i = 1; i < Len; ++i) { const uint8 c = Data[i]; S.AppendChar(c < 0x80 ? (TCHAR)c : (TCHAR)GAsdaCp1256Hi[c - 0x80]); }
		return S;
	}
	bool bMulti = false;
	int32 Valid = 0;
	if (!IsStrictUtf8(Data, Len, /*bAllowTruncTail*/true, bMulti, Valid) || !bMulti)
	{
		return DecodeCp1251Strict(Data, Len);                        // ASCII/cp1251 (старые строки, нотисы, чужие клиенты)
	}
	// Строгий UTF-8 → FString (UE берёт UTF-8 как есть; обрубок-хвост уже отрезан Valid).
	const auto Conv = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Data), Valid);
	return FString(Conv.Length(), Conv.Get());
}

// ★★★ [ПОРТ] 08.10.2026 (RU+EN+AR «везде», отчёт `Reports/owner_1008/arabic_text.md`): ПУБЛИЧНЫЕ `EncodeCp1251` /
//   `DecodeCp1251` — теперь ГИБРИД провода (`EncodeWireText` / `DecodeWireText`). Имена сохранены сознательно: ими пользуются
//   ~100 точек (имена, клан, питомец, лавка, «о себе», заметки — инвентаризация `Reports/owner_1008/arabic_client_fields.md`),
//   часть — в файлах, которые параллельно правят другие агенты. Представимое в cp1251 кодируется БАЙТ-В-БАЙТ как раньше
//   (русский/английский — без единого изменения на проводе и в базе), поэтому переключение безопасно для всего уже записанного.
//   База хранит байты прозрачно (P17 — круговой путь cp1256 точен для всех 255 байт; доказано SELECT-ом на живом 08.10).
//   Настоящий cp1251 — `EncodeCp1251Strict` / `DecodeCp1251Strict`. Откат всего разом: `asda.Text.WireCodec 0`.
int32 FAsdaLoginData::EncodeCp1251(const FString& In, uint8* Out, int32 MaxBytes)
{
	return EncodeWireText(In, Out, MaxBytes);
}

FString FAsdaLoginData::DecodeCp1251(const uint8* Data, int32 MaxLen)
{
	return DecodeWireText(Data, MaxLen);
}

int32 FAsdaLoginData::WireLimitBytes(const FString& In, int32 LimitBytes, int32 FieldBytes)
{
	// Маркер cp1256 «бесплатен», если поле вмещает лишний байт: арабский получает столько же букв, сколько давал оригинал
	//   (в арабской сборке — 1 байт cp1256 на букву без маркера).
	const int32 Extra = (AsdaWireBranch(In) == 1) ? 1 : 0;
	return FMath::Min(FieldBytes, LimitBytes + Extra);
}

FString FAsdaLoginData::TruncateToWireLimit(const FString& In, int32 LimitBytes, int32 FieldBytes)
{
	if (WireTextByteLen(In) <= WireLimitBytes(In, LimitBytes, FieldBytes)) { return In; }
	FString Best;
	for (int32 i = 1; i <= In.Len(); ++i)
	{
		// по границе символа: не оставляем половину суррогатной пары
		if (i < In.Len() && In[i - 1] >= 0xD800 && In[i - 1] <= 0xDBFF) { continue; }
		const FString Head = In.Left(i);
		if (WireTextByteLen(Head) > WireLimitBytes(Head, LimitBytes, FieldBytes)) { break; }
		Best = Head;
	}
	return Best;
}

// ═══ [ПОРТ] 08.10.2026: ДОПУСТИМЫЕ СИМВОЛЫ ИМЕНИ ПЕРСОНАЖА — из ЛОКАЛЬНЫХ правил ОРИГИНАЛА ═══
//   Эталон `validateStringByLocale@0x956090` (ui_core.c:191) выбирает правило по локали сборки (`CObj_GetField14(&g_hMainWnd)`):
//     case 2/6 `isValidNameCharAscii@0x9570A0`    — A-Z a-z 0-9 (так собрана русская сборка-эталон порта);
//     case 7   `isValidNameCharCyrillic@0x957250` — A-Z a-z 0-9 + байты cp1251 ≥ 0xC0 (А..я) + 0xA8/0xB8 (Ё/ё);
//     case 8   `isDelimiterChar@0x957610`         — ★ МИСНЕЙМ: это правило АРАБСКОЙ локали (cp1256): A-Z a-z 0-9 + байты
//              ≥ 0xC0, кроме 0xD7 («×» в cp1256).
//   Многоязычный порт: имя допустимо, если ЦЕЛИКОМ проходит кириллическое ИЛИ ЦЕЛИКОМ арабское правило оригинала (как имя,
//   созданное в русской или в арабской сборке). Смешение кириллицы с арабицей в одном имени недопустимо — его не пустила бы
//   ни одна сборка оригинала. Откат на правило эталона порта (только ASCII): `asda.Text.NameLocale 0`.
//   ★ 08.10.2026 РЕШЕНИЕ ВЛАДЕЛЬЦА: имена персонажей — ТОЛЬКО латиница (по умолчанию 0); многоязычное правило (1)
//   оставлено ключом, но выключено.
static ASDA_DEV_CVAR(int32, CVarAsdaTextNameLocale,
	TEXT("asda.Text.NameLocale"),
	0,
	TEXT("[ПОРТ] Допустимые символы имени персонажа:\n")
	TEXT(" 1 = многоязычно: имя целиком по правилу кириллической (case 7) ИЛИ арабской (case 8) локали оригинала;\n")
	TEXT(" 0 = эталон порта (case 2/6): только A-Z a-z 0-9."),
	ECVF_Default);

bool FAsdaLoginData::IsValidCharNameByLocale(const FString& Name)
{
	if (Name.IsEmpty()) { return false; }
	auto IsAsciiOk = [](TCHAR C) { return (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9'); };
	bool bAscii = true, bCyr = true, bAr = true;
	for (int32 i = 0; i < Name.Len(); ++i)
	{
		const TCHAR C = Name[i];
		const bool bA = IsAsciiOk(C);
		bAscii &= bA;
		// case 7: байт cp1251 ≥ 0xC0 (А..я) или Ё/ё (0xA8/0xB8)
		bCyr &= bA || (C >= 0x0410 && C <= 0x044F) || C == 0x0401 || C == 0x0451;
		// case 8: байт cp1256 ≥ 0xC0, кроме 0xD7
		const int32 B = AsdaCp1256Byte(C);
		bAr &= bA || (B >= 0xC0 && B != 0xD7);
	}
	if (CVarAsdaTextNameLocale.GetValueOnAnyThread() == 0) { return bAscii; }
	return bCyr || bAr;
}

// Примечание: рантайм-загрузчики error list.bin (StringTable) и loginhelpertext.bin удалены — весь текст
//   теперь берётся из ИМПОРТИРОВАННЫХ ассетов (DT_GameText и др., через offline-пайплайн import_text_tables.py).
//   Форматы .bin задокументированы в Reports (localization.md §1.1 / login_helper_ui_reverse.md §2).
// ★ Аудит №4 (B2): путь таблицы игрового текста — одно объявление на файл (был выписан дважды).
static const TCHAR* kGameTextDT = FAsdaLoginData::kGameTextPath;   // путь — объявление в шапке (15.09.2026, dt_gc_hold)

FString FAsdaLoginData::Loc(int32 Id)
{
	// Текст из импортированного ассета проекта DT_GameText (error list.bin → CSV → DataTable).
	// ★★★ Аудит №4 (B2): здесь и в `LocRaw` ниже жили ДВА НЕЗАВИСИМЫХ статических кэша ОДНОЙ И ТОЙ
	//   ЖЕ таблицы — то есть таблица могла быть загружена дважды. Оба заменены общей точкой
	//   `AsdaDt::Cached`: кэш один на путь, значит и на обе функции.
	// ★★★ [ПОРТ] 09.10.2026 (мультиязычность): текст языка сессии — языковой оверлей `DT_Loc_GameText_<lang>` поверх
	//   русской строки (`FAsdaLang::Resolve`, цепочка «язык → en → ru»). Русская сессия — ровно прежний путь. Строка,
	//   которой у RU нет, а у EN-оригинала есть (110937…), в английской сессии тоже находится.
	return StripMarkup(LocRaw(Id));
}

FString FAsdaLoginData::LocRaw(int32 Id)
{
	// Как Loc, но БЕЗ StripMarkup — сырой текст с тегами для форматированного показа.
	// ★ Аудит №4 (B2): та же общая точка и тот же кэш, что у `Loc` (см. разбор там).
	const FAsdaGameTextRow* R = AsdaDt::RowByInt<FAsdaGameTextRow>(kGameTextDT, Id, TEXT("LocRaw"));
	const FString Ru = R ? R->SourceString : FString();
	return FAsdaLang::ResolveId(TEXT("GameText"), Id, nullptr, Ru);   // ★ 09.10.2026: язык сессии (см. `Loc`)
}

bool FAsdaLoginData::LoadSavedId(bool& OutSave, FString& OutId)
{
	const FString Path = LocalCheckPath();     // ★ только Saved/ игрока, не каталог оригинальной игры
	TArray<uint8> Raw;
	if (!FFileHelper::LoadFileToArray(Raw, *Path) || Raw.Num() < 33)
	{
		OutSave = false;
		OutId.Empty();
		return false;
	}
	OutSave = Raw[0] != 0;
	uint8 Buf[33];
	FMemory::Memcpy(Buf, Raw.GetData() + 1, 32);
	Buf[32] = 0;
	OutId = DecodeCp1251(Buf, 32);  // ID обычно ASCII, cp1251-декод безопасен
	return true;
}

bool FAsdaLoginData::SaveSavedId(bool Save, const FString& Id)
{
	const FString Path = LocalCheckPath();     // ★ только Saved/ игрока, не каталог оригинальной игры
	uint8 Buf[33];
	FMemory::Memzero(Buf, sizeof(Buf));
	Buf[0] = Save ? 1 : 0;
	if (Save)
	{
		// ID в ASCII (логин), обрезка до 32 (как LimitText / буфер клиента).
		const FString A = Id.Left(32);
		for (int32 i = 0; i < A.Len() && i < 32; ++i)
		{
			const TCHAR c = A[i];
			Buf[1 + i] = (c < 128) ? (uint8)c : (uint8)'?';
		}
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
	TArray<uint8> Out;
	Out.Append(Buf, 33);
	return FFileHelper::SaveArrayToFile(Out, *Path);
}

FString FAsdaLoginData::LocalCheckPath()
{
	return FPaths::ProjectSavedDir() / TEXT("AsdaLogin") / TEXT("check.bin");
}

bool FAsdaLoginData::LoadActiveServer(FAsdaServerEntry& Out)
{
	// ★ Данные login-сервера — из АССЕТА DT_ServerList (импорт из OPset.bin офлайн, Tools/asda_build_server_list_dt.py).
	//   Рантайм каталог оригинальной игры не трогает вовсе.
	// ★ Аудит №4 (B2): свой ленивый кэш заменён общей точкой `AsdaDt::Cached`.
	const UDataTable* T = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/DT_ServerList.DT_ServerList"));
	if (!T) { return false; }
	// Первая валидная запись (эталон CServerList::Load берёт активный сервер из файла; в OPset.bin он один).
	for (const TPair<FName, uint8*>& It : T->GetRowMap())
	{
		const FAsdaServerListRow* R = reinterpret_cast<const FAsdaServerListRow*>(It.Value);
		if (!R || R->Port <= 0) { continue; }
		Out.Group = R->Group;
		Out.IP    = R->IP;
		Out.Port  = R->Port;
		Out.Name  = R->Name;
		return true;
	}
	return false;
}

bool FAsdaLoginData::LoadLoginServer(FAsdaServerEntry& Out)
{
	// Порт/группа/имя — из таблицы (эталонный OPset.bin); хост — домен сервера, независимо от IP в таблице.
	if (!LoadActiveServer(Out)) { Out.Port = 0; }
	Out.IP = AsdaServerAddr::LoginHost();
	return Out.IsValid();
}

bool FAsdaLoginData::PurgeLegacyServerIp()
{
	// Файл настроек UE: Saved/Config/.../GameUserSettings.ini, секция [AsdaLogin] ключ ServerIP (писала прежняя версия).
	if (!GConfig) { return false; }
	const bool bRemoved = GConfig->RemoveKey(LegacyServerIpSection, LegacyServerIpKey, GGameUserSettingsIni);
	if (bRemoved) { GConfig->Flush(false, GGameUserSettingsIni); }
	return bRemoved;
}

// ★ Срез inline-разметки. Делегирует в ЕДИНЫЙ парсер FAsdaMarkup::StripTags (полная грамматика
//   6 тегов: <FC=AARRGGBB>, <FS##>, <FB>, <FO>, <FD>, </F> + escape "\n"/"\t"/"\r").
//   Было: 3 ReplaceInline (<FB>/<FD>/</F>) — теги <FC=>/<FS##>/<FO> оставались ВИДНЫ игроку
//   (~30 колл-сайтов FAsdaText::Loc/Strip шли по этому наивному пути).
FString FAsdaLoginData::StripMarkup(const FString& In)
{
	return FAsdaMarkup::StripTags(In);
}
