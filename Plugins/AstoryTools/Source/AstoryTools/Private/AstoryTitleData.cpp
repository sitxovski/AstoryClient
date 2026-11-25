// Copyright (c) 2026 @sitxovski (Discord)
// Реализация модели данных редактора достижений — см. AstoryTitleData.h.
#include "AstoryTitleData.h"
#include "AstoryServerExport.h"
#include "AstoryDataTableIO.h"   // ★ волна 3 п.15: общий шаг «CSV → DataTable» и резервная копия файла

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/SecureHash.h"
#include "Engine/DataTable.h"
#include "UObject/Package.h"
#include "HAL/IConsoleManager.h"

#if WITH_EDITOR
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAstoryTitles, Log, All);

// ═════════════════════════════════════════════════════════════════════════════
// CP1251 ↔ Unicode
//   Строки внутри записей — однобайтовые CP1251 (как в оригинальном клиенте).
//   Таблица покрывает 0x80…0xFF; 0x00…0x7F совпадает с ASCII.
// ═════════════════════════════════════════════════════════════════════════════
static const uint16 GCp1251High[128] =
{
	0x0402,0x0403,0x201A,0x0453,0x201E,0x2026,0x2020,0x2021, // 80..87
	0x20AC,0x2030,0x0409,0x2039,0x040A,0x040C,0x040B,0x040F, // 88..8F
	0x0452,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014, // 90..97
	0xFFFD,0x2122,0x0459,0x203A,0x045A,0x045C,0x045B,0x045F, // 98..9F  (0x98 не определён)
	0x00A0,0x040E,0x045E,0x0408,0x00A4,0x0490,0x00A6,0x00A7, // A0..A7
	0x0401,0x00A9,0x0404,0x00AB,0x00AC,0x00AD,0x00AE,0x0407, // A8..AF
	0x00B0,0x00B1,0x0406,0x0456,0x0491,0x00B5,0x00B6,0x00B7, // B0..B7
	0x0451,0x2116,0x0454,0x00BB,0x0458,0x0405,0x0455,0x0457, // B8..BF
	0x0410,0x0411,0x0412,0x0413,0x0414,0x0415,0x0416,0x0417, // C0..C7
	0x0418,0x0419,0x041A,0x041B,0x041C,0x041D,0x041E,0x041F,
	0x0420,0x0421,0x0422,0x0423,0x0424,0x0425,0x0426,0x0427,
	0x0428,0x0429,0x042A,0x042B,0x042C,0x042D,0x042E,0x042F,
	0x0430,0x0431,0x0432,0x0433,0x0434,0x0435,0x0436,0x0437,
	0x0438,0x0439,0x043A,0x043B,0x043C,0x043D,0x043E,0x043F,
	0x0440,0x0441,0x0442,0x0443,0x0444,0x0445,0x0446,0x0447,
	0x0448,0x0449,0x044A,0x044B,0x044C,0x044D,0x044E,0x044F  // F8..FF
};

FString FAstoryTitleData::Cp1251ToString(const uint8* Bytes, int32 Len)
{
	FString Out;
	Out.Reserve(Len);
	for (int32 i = 0; i < Len; ++i)
	{
		const uint8 B = Bytes[i];
		if (B == 0)
		{
			break;                          // терминатор
		}
		Out.AppendChar(B < 0x80 ? (TCHAR)B : (TCHAR)GCp1251High[B - 0x80]);
	}
	return Out;
}

void FAstoryTitleData::StringToCp1251(const FString& S, TArray<uint8>& Out)
{
	Out.Reset();
	Out.Reserve(S.Len());
	for (int32 i = 0; i < S.Len(); ++i)
	{
		const TCHAR C = S[i];
		if ((uint32)C < 0x80)
		{
			Out.Add((uint8)C);
			continue;
		}
		// Обратный поиск по таблице. 128 элементов — линейного прохода достаточно.
		uint8 Found = 0;
		for (int32 k = 0; k < 128; ++k)
		{
			if (GCp1251High[k] == (uint16)C)
			{
				Found = (uint8)(0x80 + k);
				break;
			}
		}
		Out.Add(Found ? Found : (uint8)'?');   // непредставимый символ — '?', как делает сам клиент
	}
}

// ═════════════════════════════════════════════════════════════════════════════
// Раскладка записи title_list (240 б)
// ═════════════════════════════════════════════════════════════════════════════
const TArray<FAstoryTitleFieldDef>& AstoryTitleFields()
{
	static TArray<FAstoryTitleFieldDef> Fields = []()
	{
		TArray<FAstoryTitleFieldDef> F;
		auto AddI = [&F](int32 Off, const TCHAR* Csv, const TCHAR* Label, const TCHAR* Tip,
		                 EAstoryFieldGroup Grp, EAstoryFieldEnum En, bool bIdent, bool bList)
		{
			FAstoryTitleFieldDef D;
			D.Offset = Off; D.Type = EAstoryFieldType::Int32; D.ByteLen = 4;
			D.CsvName = Csv; D.Label = Label; D.Tip = Tip;
			D.Group = Grp; D.Enum = En; D.bIdentified = bIdent; D.bInList = bList;
			F.Add(D);
		};
		auto AddS = [&F](int32 Off, int32 Len, const TCHAR* Csv, const TCHAR* Label, const TCHAR* Tip,
		                 EAstoryFieldGroup Grp, bool bList)
		{
			FAstoryTitleFieldDef D;
			D.Offset = Off; D.Type = EAstoryFieldType::String; D.ByteLen = Len;
			D.CsvName = Csv; D.Label = Label; D.Tip = Tip;
			D.Group = Grp; D.bInList = bList;
			F.Add(D);
		};

		AddI(0,  TEXT("Id"), TEXT("Id"),
			TEXT("Ключ звания. ★ Он же — НОМЕР БИТА в маске «получено» (опкоды 6066/6086). "
			     "Маска расширена до 512 бит (патч P21), поэтому доступны id 0…511."),
			EAstoryFieldGroup::Identity, EAstoryFieldEnum::None, true, true);

		AddI(4,  TEXT("SortNum"), TEXT("№ в списке"),
			TEXT("Число в колонке «Нет.» окна званий. Эталон печатает его как \"%ld\"."),
			EAstoryFieldGroup::Display, EAstoryFieldEnum::None, true, true);

		AddI(8,  nullptr, TEXT("Unk8"),
			TEXT("НЕ ОПОЗНАНО. Загрузчик поле дешифрует (DWORD −40) ⇒ поле реальное, но читателей "
			     "нет ни в одной из 49 функций окна CUITitle (0xE962A0…0xE9CC80), ни во внешних "
			     "вызовах dataRecLookupById@0x9C48A0 — «ссылок в срезе нет».\n"
			     "По данным: второй почти-биективный порядковый ключ 0…362; у 183 из 280 записей "
			     "равен SortNum + 15. Правка уходит только в DataHex/.bin."),
			EAstoryFieldGroup::Unidentified, EAstoryFieldEnum::None, false, false);

		AddI(12, TEXT("ClassReq"), TEXT("Требование по классу"),
			TEXT("0 — любой класс · 1 — class ≤ 4 · 2 — class 12…15 · 3 — class 23…26."),
			EAstoryFieldGroup::Conditions, EAstoryFieldEnum::ClassReq, true, true);

		AddS(16, 64, TEXT("Name"), TEXT("Имя звания"),
			TEXT("Текст звания (64 байта, CP1251). Показывается в списке и над головой персонажа."),
			EAstoryFieldGroup::Identity, true);

		AddI(80, TEXT("ColorIndex"), TEXT("Грейд"),
			TEXT("ГРЕЙД звания: индекс цвета И строка локализации 1450+v (Обычный…Мифический). "
			     "★ Имя поля сохранено историческое — на него завязан UAsdaNameplateComponent::LookupTitle."),
			EAstoryFieldGroup::Display, EAstoryFieldEnum::Grade, true, true);

		AddI(84, TEXT("Group"), TEXT("Вкладка"),
			TEXT("Вкладка-фильтр окна 18700: 0 «Обычный» (loc 2405) · 1 «Соревнование» (2406) · "
			     "2 «Секретные» (2407). ★ Это ЕДИНСТВЕННОЕ, чем задаётся категория: эталон "
			     "QuestLog_PopulateCategoryList@0xE98C00 сравнивает *(запись+84) с индексом, "
			     "выбранным в выпадающем списке 18713. «Категория» (+92) — это ДРУГОЕ поле, "
			     "оно рисует колонку списка, на вкладку не влияет.\n"
			     "★ Побочные правила вкладки:\n"
			     "  · Enabled = 0 убирает звание из ЛЮБОЙ вкладки;\n"
			     "  · вкладка 2 «Секретные» показывает и НЕполученные звания, вкладки 0 и 1 — "
			     "только полученные либо найденные (жёстко по индексу 2 в коде, не по данным);\n"
			     "  · Group = 1 «Соревнование» ЗАПРЕЩАЕТ удаление звания игроком "
			     "(questItem_ValidateSelection@0xE99C30 выдаёт нотис 2428);\n"
			     "  · знаменатель «Собрано %» (нотис 2408) — все звания вкладки с Enabled ≠ 0, "
			     "прошедшие гейт класса, включая неполученные.\n"
			     "★ Выгружать в серверный FameInfo.txt НЕЧЕГО: колонка 2 там повторяет это "
			     "значение у 274 записей из 280 (расходится у id 90…95, где занята под другое), "
			     "но её не читает НИКТО — сервер разбирает из записи только смещения "
			     "+2, +4, +8, +10, +86, +88, +90, +92, а +3 пишется загрузчиком и больше нигде "
			     "не встречается."),
			EAstoryFieldGroup::Display, EAstoryFieldEnum::Group, true, true);

		AddI(88, TEXT("Position"), TEXT("Позиция"),
			TEXT("Где звание стоит относительно имени: 0 — приставка (loc 1466 «Преф.») · "
			     "1 — окончание (loc 1467 «Оконч.»)."),
			EAstoryFieldGroup::Display, EAstoryFieldEnum::Position, true, true);

		AddI(92, TEXT("CategoryLocIndex"), TEXT("Категория"),
			TEXT("Колонка «Категория»: строка локализации 1460+v "
			     "(Монстр/Уровень/Предмет/Элитный/Друг/Прочие). ★ Допустимо 0…5."),
			EAstoryFieldGroup::Display, EAstoryFieldEnum::Category, true, true);

		AddI(96, TEXT("Point"), TEXT("Очки славы"),
			TEXT("Очки славы за звание. ★ Есть серверный близнец — колонка 42 файла FameInfo.txt; "
			     "именно здесь расходятся клиент и сервер."),
			EAstoryFieldGroup::Conditions, EAstoryFieldEnum::None, true, true);

		AddS(100, 64, TEXT("Desc"), TEXT("Условие получения"),
			TEXT("Текст условия получения (контрол 18723 окна званий), 64 байта CP1251.\n"
			     "★ ДЕФЕКТ ОРИГИНАЛА D8: ItemTooltip_RefreshStats@0xE99360 копирует поле как "
			     "memcpy(..., rec+100, 0x3F) — 63 байта при поле 64, поэтому у записи, занявшей "
			     "поле целиком, теряется последний символ.\n"
			     "★ ДЕФЕКТ ДАННЫХ: в текущей сборке ~70 описаний обрезаны — см. метку «описание "
			     "обрезано» в списке."),
			EAstoryFieldGroup::Conditions, false);

		AddI(164, nullptr, TEXT("Unk164"),
			TEXT("НЕ ОПОЗНАНО (DWORD −40). Значения 0…3; у 254 из 280 записей совпадает с +168. "
			     "Читателей в коде нет."),
			EAstoryFieldGroup::Unidentified, EAstoryFieldEnum::None, false, false);
		AddI(168, nullptr, TEXT("Unk168"),
			TEXT("НЕ ОПОЗНАНО (DWORD −40). Значения 0…3. Читателей в коде нет."),
			EAstoryFieldGroup::Unidentified, EAstoryFieldEnum::None, false, false);
		AddI(172, nullptr, TEXT("Unk172"),
			TEXT("НЕ ОПОЗНАНО (DWORD −40). Значения 0/1; ноль только у id 18, 40 и 57. "
			     "Читателей в коде нет."),
			EAstoryFieldGroup::Unidentified, EAstoryFieldEnum::None, false, false);

		for (int32 k = 0; k < 6; ++k)
		{
			static const TCHAR* OptNames[6] = { TEXT("OptionTypeId0"), TEXT("OptionTypeId1"),
				TEXT("OptionTypeId2"), TEXT("OptionTypeId3"), TEXT("OptionTypeId4"), TEXT("OptionTypeId5") };
			static const TCHAR* OptLabels[6] = { TEXT("Бонус 1: тип"), TEXT("Бонус 2: тип"),
				TEXT("Бонус 3: тип"), TEXT("Бонус 4: тип"), TEXT("Бонус 5: тип"), TEXT("Бонус 6: тип") };
			AddI(176 + 4 * k, OptNames[k], OptLabels[k],
				TEXT("Тип бонуса → запись title_option. ★ ЗНАЧЕНИЯ бонуса в таблице НЕТ — его даёт "
				     "сервер (опкоды 2255/6065). В текущей сборке данных все типы нулевые, поэтому "
				     "панель бонусов пуста и в оригинале."),
				EAstoryFieldGroup::Bonuses, EAstoryFieldEnum::None, true, false);
		}

		AddI(200, TEXT("Enabled"), TEXT("Показывать в списке"),
			TEXT("Гейт списка окна: 0 ⇒ строка в список НЕ попадает. В данных встречаются 0, 1 и 2 — "
			     "код проверяет «не ноль»."),
			EAstoryFieldGroup::Display, EAstoryFieldEnum::Enabled, true, true);

		for (int32 k = 0; k < 6; ++k)
		{
			static const TCHAR* PctNames[6] = { TEXT("IsPercent0"), TEXT("IsPercent1"),
				TEXT("IsPercent2"), TEXT("IsPercent3"), TEXT("IsPercent4"), TEXT("IsPercent5") };
			static const TCHAR* PctLabels[6] = { TEXT("Бонус 1: в процентах"), TEXT("Бонус 2: в процентах"),
				TEXT("Бонус 3: в процентах"), TEXT("Бонус 4: в процентах"), TEXT("Бонус 5: в процентах"),
				TEXT("Бонус 6: в процентах") };
			AddI(204 + 4 * k, PctNames[k], PctLabels[k],
				TEXT("Признак «значение бонуса — проценты» для соответствующего слота."),
				EAstoryFieldGroup::Bonuses, EAstoryFieldEnum::None, true, false);
		}

		AddI(228, nullptr, TEXT("Unk228"),
			TEXT("НЕ ОПОЗНАНО (DWORD −40). Во всех 280 записях = 0, серверного близнеца нет — "
			     "по сути мусорное поле. Читателей в коде нет."),
			EAstoryFieldGroup::Unidentified, EAstoryFieldEnum::None, false, false);
		AddI(232, nullptr, TEXT("Unk232"),
			TEXT("НЕ ОПОЗНАНО (DWORD −40). Значения 0/1; единица у 28 записей (id 24…26, 35…37, "
			     "47…69). Читателей в коде нет."),
			EAstoryFieldGroup::Unidentified, EAstoryFieldEnum::None, false, false);
		AddI(236, nullptr, TEXT("Unk236"),
			TEXT("НЕ ОПОЗНАНО (DWORD −40). Во всех 280 записях = 1. Читателей в коде нет."),
			EAstoryFieldGroup::Unidentified, EAstoryFieldEnum::None, false, false);

		return F;
	}();
	return Fields;
}

const TArray<FAstoryServerColMap>& AstoryServerColMap()
{
	// ★ ДОКАЗАННЫЕ соответствия — по загрузчику `LoadFameInfo@0x49F410` (запись 96 б):
	//     кол. 0        → +0  Id                       ↔ клиент +0   Id
	//     кол. 1        → +2  гейт «включено»          ↔ клиент +200 Enabled
	//     кол. 12 + 5m  → +26 + 10m тип слот-баффа     ↔ клиент +176 + 4m OptionTypeId(m)
	//     кол. 42       → +86 очки славы               ↔ клиент +96  Point
	//   Остальные колонки сервера описывают УСЛОВИЯ ВЫДАЧИ (порог прогресса, режим сравнения,
	//   разброс значений баффов) — клиентского близнеца у них нет, сверять не с чем.
	//   ⚠ Расширять таблицу только по коду загрузчика, а не «по похожести чисел»: на догадках
	//   мы уже получали миснеймы (EquipFlag / TypeLocIndex / Param96).
	static TArray<FAstoryServerColMap> Map = []()
	{
		TArray<FAstoryServerColMap> M;
		M.Add({ AstoryTitleOffsets::Id,      0,  TEXT("Id") });
		M.Add({ AstoryTitleOffsets::Enabled, 1,  TEXT("Показывать в списке") });
		for (int32 m = 0; m < 6; ++m)
		{
			static const TCHAR* OptLabels[6] = { TEXT("Бонус 1: тип"), TEXT("Бонус 2: тип"),
				TEXT("Бонус 3: тип"), TEXT("Бонус 4: тип"), TEXT("Бонус 5: тип"), TEXT("Бонус 6: тип") };
			M.Add({ AstoryTitleOffsets::OptionTypeId0 + 4 * m, 12 + 5 * m, OptLabels[m] });
		}
		M.Add({ AstoryTitleOffsets::Point,   42, TEXT("Очки славы") });
		return M;
	}();
	return Map;
}

// ═════════════════════════════════════════════════════════════════════════════
// Запись: присвоение с синхронной правкой Payload
// ═════════════════════════════════════════════════════════════════════════════
bool FAstoryTitleRecord::IsDescLikelyTruncated() const
{
	const FString D = GetStr(AstoryTitleOffsets::Desc).TrimStartAndEnd();
	if (D.IsEmpty())
	{
		return false;
	}

	// Признак 1 — текст занял ВСЁ поле: конец фразы физически не поместился.
	TArray<uint8> Bytes;
	FAstoryTitleData::StringToCp1251(D, Bytes);
	if (Bytes.Num() >= 64)
	{
		return true;
	}

	// Признак 2 — след конвертера, резавшего строку на первой букве «я».
	//   В ШИФРОВАННОМ виде «я» (CP1251 0xFF) — это байт 0x00, и наивный читатель принимает его
	//   за конец строки (закон `item-string-cipher-terminator`; настоящий терминатор — 0x01).
	//   Обрезанная так фраза не оканчивается знаком препинания И не содержит ни одной «я».
	const TCHAR Last = D[D.Len() - 1];
	const FString Terminators = TEXT(".!?)»\"'…:;");
	int32 Dummy = 0;
	if (Terminators.FindChar(Last, Dummy))
	{
		return false;
	}
	return !D.Contains(TEXT("я"));
}

void FAstoryTitleRecord::SetInt(int32 Offset, int32 Value)
{
	Ints.Add(Offset, Value);
	if (Payload.Num() >= Offset + 4)
	{
		FMemory::Memcpy(Payload.GetData() + Offset, &Value, 4);
	}
}

void FAstoryTitleRecord::SetStr(int32 Offset, int32 ByteLen, const FString& Value)
{
	Strs.Add(Offset, Value);
	if (Payload.Num() >= Offset + ByteLen)
	{
		TArray<uint8> Enc;
		FAstoryTitleData::StringToCp1251(Value, Enc);
		// Обрезаем по длине поля, оставляя место под терминатор, если строка короче.
		const int32 N = FMath::Min(Enc.Num(), ByteLen);
		FMemory::Memzero(Payload.GetData() + Offset, ByteLen);
		if (N > 0)
		{
			FMemory::Memcpy(Payload.GetData() + Offset, Enc.GetData(), N);
		}
	}
}

// ═════════════════════════════════════════════════════════════════════════════
// Шифр записи title_list — зеркало dec_title_list из Tools/decode_item_db.py
//   DWORD-поля:  на диске значение +40; строки: каждый байт +1, терминатор 0x01,
//   хвост поля добит 0x01 (проверено: во всех 280 записях хвост = 0x01).
// ═════════════════════════════════════════════════════════════════════════════
static const int32 GTitleDwordOffsets[] =
{
	0, 4, 8, 12,
	80, 84, 88, 92, 96,
	164, 168, 172,
	176, 180, 184, 188, 192, 196,
	200,
	204, 208, 212, 216, 220, 224,
	228, 232, 236
};
static const int32 GTitleDwordConst = 40;
static const int32 GTitleStrFields[][2] = { { 16, 64 }, { 100, 64 } };

void FAstoryTitleData::DecryptTitleRecord(TArray<uint8>& Rec)
{
	for (int32 Off : GTitleDwordOffsets)
	{
		uint32 V;
		FMemory::Memcpy(&V, Rec.GetData() + Off, 4);
		V -= GTitleDwordConst;
		FMemory::Memcpy(Rec.GetData() + Off, &V, 4);
	}
	for (const auto& SF : GTitleStrFields)
	{
		const int32 Off = SF[0], Len = SF[1];
		uint8* P = Rec.GetData() + Off;
		int32 End = Len;
		bool bPadded = false;
		for (int32 k = 0; k < Len; ++k)          // истинный терминатор в шифре — 0x01
		{
			if (P[k] == 1) { End = k; bPadded = true; break; }
		}
		if (!bPadded)
		{
			for (int32 k = 0; k < Len; ++k)       // старый формат — до первого 0x00
			{
				if (P[k] == 0) { End = k; break; }
			}
		}
		for (int32 k = 0; k < End; ++k)
		{
			P[k] = (uint8)(P[k] - 1);
		}
		if (bPadded)
		{
			FMemory::Memzero(P + End, Len - End);
		}
		else if (End < Len)
		{
			P[End] = 0;
		}
	}
}

void FAstoryTitleData::EncryptTitleRecord(TArray<uint8>& Rec)
{
	for (const auto& SF : GTitleStrFields)
	{
		const int32 Off = SF[0], Len = SF[1];
		uint8* P = Rec.GetData() + Off;
		int32 End = Len;                          // «строка заняла всё поле, терминатора нет»
		for (int32 k = 0; k < Len; ++k)
		{
			if (P[k] == 0) { End = k; break; }
		}
		for (int32 k = 0; k < End; ++k)
		{
			P[k] = (uint8)(P[k] + 1);
		}
		for (int32 k = End; k < Len; ++k)
		{
			P[k] = 1;                             // терминатор + заполнитель
		}
	}
	for (int32 Off : GTitleDwordOffsets)
	{
		uint32 V;
		FMemory::Memcpy(&V, Rec.GetData() + Off, 4);
		V += GTitleDwordConst;
		FMemory::Memcpy(Rec.GetData() + Off, &V, 4);
	}
}

// ═════════════════════════════════════════════════════════════════════════════
// Пути
// ═════════════════════════════════════════════════════════════════════════════
FString FAstoryTitleData::ProjectRawDir()  { return FPaths::ProjectDir() / TEXT("RawAssets"); }
FString FAstoryTitleData::CsvPath()        { return ProjectRawDir() / TEXT("Data/ItemDB/title_list.csv"); }
FString FAstoryTitleData::JsonPath()       { return ProjectRawDir() / TEXT("Data/ItemDB/title_list.json"); }
FString FAstoryTitleData::BinPath()        { return ProjectRawDir() / TEXT("Data/Character/title_list.bin"); }
FString FAstoryTitleData::GameTextPath()   { return ProjectRawDir() / TEXT("Text/GameText.csv"); }
FString FAstoryTitleData::BackupDir()      { return ProjectRawDir() / TEXT("Data/ItemDB/_backup"); }

FString FAstoryTitleData::ResolveServerFameInfo()
{
	// ★ ФАКТ, а не догадка: авторитетная таблица — `GameServer\ConfigSet\FameInfo.txt` (251 запись).
	//   Копия в `GameServer\ItemTable_Info\` — другое поколение (147 записей, 2009 г.).
	//   Первым пробуем живой сервер, затем локальные копии.
	static const TCHAR* Candidates[] =
	{
		TEXT("//@@ASTORY_SERVER_HOSTNAME@@/h(@@ASTORY_SERVER_HOSTNAME@@_ns)/ASDA/Working_ASDA/v2/GameServer/ConfigSet/FameInfo.txt"),
		TEXT("@@ASTORY_DEV_ROOT_FWD@@/Donors/GameServer_stock/ConfigSet/FameInfo.txt")
	};
	IFileManager& FM = IFileManager::Get();
	for (const TCHAR* C : Candidates)
	{
		if (FM.FileExists(C))
		{
			return FString(C);
		}
	}
	return FString();
}

// ═════════════════════════════════════════════════════════════════════════════
// CSV: разбор и запись (формат совместим с Tools/decode_item_db.py:
//   разделитель ',', кавычка '"', QUOTE_MINIMAL, перевод строки CRLF, UTF-8 без BOM)
// ═════════════════════════════════════════════════════════════════════════════
namespace AstoryCsv
{
	// Разбор всего текста в таблицу полей. Поддержаны кавычки и переводы строк внутри поля.
	static void Parse(const FString& Text, TArray<TArray<FString>>& OutRows)
	{
		OutRows.Reset();
		TArray<FString> Row;
		FString Field;
		bool bInQuotes = false;
		const int32 N = Text.Len();
		for (int32 i = 0; i < N; ++i)
		{
			const TCHAR C = Text[i];
			if (bInQuotes)
			{
				if (C == TEXT('"'))
				{
					if (i + 1 < N && Text[i + 1] == TEXT('"')) { Field.AppendChar(TEXT('"')); ++i; }
					else { bInQuotes = false; }
				}
				else
				{
					Field.AppendChar(C);
				}
				continue;
			}
			if (C == TEXT('"'))            { bInQuotes = true; }
			else if (C == TEXT(','))       { Row.Add(Field); Field.Reset(); }
			else if (C == TEXT('\r'))      { /* CR игнорируем: перевод даёт LF */ }
			else if (C == TEXT('\n'))      { Row.Add(Field); Field.Reset(); OutRows.Add(Row); Row.Reset(); }
			else                           { Field.AppendChar(C); }
		}
		if (Field.Len() > 0 || Row.Num() > 0)
		{
			Row.Add(Field);
			OutRows.Add(Row);
		}
	}

	// Экранирование одного поля по правилу QUOTE_MINIMAL модуля csv Python.
	static FString Quote(const FString& S)
	{
		const bool bNeed = S.Contains(TEXT(",")) || S.Contains(TEXT("\"")) ||
		                   S.Contains(TEXT("\n")) || S.Contains(TEXT("\r"));
		if (!bNeed)
		{
			return S;
		}
		return TEXT("\"") + S.Replace(TEXT("\""), TEXT("\"\"")) + TEXT("\"");
	}

	// Экранирование строки для JSON при ensure_ascii=False (не-ASCII остаются как есть).
	static FString JsonEscape(const FString& S)
	{
		FString Out;
		Out.Reserve(S.Len() + 8);
		for (int32 i = 0; i < S.Len(); ++i)
		{
			const TCHAR C = S[i];
			switch (C)
			{
			case TEXT('"'):  Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n");  break;
			case TEXT('\r'): Out += TEXT("\\r");  break;
			case TEXT('\t'): Out += TEXT("\\t");  break;
			case TEXT('\b'): Out += TEXT("\\b");  break;
			case TEXT('\f'): Out += TEXT("\\f");  break;
			default:
				if ((uint32)C < 0x20) { Out += FString::Printf(TEXT("\\u%04x"), (uint32)C); }
				else                  { Out.AppendChar(C); }
			}
		}
		return Out;
	}
}

// ═════════════════════════════════════════════════════════════════════════════
// Загрузка
// ═════════════════════════════════════════════════════════════════════════════
bool FAstoryTitleData::LoadCsv(FString& OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *CsvPath()))
	{
		OutError = FString::Printf(TEXT("не читается %s"), *CsvPath());
		return false;
	}

	TArray<TArray<FString>> Rows;
	AstoryCsv::Parse(Text, Rows);
	if (Rows.Num() < 2)
	{
		OutError = TEXT("title_list.csv пуст");
		return false;
	}

	// Индексы колонок по заголовку — порядок колонок в файле нам не диктует ничего.
	TMap<FString, int32> ColIdx;
	for (int32 i = 0; i < Rows[0].Num(); ++i)
	{
		ColIdx.Add(Rows[0][i], i);
	}
	const int32* HexCol = ColIdx.Find(TEXT("DataHex"));
	const int32* RowCol = ColIdx.Find(TEXT("_row"));

	const TArray<FAstoryTitleFieldDef>& Fields = AstoryTitleFields();

	Records.Reset();
	for (int32 r = 1; r < Rows.Num(); ++r)
	{
		const TArray<FString>& R = Rows[r];
		if (R.Num() < 2)
		{
			continue;                                    // хвостовая пустая строка
		}

		FAstoryTitleRecord Rec;
		Rec.RowIndex = (RowCol && R.IsValidIndex(*RowCol)) ? FCString::Atoi(*R[*RowCol]) : (r - 1);

		// Payload берём из DataHex — он полный и хранит в том числе неопознанные поля.
		Rec.Payload.SetNumZeroed(240);
		if (HexCol && R.IsValidIndex(*HexCol))
		{
			const FString& Hex = R[*HexCol];
			const TCHAR* H = *Hex;
			const int32 Bytes = FMath::Min(240, Hex.Len() / 2);
			auto Nib = [](TCHAR C) -> int32
			{
				if (C >= TEXT('0') && C <= TEXT('9')) return C - TEXT('0');
				if (C >= TEXT('a') && C <= TEXT('f')) return C - TEXT('a') + 10;
				if (C >= TEXT('A') && C <= TEXT('F')) return C - TEXT('A') + 10;
				return 0;
			};
			for (int32 b = 0; b < Bytes; ++b)
			{
				Rec.Payload[b] = (uint8)((Nib(H[b * 2]) << 4) | Nib(H[b * 2 + 1]));
			}
		}

		// Именованные поля читаем из колонок CSV (это то, что реально видит игра через DataTable),
		//   а неопознанные — из Payload.
		for (const FAstoryTitleFieldDef& F : Fields)
		{
			if (F.Type == EAstoryFieldType::Int32)
			{
				const int32* Ci = F.CsvName ? ColIdx.Find(F.CsvName) : nullptr;
				if (Ci && R.IsValidIndex(*Ci))
				{
					Rec.Ints.Add(F.Offset, FCString::Atoi(*R[*Ci]));
				}
				else if (Rec.Payload.Num() >= F.Offset + 4)
				{
					int32 V; FMemory::Memcpy(&V, Rec.Payload.GetData() + F.Offset, 4);
					Rec.Ints.Add(F.Offset, V);
				}
			}
			else
			{
				const int32* Ci = F.CsvName ? ColIdx.Find(F.CsvName) : nullptr;
				if (Ci && R.IsValidIndex(*Ci))
				{
					Rec.Strs.Add(F.Offset, R[*Ci]);
				}
				else if (Rec.Payload.Num() >= F.Offset + F.ByteLen)
				{
					Rec.Strs.Add(F.Offset, Cp1251ToString(Rec.Payload.GetData() + F.Offset, F.ByteLen));
				}
			}
		}
		Records.Add(MoveTemp(Rec));
	}

	// Заголовок .bin переносим в пересобранный файл 1:1.
	TArray<uint8> Bin;
	if (FFileHelper::LoadFileToArray(Bin, *BinPath()) && Bin.Num() >= 20)
	{
		FMemory::Memcpy(BinHeader, Bin.GetData(), 20);
	}
	return true;
}

bool FAstoryTitleData::LoadGameText(FString& OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *GameTextPath()))
	{
		OutError = FString::Printf(TEXT("не читается %s"), *GameTextPath());
		return false;
	}
	TArray<TArray<FString>> Rows;
	AstoryCsv::Parse(Text, Rows);
	LocStrings.Reset();
	for (int32 r = 1; r < Rows.Num(); ++r)
	{
		if (Rows[r].Num() < 2)
		{
			continue;
		}
		const int32 Key = FCString::Atoi(*Rows[r][0]);
		LocStrings.Add(Key, Rows[r][1]);
	}
	return true;
}

bool FAstoryTitleData::LoadServerFameInfo()
{
	ServerRows.Reset();
	ServerRawLines.Reset();
	ServerFilePath = ResolveServerFameInfo();
	ServerFileHash.Empty();
	if (ServerFilePath.IsEmpty())
	{
		return false;                                    // сервер недоступен — не ошибка
	}

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *ServerFilePath))
	{
		ServerFilePath.Empty();
		return false;
	}
	ServerFileHash = FAstoryServerExport::HashFile(ServerFilePath);

	Text.ParseIntoArray(ServerRawLines, TEXT("\n"), /*CullEmpty=*/false);
	for (FString& L : ServerRawLines)
	{
		L.RemoveFromEnd(TEXT("\r"));
	}

	// Первая строка — количество записей, дальше строки, разделённые табуляцией.
	for (int32 i = 1; i < ServerRawLines.Num(); ++i)
	{
		const FString& L = ServerRawLines[i];
		if (L.TrimStartAndEnd().IsEmpty())
		{
			continue;
		}
		TArray<FString> Parts;
		L.ParseIntoArray(Parts, TEXT("\t"), /*CullEmpty=*/false);
		if (Parts.Num() < 2)
		{
			continue;
		}
		FAstoryFameServerRow Row;
		Row.Cols.Reserve(Parts.Num());
		for (const FString& P : Parts)
		{
			Row.Cols.Add(FCString::Atoi(*P));
		}
		Row.Id = Row.Cols[0];
		ServerRows.Add(Row.Id, Row);
	}
	return true;
}

bool FAstoryTitleData::LoadAll(FString& OutError)
{
	bLoaded = false;
	if (!LoadCsv(OutError))      { return false; }
	if (!LoadGameText(OutError)) { return false; }
	LoadServerFameInfo();                                // не обязателен

	// ★ Снимок «как было при загрузке»: по нему отличаются ПРАВКИ ЭТОЙ СЕССИИ от расхождений,
	//   которые в данных были и до нас.
	Baseline.Reset();
	for (const FAstoryTitleRecord& R : Records)
	{
		Baseline.Add(R.GetId(), R);
	}

	bLoaded = true;
	UE_LOG(LogAstoryTitles, Log,
		TEXT("загружено: званий %d, строк локализации %d, серверных строк %d (%s)"),
		Records.Num(), LocStrings.Num(), ServerRows.Num(),
		ServerFilePath.IsEmpty() ? TEXT("сервер недоступен") : *ServerFilePath);
	return true;
}

FString FAstoryTitleData::Loc(int32 Id, bool* bExists) const
{
	const FString* P = LocStrings.Find(Id);
	if (bExists)
	{
		*bExists = (P != nullptr);
	}
	return P ? *P : FString();
}

// ═════════════════════════════════════════════════════════════════════════════
// Сверка с сервером
// ═════════════════════════════════════════════════════════════════════════════
TArray<FString> FAstoryTitleData::DiffWithServer(const FAstoryTitleRecord& Rec) const
{
	TArray<FString> Out;
	const FAstoryFameServerRow* Srv = ServerRows.Find(Rec.GetId());
	if (!Srv)
	{
		return Out;                                       // «строки нет» — отдельное состояние
	}
	for (const FAstoryServerColMap& M : AstoryServerColMap())
	{
		if (M.ServerCol == 0)
		{
			continue;                                     // id сверять незачем — по нему и искали
		}
		if (!Srv->Cols.IsValidIndex(M.ServerCol))
		{
			continue;
		}
		const int32 ClientVal = Rec.GetInt(M.ClientOffset);
		const int32 ServerVal = Srv->Cols[M.ServerCol];
		if (ClientVal != ServerVal)
		{
			Out.Add(FString::Printf(TEXT("%s: клиент %d ≠ сервер %d"), M.Label, ClientVal, ServerVal));
		}
	}
	return Out;
}

// ═════════════════════════════════════════════════════════════════════════════
// Валидация
// ═════════════════════════════════════════════════════════════════════════════
// Ключ проблемы — по нему сравниваем «было/стало».
static FString AstoryIssueKey(const FAstoryIssue& I)
{
	return FString::Printf(TEXT("%d|%d|%s"), I.Id, (int32)I.Kind, *I.Text);
}

TArray<FAstoryIssue> FAstoryTitleData::Validate() const
{
	// Сначала — каким набор проблем был при загрузке. Всё, что там уже было, помечаем
	//   унаследованным и сохранять не мешаем.
	TArray<FAstoryTitleRecord> Base;
	Base.Reserve(Baseline.Num());
	for (const TPair<int32, FAstoryTitleRecord>& P : Baseline)
	{
		Base.Add(P.Value);
	}
	TSet<FString> Was;
	for (const FAstoryIssue& I : ValidateRecords(Base))
	{
		Was.Add(AstoryIssueKey(I));
	}

	TArray<FAstoryIssue> Out = ValidateRecords(Records);
	for (FAstoryIssue& I : Out)
	{
		I.bPreExisting = Was.Contains(AstoryIssueKey(I));
	}
	return Out;
}

TArray<FAstoryIssue> FAstoryTitleData::ValidateRecords(
	const TArray<FAstoryTitleRecord>& InRecords) const
{
	TArray<FAstoryIssue> Issues;
	TMap<int32, int32> SeenId;                            // id → RowIndex первой встречи

	for (const FAstoryTitleRecord& Rec : InRecords)
	{
		const int32 Id = Rec.GetId();

		if (Id < 0)
		{
			Issues.Add({ Rec.RowIndex, Id, EAstoryIssueKind::IdOutOfRange,
				FString::Printf(TEXT("отрицательный id %d"), Id) });
		}
		if (const int32* First = SeenId.Find(Id))
		{
			Issues.Add({ Rec.RowIndex, Id, EAstoryIssueKind::DuplicateId,
				FString::Printf(TEXT("id %d уже занят строкой %d"), Id, *First) });
		}
		else
		{
			SeenId.Add(Id, Rec.RowIndex);
		}

		if (Rec.GetName().TrimStartAndEnd().IsEmpty())
		{
			Issues.Add({ Rec.RowIndex, Id, EAstoryIssueKind::EmptyName, TEXT("пустое имя звания") });
		}

		// ★ Известный дефект данных: колонка «Категория» ссылается вне 1460…1465.
		const int32 Cat = Rec.GetInt(AstoryTitleOffsets::CategoryLocIndex);
		if (Cat < 0 || Cat > 5)
		{
			Issues.Add({ Rec.RowIndex, Id, EAstoryIssueKind::BadCategory,
				FString::Printf(TEXT("категория %d ⇒ строка %d вне диапазона 1460…1465"), Cat, 1460 + Cat) });
		}
		else
		{
			bool bHas = false;
			Loc(1460 + Cat, &bHas);
			if (!bHas)
			{
				Issues.Add({ Rec.RowIndex, Id, EAstoryIssueKind::MissingLoc,
					FString::Printf(TEXT("нет строки локализации %d (категория)"), 1460 + Cat) });
			}
		}

		// Грейд: строка 1450+v.
		const int32 Grade = Rec.GetInt(AstoryTitleOffsets::ColorIndex);
		bool bGradeHas = false;
		Loc(1450 + Grade, &bGradeHas);
		if (!bGradeHas)
		{
			Issues.Add({ Rec.RowIndex, Id, EAstoryIssueKind::MissingLoc,
				FString::Printf(TEXT("нет строки локализации %d (грейд %d)"), 1450 + Grade, Grade) });
		}

		if (Rec.IsDescLikelyTruncated())
		{
			Issues.Add({ Rec.RowIndex, Id, EAstoryIssueKind::TruncatedDesc,
				FString::Printf(TEXT("описание обрезано в данных: «%s»"),
					*Rec.GetStr(AstoryTitleOffsets::Desc)) });
		}

		if (Rec.IsUnreachable())
		{
			Issues.Add({ Rec.RowIndex, Id, EAstoryIssueKind::UnreachableId,
				FString::Printf(TEXT("id %d ≥ %d — звание НЕДОСТИЖИМО: маска «получено» в протоколе "
				                     "ровно %d бит"), Id, AstoryFameMaskBits, AstoryFameMaskBits) });
		}

		if (ServerRows.Num() > 0)
		{
			if (!ServerRows.Contains(Id))
			{
				Issues.Add({ Rec.RowIndex, Id, EAstoryIssueKind::ServerMissing,
					FString::Printf(TEXT("у сервера нет строки с id %d"), Id) });
			}
			else
			{
				for (const FString& D : DiffWithServer(Rec))
				{
					Issues.Add({ Rec.RowIndex, Id, EAstoryIssueKind::ServerMismatch, D });
				}
			}
		}
	}
	return Issues;
}

// ═════════════════════════════════════════════════════════════════════════════
// Обратная сборка .bin
// ═════════════════════════════════════════════════════════════════════════════
static void AppendMd5Prefix(TArray<uint8>& Out, const uint8* Payload, int32 Len)
{
	FMD5 Md5;
	Md5.Update(Payload, Len);
	uint8 Digest[16];
	Md5.Final(Digest);
	Out.Append(Digest, 16);
}

bool FAstoryTitleData::BuildBin(TArray<uint8>& OutBytes, FString& OutError) const
{
	OutBytes.Reset();
	OutBytes.Append(BinHeader, 20);
	// В заголовке count — количество записей; при добавлении/удалении его надо обновить.
	const int32 Count = Records.Num();
	FMemory::Memcpy(OutBytes.GetData() + 4, &Count, 4);

	for (const FAstoryTitleRecord& Rec : Records)
	{
		if (Rec.Payload.Num() != 240)
		{
			OutError = FString::Printf(TEXT("запись %d: длина payload %d вместо 240"),
				Rec.RowIndex, Rec.Payload.Num());
			return false;
		}
		TArray<uint8> Enc = Rec.Payload;
		EncryptTitleRecord(Enc);
		AppendMd5Prefix(OutBytes, Enc.GetData(), 240);
		OutBytes.Append(Enc);
	}
	return true;
}

bool FAstoryTitleData::SelfTestBinRoundTrip(const FString& InBinPath, FString& OutReport)
{
	TArray<uint8> Orig;
	if (!FFileHelper::LoadFileToArray(Orig, *InBinPath))
	{
		OutReport = FString::Printf(TEXT("не читается %s"), *InBinPath);
		return false;
	}
	if (Orig.Num() < 20)
	{
		OutReport = TEXT("файл короче заголовка");
		return false;
	}
	int32 Count = 0;
	FMemory::Memcpy(&Count, Orig.GetData() + 4, 4);
	const int32 Stride = 256;
	const int32 Expected = 20 + Count * Stride;
	if (Orig.Num() != Expected)
	{
		OutReport = FString::Printf(TEXT("размер %d ≠ 20 + %d × %d = %d"),
			Orig.Num(), Count, Stride, Expected);
		return false;
	}

	TArray<uint8> Rebuilt;
	Rebuilt.Append(Orig.GetData(), 20);

	int32 PayloadMismatch = 0, Md5Mismatch = 0, FirstBadRec = INDEX_NONE, FirstBadOff = INDEX_NONE;
	for (int32 i = 0; i < Count; ++i)
	{
		const uint8* Rec = Orig.GetData() + 20 + i * Stride;
		TArray<uint8> Payload;
		Payload.Append(Rec + 16, 240);

		TArray<uint8> Work = Payload;
		FAstoryTitleData::DecryptTitleRecord(Work);
		FAstoryTitleData::EncryptTitleRecord(Work);

		if (FMemory::Memcmp(Work.GetData(), Payload.GetData(), 240) != 0)
		{
			++PayloadMismatch;
			if (FirstBadRec == INDEX_NONE)
			{
				FirstBadRec = i;
				for (int32 b = 0; b < 240; ++b)
				{
					if (Work[b] != Payload[b]) { FirstBadOff = b; break; }
				}
			}
		}

		// MD5-префикс воспроизводим сами: prefix == md5(payload в ШИФРОВАННОМ виде).
		TArray<uint8> WithMd5;
		AppendMd5Prefix(WithMd5, Work.GetData(), 240);
		if (FMemory::Memcmp(WithMd5.GetData(), Rec, 16) != 0)
		{
			++Md5Mismatch;
		}

		Rebuilt.Append(WithMd5);
		Rebuilt.Append(Work);
	}

	const bool bIdentical = (Rebuilt.Num() == Orig.Num()) &&
		FMemory::Memcmp(Rebuilt.GetData(), Orig.GetData(), Orig.Num()) == 0;

	OutReport = FString::Printf(
		TEXT("%s\n  размер %d б, записей %d (шаг %d = MD5 16 + запись 240)\n")
		TEXT("  тело записей: расхождений %d%s\n")
		TEXT("  MD5-префиксы: расхождений %d (алгоритм: md5 шифрованной записи, воспроизведён)\n")
		TEXT("  ИТОГ: %s"),
		*FPaths::GetCleanFilename(InBinPath), Orig.Num(), Count, Stride,
		PayloadMismatch,
		FirstBadRec != INDEX_NONE
			? *FString::Printf(TEXT(" (первое — запись %d, смещение %d)"), FirstBadRec, FirstBadOff)
			: TEXT(""),
		Md5Mismatch,
		bIdentical ? TEXT("ПОБАЙТОВО СОВПАДАЕТ С ОРИГИНАЛОМ") : TEXT("★ РАСХОЖДЕНИЕ — писать .bin НЕЛЬЗЯ"));

	return bIdentical;
}

// ═════════════════════════════════════════════════════════════════════════════
// Сохранение
// ═════════════════════════════════════════════════════════════════════════════
namespace
{
	// Резервная копия файла в _backup с меткой времени. Возвращает путь копии.
	// ★ Волна 3 п.15: реализация — общий FAstoryDataTableIO::BackupFile (тело перенесено туда без изменений).
	FString BackupFile(const FString& Src, const FString& Dir, const FString& Stamp)
	{
		return FAstoryDataTableIO::BackupFile(Src, Dir, Stamp);
	}
}

// CSV ровно в том виде, в каком его пишет Tools/decode_item_db.py: тот же порядок колонок,
//   то же экранирование (QUOTE_MINIMAL), тот же CRLF. Иначе первое же сохранение «вхолостую»
//   даст гигантский дифф файла и спрячет настоящие правки.
FString FAstoryTitleData::SerializeCsv() const
{
	const TArray<FAstoryTitleFieldDef>& Fields = AstoryTitleFields();

	TArray<FString> Header;
	Header.Add(TEXT("_row"));
	for (const FAstoryTitleFieldDef& F : Fields)
	{
		if (F.CsvName) { Header.Add(F.CsvName); }
	}
	Header.Add(TEXT("DataHex"));

	FString Csv;
	Csv.Reserve(Records.Num() * 700);
	Csv += FString::Join(Header, TEXT(","));
	Csv += TEXT("\r\n");

	for (const FAstoryTitleRecord& Rec : Records)
	{
		TArray<FString> Cells;
		Cells.Add(FString::FromInt(Rec.RowIndex));
		for (const FAstoryTitleFieldDef& F : Fields)
		{
			if (!F.CsvName)
			{
				continue;
			}
			Cells.Add(F.Type == EAstoryFieldType::Int32
				? FString::FromInt(Rec.GetInt(F.Offset))
				: AstoryCsv::Quote(Rec.GetStr(F.Offset)));
		}
		Cells.Add(BytesToHex(Rec.Payload.GetData(), Rec.Payload.Num()).ToLower());
		Csv += FString::Join(Cells, TEXT(","));
		Csv += TEXT("\r\n");
	}
	return Csv;
}

// JSON в формате json.dump(ensure_ascii=False, indent=0) — как у декодера.
FString FAstoryTitleData::SerializeJson() const
{
	const TArray<FAstoryTitleFieldDef>& Fields = AstoryTitleFields();

	FString Json;
	Json.Reserve(Records.Num() * 800);
	Json += TEXT("[\r\n");
	for (int32 r = 0; r < Records.Num(); ++r)
	{
		const FAstoryTitleRecord& Rec = Records[r];
		TArray<FString> Pairs;
		// ★ Первый ключ — "Name": декодер кладёт его первым (как имя строки), а потом
		//   ЗАТИРАЕТ значением одноимённой колонки. Позиция ключа при этом остаётся первой.
		Pairs.Add(FString::Printf(TEXT("\"Name\": \"%s\""),
			*AstoryCsv::JsonEscape(Rec.GetStr(AstoryTitleOffsets::Name))));
		for (const FAstoryTitleFieldDef& F : Fields)
		{
			if (!F.CsvName || FCString::Strcmp(F.CsvName, TEXT("Name")) == 0)
			{
				continue;
			}
			Pairs.Add(F.Type == EAstoryFieldType::Int32
				? FString::Printf(TEXT("\"%s\": %d"), F.CsvName, Rec.GetInt(F.Offset))
				: FString::Printf(TEXT("\"%s\": \"%s\""), F.CsvName,
					*AstoryCsv::JsonEscape(Rec.GetStr(F.Offset))));
		}
		Pairs.Add(FString::Printf(TEXT("\"DataHex\": \"%s\""),
			*BytesToHex(Rec.Payload.GetData(), Rec.Payload.Num()).ToLower()));

		Json += TEXT("{\r\n");
		Json += FString::Join(Pairs, TEXT(",\r\n"));
		Json += TEXT("\r\n}");
		Json += (r + 1 < Records.Num()) ? TEXT(",\r\n") : TEXT("\r\n");
	}
	Json += TEXT("]");
	return Json;
}

bool FAstoryTitleData::Save(bool bAlsoWriteBin, FString& OutLog, FString& OutError,
                            bool bSavePackage)
{
	OutLog.Empty();
	OutError.Empty();

	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));

	// ── 1. Резервные копии ──
	const FString BakCsv  = BackupFile(CsvPath(),  BackupDir(), Stamp);
	BackupFile(JsonPath(), BackupDir(), Stamp);
	OutLog += FString::Printf(TEXT("резервные копии: %s\n"),
		BakCsv.IsEmpty() ? TEXT("(нечего копировать)") : *FPaths::GetCleanFilename(BakCsv));

	// ── 2. CSV ──
	const FString Csv = SerializeCsv();
	if (!FFileHelper::SaveStringToFile(Csv, *CsvPath(),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("не удалось записать %s"), *CsvPath());
		return false;
	}
	OutLog += FString::Printf(TEXT("записан %s (%d записей)\n"),
		*FPaths::GetCleanFilename(CsvPath()), Records.Num());

	// ── 3. JSON ──
	FFileHelper::SaveStringToFile(SerializeJson(), *JsonPath(),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	OutLog += FString::Printf(TEXT("записан %s\n"), *FPaths::GetCleanFilename(JsonPath()));

	// ── 4. Переимпорт DT_title_list из только что записанного CSV ──
	// ★ Волна 3 п.15: общий шаг «CSV → DataTable» каркаса (FAstoryDataTableIO), тело и текст отчёта — прежние
	//   (прямое сохранение пакета вместо PromptForCheckoutAndSave, замечания импортёра — в отчёт).
#if WITH_EDITOR
	const TCHAR* DtPath = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_title_list.DT_title_list");
	if (!FAstoryDataTableIO::ReimportFromCsv(DtPath, TEXT("DT_title_list"), Csv, bSavePackage, OutLog))
	{
		OutLog += TEXT("★ DT_title_list не найден — CSV записан, ассет НЕ обновлён\n");
	}
#endif

	// ── 5. Обратная сборка .bin (по требованию) ──
	if (bAlsoWriteBin)
	{
		FString Report;
		const bool bRoundTripOk = SelfTestBinRoundTrip(BinPath(), Report);
		OutLog += FString::Printf(TEXT("самопроверка обратной сборки:\n%s\n"), *Report);
		if (!bRoundTripOk)
		{
			OutError = TEXT("обратная сборка .bin не совпала с оригиналом — файл НЕ записан");
			return false;
		}
		TArray<uint8> Bin;
		if (!BuildBin(Bin, OutError))
		{
			return false;
		}
		BackupFile(BinPath(), BackupDir(), Stamp);
		if (!FFileHelper::SaveArrayToFile(Bin, *BinPath()))
		{
			OutError = FString::Printf(TEXT("не удалось записать %s"), *BinPath());
			return false;
		}
		OutLog += FString::Printf(TEXT("записан %s (%d б)\n"),
			*FPaths::GetCleanFilename(BinPath()), Bin.Num());
	}

	// Сохранённое становится новой базовой линией: следующая выгрузка на сервер повезёт
	//   только СЛЕДУЮЩИЕ правки, а не эти же ещё раз.
	Baseline.Reset();
	for (const FAstoryTitleRecord& R : Records)
	{
		Baseline.Add(R.GetId(), R);
	}

	return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Достройка серверной таблицы недостающими записями + доказательство корректности
//   Доказываем ТРИ вещи:
//     1. пересборка БЕЗ достройки побайтово равна живому файлу;
//     2. достроенный файл отличается от живого ТОЛЬКО первой строкой (счётчик) и
//        дописанным хвостом — ни одной правки в существующих строках;
//     3. число записей в первой строке совпадает с фактическим числом строк.
// ═════════════════════════════════════════════════════════════════════════════
void FAstoryTitleData::BuildAndVerifyServerAddMissing(bool bDoExport, FString& OutReport,
                                                      bool bIgnoreLoaderLimit)
{
	OutReport.Empty();

	FAstoryTitleData D;
	FString Error;
	if (!D.LoadAll(Error) || D.GetServerRows().Num() == 0)
	{
		OutReport = TEXT("★ серверная таблица недоступна — достраивать нечего");
		UE_LOG(LogAstoryTitles, Warning, TEXT("%s"), *OutReport);
		return;
	}

	TArray<uint8> Orig;
	FFileHelper::LoadFileToArray(Orig, *D.GetServerFilePath());
	OutReport += FString::Printf(TEXT("источник: %s\n  %d б, записей %d\n\n"),
		*D.GetServerFilePath(), Orig.Num(), D.GetServerRows().Num());

	// ── 1. без достройки: обязано совпасть побайтово ──
	{
		TArray<uint8> Same;
		int32 Ch = 0;
		D.BuildServerFameInfo(Same, Ch, Error, false, /*bAddMissingRows=*/false);
		const bool bOk = (Same.Num() == Orig.Num()) &&
			FMemory::Memcmp(Same.GetData(), Orig.GetData(), Orig.Num()) == 0;
		OutReport += FString::Printf(
			TEXT("[1] Пересборка БЕЗ достройки: %d б против %d б — %s\n"),
			Same.Num(), Orig.Num(), bOk ? TEXT("ПОБАЙТОВО СОВПАДАЕТ") : TEXT("★ РАСХОЖДЕНИЕ"));
		if (!bOk)
		{
			UE_LOG(LogAstoryTitles, Error, TEXT("%s"), *OutReport);
			return;
		}
	}

	// ── 2. с достройкой ──
	TArray<uint8> Built;
	int32 Changed = 0, Added = 0;
	TArray<int32> AddedIds, SkippedIds;
	if (!D.BuildServerFameInfo(Built, Changed, Error, false, /*bAddMissingRows=*/true,
			&Added, &AddedIds, bIgnoreLoaderLimit, &SkippedIds))
	{
		OutReport += FString::Printf(TEXT("★ достройка не удалась: %s\n"), *Error);
		UE_LOG(LogAstoryTitles, Error, TEXT("%s"), *OutReport);
		return;
	}

	FString IdList, UnrList;
	int32 UnrCount = 0;
	for (int32 k = 0; k < AddedIds.Num(); ++k)
	{
		IdList += (k ? TEXT(", ") : TEXT("")) + FString::FromInt(AddedIds[k]);
		if (AddedIds[k] >= AstoryFameMaskBits)
		{
			UnrList += (UnrCount ? TEXT(", ") : TEXT("")) + FString::FromInt(AddedIds[k]);
			++UnrCount;
		}
	}
	OutReport += FString::Printf(
		TEXT("\n[2] Достройка: добавлено строк %d · существующих строк изменено %d\n")
		TEXT("    записей стало %d (было %d)\n    id: %s\n"),
		Added, Changed, D.GetServerRows().Num() + Added, D.GetServerRows().Num(), *IdList);

	// ── 3. чем именно отличается от оригинала ──
	{
		// Первое отличие и общий префикс: всё старое содержимое, кроме первой строки,
		//   обязано совпасть байт в байт.
		int32 FirstDiff = 0;
		const int32 N = FMath::Min(Orig.Num(), Built.Num());
		while (FirstDiff < N && Orig[FirstDiff] == Built[FirstDiff]) { ++FirstDiff; }

		// Где кончается первая строка исходника.
		int32 FirstLineEnd = 0;
		while (FirstLineEnd < Orig.Num() && Orig[FirstLineEnd] != '\n') { ++FirstLineEnd; }

		// Сравниваем всё, что после первой строки, на общей длине оригинала.
		int32 TailDiffs = 0;
		for (int32 i = FirstLineEnd + 1; i < Orig.Num() && i < Built.Num(); ++i)
		{
			if (Orig[i] != Built[i]) { ++TailDiffs; }
		}
		OutReport += FString::Printf(
			TEXT("\n[3] Сверка со старым содержимым\n")
			TEXT("    первое отличие на смещении %d (первая строка — счётчик, длина %d)\n")
			TEXT("    отличий в старых строках после счётчика: %d — %s\n")
			TEXT("    файл вырос: %d б -> %d б (+%d)\n"),
			FirstDiff, FirstLineEnd, TailDiffs,
			TailDiffs == 0 ? TEXT("НИ ОДНОЙ ПРАВКИ В СУЩЕСТВУЮЩИХ СТРОКАХ")
			               : TEXT("★ СТАРЫЕ СТРОКИ ИЗМЕНЕНЫ"),
			Orig.Num(), Built.Num(), Built.Num() - Orig.Num());
	}

	// ── 4. счётчик против фактического числа строк ──
	{
		FString AsText;
		FFileHelper::BufferToString(AsText, Built.GetData(), Built.Num());
		TArray<FString> Lines;
		AsText.ParseIntoArray(Lines, TEXT("\n"), false);
		int32 Real = 0;
		for (int32 i = 1; i < Lines.Num(); ++i)
		{
			if (!Lines[i].TrimStartAndEnd().IsEmpty()) { ++Real; }
		}
		const int32 Declared = Lines.Num() > 0 ? FCString::Atoi(*Lines[0]) : -1;
		OutReport += FString::Printf(
			TEXT("\n[4] Счётчик в первой строке %d · фактических строк %d — %s\n"),
			Declared, Real, Declared == Real ? TEXT("сходится") : TEXT("★ НЕ СХОДИТСЯ"));
	}

	// ── 5. предупреждение о недостижимых ──
	if (UnrCount > 0)
	{
		OutReport += FString::Printf(
			TEXT("\n[5] ★★★ Из добавленных %d записей имеют id >= %d и СЕЙЧАС НЕ РАБОТАЮТ:\n")
			TEXT("    маска «получено» в протоколе ровно %d бит — сервер не может отметить такой бит.\n")
			TEXT("    Строки в таблице лежат и ждут расширения маски (её расширяют до 512).\n")
			TEXT("    id: %s\n"),
			UnrCount, AstoryFameMaskBits, AstoryFameMaskBits, *UnrList);
	}

	// ── 6. выгрузка в обе серверные ветки ──
	if (SkippedIds.Num() > 0)
	{
		FString SkipList;
		for (int32 k = 0; k < SkippedIds.Num(); ++k)
		{
			SkipList += (k ? TEXT(", ") : TEXT("")) + FString::FromInt(SkippedIds[k]);
		}
		OutReport += FString::Printf(
			TEXT("\n[5a] ★★★ НЕ ДОБАВЛЕНО %d записей — предел загрузчика %d\n")
			TEXT("     sub_4A18F0@0x4A18F0: if (count > 0 && count <= 256) иначе «FameInfo count over»\n")
			TEXT("     и таблица не грузится ЦЕЛИКОМ. Предел уже поднят патчем P21 до 512 — значит записей больше 512.\n")
			TEXT("     id: %s\n"),
			SkippedIds.Num(), AstoryServerFameMaxRows, *SkipList);
	}

	if (bDoExport)
	{
		FAstoryServerExportRequest Req;
		int32 Ch2 = 0;
		if (D.BuildServerExportRequest(Req, Ch2, Error, /*bAllMappedColumns=*/false,
				/*bAddMissingRows=*/true, bIgnoreLoaderLimit))
		{
			FString ExportError;
			const FString Folder = FAstoryServerExport::Build(Req, ExportError);
			OutReport += FString::Printf(TEXT("\n[6] Выгрузка: %s %s\n"),
				*Folder, *ExportError);
			for (const FAstoryServerExportFile& F : Req.Files)
			{
				for (const FAstoryServerPlacement& P : F.Placements)
				{
					OutReport += FString::Printf(TEXT("    -> %s\\%s\\%s\n"),
						*P.ServerKind, *P.RelativeDir, *F.FileName);
				}
			}
		}
		else
		{
			OutReport += FString::Printf(TEXT("\n[6] ★ выгрузка не удалась: %s\n"), *Error);
		}
	}

	UE_LOG(LogAstoryTitles, Log, TEXT("достройка серверной таблицы:\n%s"), *OutReport);
}

// ═════════════════════════════════════════════════════════════════════════════
// Финальная выгрузка: два варианта серверной таблицы в одной папке
// ═════════════════════════════════════════════════════════════════════════════
void FAstoryTitleData::BuildFinalServerExport(FString& OutReport)
{
	OutReport.Empty();

	FAstoryTitleData D;
	FString Error;
	if (!D.LoadAll(Error) || D.GetServerRows().Num() == 0)
	{
		OutReport = TEXT("★ серверная таблица недоступна");
		UE_LOG(LogAstoryTitles, Warning, TEXT("%s"), *OutReport);
		return;
	}

	TArray<uint8> Orig;
	FFileHelper::LoadFileToArray(Orig, *D.GetServerFilePath());

	// Общий для обоих вариантов список мест на сервере (ищется ФАКТОМ).
	const TArray<FAstoryServerPlacement> All =
		FAstoryServerExport::DiscoverPlacements(TEXT("FameInfo.txt"), D.GetServerFileHash());
	TArray<FAstoryServerPlacement> Targets;
	for (const FAstoryServerPlacement& P : All)
	{
		if (P.bSameContent) { Targets.Add(P); }
	}

	FAstoryServerExportRequest Req;
	Req.ToolName = TEXT("Achievements Editor — финальная таблица званий");

	auto MakeVariant = [&](bool bIgnoreLimit, const TCHAR* SubFolder, const TCHAR* Note)
		-> int32
	{
		TArray<uint8> Bytes;
		int32 Changed = 0, Added = 0;
		TArray<int32> AddedIds, SkippedIds;
		FString E;
		if (!D.BuildServerFameInfo(Bytes, Changed, E, /*bAllMappedColumns=*/false,
				/*bAddMissingRows=*/true, &Added, &AddedIds, bIgnoreLimit, &SkippedIds))
		{
			OutReport += FString::Printf(TEXT("★ вариант «%s» не собрался: %s\n"), SubFolder, *E);
			return 0;
		}

		// Доказательство: старое содержимое, кроме счётчика в первой строке, не тронуто.
		int32 FirstLineEnd = 0;
		while (FirstLineEnd < Orig.Num() && Orig[FirstLineEnd] != '\n') { ++FirstLineEnd; }
		int32 TailDiffs = 0;
		for (int32 i = FirstLineEnd + 1; i < Orig.Num() && i < Bytes.Num(); ++i)
		{
			if (Orig[i] != Bytes[i]) { ++TailDiffs; }
		}
		OutReport += FString::Printf(
			TEXT("  вариант «%s»: записей %d (+%d) · %d б · существующих строк изменено %d · "
			     "отличий в старых строках %d — %s\n"),
			SubFolder, D.GetServerRows().Num() + Added, Added, Bytes.Num(), Changed, TailDiffs,
			TailDiffs == 0 ? TEXT("СТАРЫЕ СТРОКИ НЕ ТРОНУТЫ") : TEXT("★ СТАРЫЕ СТРОКИ ИЗМЕНЕНЫ"));

		FAstoryServerExportFile File;
		File.FileName = TEXT("FameInfo.txt");
		File.SubFolder = SubFolder;
		File.Note = Note;
		File.Content = MoveTemp(Bytes);
		File.Placements = Targets;
		File.AllPlacements = All;
		Req.Files.Add(MoveTemp(File));
		return Added;
	};

	OutReport += TEXT("ФИНАЛЬНАЯ ВЫГРУЗКА СЕРВЕРНОЙ ТАБЛИЦЫ\n");
	// ★ ОДИН вариант — ПОЛНАЯ таблица. Предел 256 снят патчем P21
	//   (иммедиат 0x4A19AA поднят до 0x200), маска расширена до 512 бит.
	//   Проверено на живом сервере: id 279 выдан и отображается.
	//   Урезанного варианта больше нет: инструмент отдаёт таблицу целиком.
	const int32 AddedFull = MakeVariant(true, TEXT(""),
		TEXT("Полная таблица званий. Требует патча P21 (маска 512 бит и "
		     "снятый предел загрузчика) — он уже стоит на боевом сервере."));

	Req.ChangeSummary = FString::Printf(
		TEXT("Финальная таблица званий: тексты приведены к русскому виду, числовые расхождения\r\n")
		TEXT("клиент/сервер сведены в пользу СЕРВЕРА, недостающие записи достроены.\r\n")
		TEXT("\r\n")
		TEXT("Файл один — полная таблица: %d записей (+%d к исходной серверной).\r\n")
		TEXT("\r\n")
		TEXT("Значения новых строк: сопоставленные колонки (0 Id, 1 Enabled, 12+5m OptionTypeId,\r\n")
		TEXT("42 Point) — из клиентской таблицы; колонки без клиентского близнеца — из строки-\r\n")
		TEXT("соседа (запись с наибольшим id).\r\n")
		TEXT("\r\n")
		TEXT("★ Разобранная семантика серверных колонок (загрузчик sub_4A18F0@0x4A18F0, запись 96 б):\r\n")
		TEXT("    кол. 1  -> +2  ФЛАГ: хранится как (atol != 0); значения 1 и 2 неразличимы;\r\n")
		TEXT("    кол. 3  -> +4  ЦЕЛЕВОЙ ПОРОГ: звание получено, когда прогресс >= этого числа;\r\n")
		TEXT("    кол. 43 -> +88 РЕЖИМ второго порога: 0 не используется, 1 абсолютный, 2 проценты;\r\n")
		TEXT("    кол. 44 -> +90 ВТОРОЙ ПОРОГ (абсолютное значение либо проценты от кол. 3);\r\n")
		TEXT("    кол. 45 -> +92 1 = универсальный накопитель прогресса (иначе прогресс считает\r\n")
		TEXT("                   хардкод switch по id прямо в EXE, и новый id он не знает).\r\n")
		TEXT("  ⇒ У всех достроенных строк кол. 45 = 1, поэтому прогресс им считает общий\r\n")
		TEXT("    накопитель, а не отсутствующая ветка switch.\r\n"),
		D.GetServerRows().Num() + AddedFull, AddedFull);

	Req.RollbackHint = FString::Printf(
		TEXT("Исходный серверный файл мы НЕ трогали, он на месте: %s\r\n")
		TEXT("Перед подменой сделайте копию рядом (FameInfo.txt.bak) — откат сводится к её возврату.\r\n")
		TEXT("Клиентская сторона откатывается из RawAssets/Data/ItemDB/_backup/ с последующим\r\n")
		TEXT("переимпортом DT_title_list."),
		*D.GetServerFilePath());

	FString ExportError;
	const FString Folder = FAstoryServerExport::Build(Req, ExportError);
	OutReport += FString::Printf(TEXT("\nпапка выгрузки: %s %s\n"), *Folder, *ExportError);

	UE_LOG(LogAstoryTitles, Log, TEXT("%s"), *OutReport);
}

// ═════════════════════════════════════════════════════════════════════════════
// Полная самопроверка (ничего не пишет на диск)
//   Доказывает три вещи разом:
//     1. обратная сборка title_list.bin совпадает с оригиналом ПОБАЙТОВО;
//     2. заново сериализованный CSV побайтово равен тому, что лежит на диске
//        ⇒ сохранение «вхолостую» не портит файл и не даёт ложных диффов;
//     3. то же для JSON.
//   Доступна командой консоли редактора `Astory.Titles.SelfCheck` и кнопкой на панели.
// ═════════════════════════════════════════════════════════════════════════════
// ═════════════════════════════════════════════════════════════════════════════
// Применение патча текстов
// ═════════════════════════════════════════════════════════════════════════════
void FAstoryTitleData::ApplyTextPatch(const FString& PatchCsvPath, bool bWriteBin,
                                      FString& OutReport, bool bExportServer)
{
	OutReport.Empty();

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *PatchCsvPath))
	{
		OutReport = FString::Printf(TEXT("★ не читается файл патча %s"), *PatchCsvPath);
		return;
	}
	TArray<TArray<FString>> Rows;
	AstoryCsv::Parse(Text, Rows);
	if (Rows.Num() < 2)
	{
		OutReport = TEXT("★ файл патча пуст");
		return;
	}
	TMap<FString, int32> Col;
	for (int32 i = 0; i < Rows[0].Num(); ++i)
	{
		Col.Add(Rows[0][i], i);
	}
	const int32* CId = Col.Find(TEXT("Id"));
	const int32* CField = Col.Find(TEXT("Field"));
	const int32* CValue = Col.Find(TEXT("Value"));
	const int32* COrigin = Col.Find(TEXT("Origin"));
	if (!CId || !CField || !CValue)
	{
		OutReport = TEXT("★ в патче нет колонок Id/Field/Value");
		return;
	}

	FAstoryTitleData D;
	FString Error;
	if (!D.LoadAll(Error))
	{
		OutReport = FString::Printf(TEXT("★ данные не загрузились: %s"), *Error);
		return;
	}

	TMap<int32, int32> ById;
	for (int32 i = 0; i < D.Records.Num(); ++i)
	{
		ById.Add(D.Records[i].GetId(), i);
	}

	int32 Applied = 0, Skipped = 0, TooLong = 0, NoRecord = 0, Same = 0;
	TMap<FString, int32> ByOrigin;
	FString Problems;

	for (int32 r = 1; r < Rows.Num(); ++r)
	{
		const TArray<FString>& R = Rows[r];
		if (!R.IsValidIndex(*CValue))
		{
			continue;
		}
		const int32 Id = FCString::Atoi(*R[*CId]);
		const FString Field = R[*CField];
		const FString Value = R[*CValue];
		const FString Origin = (COrigin && R.IsValidIndex(*COrigin)) ? R[*COrigin] : TEXT("");

		const int32* Idx = ById.Find(Id);
		if (!Idx)
		{
			++NoRecord;
			Problems += FString::Printf(TEXT("  id %d: записи нет\n"), Id);
			continue;
		}

		// Поле ищем в общей раскладке по имени колонки CSV — так патч умеет и тексты,
		//   и числовые поля (Enabled, Point и любые другие), не требуя правок кода.
		int32 Offset = INDEX_NONE;
		EAstoryFieldType FType = EAstoryFieldType::Int32;
		for (const FAstoryTitleFieldDef& F : AstoryTitleFields())
		{
			if (F.CsvName && Field == F.CsvName)
			{
				Offset = F.Offset;
				FType = F.Type;
				break;
			}
		}
		if (Offset == INDEX_NONE)
		{
			++Skipped;
			Problems += FString::Printf(TEXT("  id %d: поле «%s» не поддержано\n"), Id, *Field);
			continue;
		}

		if (FType == EAstoryFieldType::Int32)
		{
			const int32 NewNum = FCString::Atoi(*Value);
			FAstoryTitleRecord& NumRec = D.Records[*Idx];
			if (NumRec.GetInt(Offset) == NewNum)
			{
				++Same;
				continue;
			}
			NumRec.SetInt(Offset, NewNum);
			++Applied;
			ByOrigin.FindOrAdd(Origin)++;
			continue;
		}

		// ★ Длина в CP1251, а не в символах: поле однобайтовое.
		TArray<uint8> Bytes;
		StringToCp1251(Value, Bytes);
		if (Bytes.Num() > 63)
		{
			++TooLong;
			Problems += FString::Printf(
				TEXT("  id %d %s: %d байт > 63 — ОТКЛОНЕНО (влезет не целиком)\n"),
				Id, *Field, Bytes.Num());
			continue;
		}

		FAstoryTitleRecord& Rec = D.Records[*Idx];
		if (Rec.GetStr(Offset) == Value)
		{
			++Same;
			continue;
		}
		Rec.SetStr(Offset, 64, Value);
		++Applied;
		ByOrigin.FindOrAdd(Origin)++;
	}

	OutReport += FString::Printf(
		TEXT("ПАТЧ ТЕКСТОВ: %s\n  строк в патче %d · применено %d · уже совпадало %d\n"),
		*FPaths::GetCleanFilename(PatchCsvPath), Rows.Num() - 1, Applied, Same);
	for (const TPair<FString, int32>& P : ByOrigin)
	{
		OutReport += FString::Printf(TEXT("    %s: %d\n"), *P.Key, P.Value);
	}
	if (TooLong || NoRecord || Skipped)
	{
		OutReport += FString::Printf(TEXT("  ★ отклонено: длинных %d · без записи %d · чужих полей %d\n%s"),
			TooLong, NoRecord, Skipped, *Problems);
	}

	if (Applied == 0)
	{
		OutReport += TEXT("  менять нечего — файлы не трогаем\n");
		return;
	}

	// ── валидация перед записью (та же, что у кнопки «Сохранить») ──
	TArray<FAstoryIssue> Blocking, Inherited;
	for (const FAstoryIssue& I : D.Validate())
	{
		if (!I.IsBlocking())
		{
			continue;
		}
		(I.bPreExisting ? Inherited : Blocking).Add(I);
	}
	if (Inherited.Num() > 0)
	{
		// Унаследованные дефекты данных сохранять не мешают, но молчать о них нельзя.
		OutReport += FString::Printf(
			TEXT("  ⚠ унаследованных дефектов данных: %d (были до этой правки, не трогаем): %s\n"),
			Inherited.Num(), *Inherited[0].Text);
	}
	if (Blocking.Num() > 0)
	{
		OutReport += FString::Printf(TEXT("  ★ СОХРАНЕНИЕ ОТМЕНЕНО: НОВЫХ блокирующих ошибок %d\n"),
			Blocking.Num());
		for (int32 i = 0; i < FMath::Min(Blocking.Num(), 10); ++i)
		{
			OutReport += FString::Printf(TEXT("      id %d: %s\n"), Blocking[i].Id, *Blocking[i].Text);
		}
		return;
	}

	// ── серверная выгрузка: СОБИРАЕМ ДО Save() ──
	//   ★ `Save()` в конце переписывает `Baseline` сохранённым состоянием, и после него режим
	//   «только правки этой сессии» не увидел бы ничего. Поэтому запрос выгрузки формируем
	//   здесь, пока снимок ещё «как было до патча», а на диск раскладываем уже после успешного
	//   сохранения клиента.
	FAstoryServerExportRequest Req;
	int32 ServerChanged = 0;
	bool bReqOk = false;
	if (bExportServer)
	{
		FString ExpError;
		bReqOk = D.BuildServerExportRequest(Req, ServerChanged, ExpError,
			/*bAllMappedColumns=*/false, /*bAddMissingRows=*/false);
		if (!bReqOk)
		{
			OutReport += FString::Printf(TEXT("  ★ серверная выгрузка не собралась: %s\n"), *ExpError);
		}
	}

	FString Log;
	if (!D.Save(bWriteBin, Log, Error, /*bSavePackage=*/true))
	{
		OutReport += FString::Printf(TEXT("  ★ ошибка сохранения: %s\n%s"), *Error, *Log);
		return;
	}
	OutReport += Log;

	if (bReqOk)
	{
		FString ExpError;
		const FString Folder = FAstoryServerExport::Build(Req, ExpError);
		OutReport += FString::Printf(
			TEXT("  сервер: изменено строк %d · папка выгрузки %s %s\n"),
			ServerChanged, *Folder, *ExpError);
		for (const FAstoryServerExportFile& F : Req.Files)
		{
			for (const FAstoryServerPlacement& P : F.AllPlacements)
			{
				OutReport += FString::Printf(TEXT("    место: %s\\%s\\%s — %s\n"),
					*P.ServerKind, *P.RelativeDir, *F.FileName,
					P.bSameContent ? TEXT("то же содержимое ⇒ выгружено")
					               : TEXT("ДРУГОЕ содержимое ⇒ только предупреждение"));
			}
		}
	}

	// Контрольная сводка ПОСЛЕ записи — сколько описаний осталось помеченными.
	FAstoryTitleData Back;
	FString E;
	if (Back.LoadAll(E))
	{
		int32 Trunc = 0, Dash = 0;
		for (const FAstoryTitleRecord& R : Back.GetRecords())
		{
			if (R.GetStr(AstoryTitleOffsets::Desc).TrimStartAndEnd() == TEXT("-")) { ++Dash; }
			else if (R.IsDescLikelyTruncated()) { ++Trunc; }
		}
		OutReport += FString::Printf(
			TEXT("  после применения: описаний-пустышек «-» %d · помеченных обрезанными %d\n"),
			Dash, Trunc);
	}

	UE_LOG(LogAstoryTitles, Log, TEXT("%s"), *OutReport);
}

// ═════════════════════════════════════════════════════════════════════════════
// ПРОВЕРКА ВВОДА-ВЫВОДА
// ═════════════════════════════════════════════════════════════════════════════
namespace AstoryVerify
{
	// Смещения всех различающихся байтов (не больше Limit штук).
	static TArray<int32> DiffOffsets(const TArray<uint8>& A, const TArray<uint8>& B, int32 Limit = 64)
	{
		TArray<int32> Out;
		const int32 N = FMath::Min(A.Num(), B.Num());
		for (int32 i = 0; i < N && Out.Num() < Limit; ++i)
		{
			if (A[i] != B[i]) { Out.Add(i); }
		}
		return Out;
	}

	// В какую строку и какую колонку (табы) попадает байтовое смещение.
	static void LocateOffset(const TArray<uint8>& Data, int32 Offset, int32& OutLine, int32& OutCol)
	{
		OutLine = 0;
		int32 LineStart = 0;
		for (int32 i = 0; i < Offset && i < Data.Num(); ++i)
		{
			if (Data[i] == '\n') { ++OutLine; LineStart = i + 1; }
		}
		OutCol = 0;
		for (int32 i = LineStart; i < Offset && i < Data.Num(); ++i)
		{
			if (Data[i] == '\t') { ++OutCol; }
		}
	}

	// Сколько ячеек файла оканчиваются пробелом — их формат обязан сохраняться.
	static int32 CountTrailingSpaceCells(const TArray<uint8>& Data)
	{
		int32 N = 0;
		for (int32 i = 1; i < Data.Num(); ++i)
		{
			const uint8 C = Data[i];
			if ((C == '\t' || C == '\r' || C == '\n') && Data[i - 1] == ' ') { ++N; }
		}
		return N;
	}
}

void FAstoryTitleData::RunIoVerification(FString& OutReport)
{
	OutReport.Empty();

	// ── А. КЛИЕНТ: правка обязана доехать до CSV, JSON, .bin и DataTable ──
	OutReport += TEXT("[A] КЛИЕНТ: правка доезжает до того, что читает игра\n");

	// Снимок исходных файлов — в конце всё обязано вернуться к нему побайтово.
	TArray<uint8> WasCsv, WasJson, WasBin;
	FFileHelper::LoadFileToArray(WasCsv,  *CsvPath());
	FFileHelper::LoadFileToArray(WasJson, *JsonPath());
	FFileHelper::LoadFileToArray(WasBin,  *BinPath());

	FAstoryTitleData D;
	FString Error;
	if (!D.LoadAll(Error))
	{
		OutReport += FString::Printf(TEXT("    ★ данные не загрузились: %s\n"), *Error);
		return;
	}
	if (D.GetRecords().Num() == 0)
	{
		OutReport += TEXT("    ★ записей нет\n");
		return;
	}

	// Правим ОДНО поле у первой записи так, чтобы число цифр не изменилось.
	FAstoryTitleRecord& Rec = D.GetRecords()[0];
	const int32 TestId = Rec.GetId();
	const int32 WasPoint = Rec.GetInt(AstoryTitleOffsets::Point);
	const int32 NewPoint = (WasPoint % 10 == 9) ? WasPoint - 1 : WasPoint + 1;
	Rec.SetInt(AstoryTitleOffsets::Point, NewPoint);

	FString Log;
	if (!D.Save(/*bAlsoWriteBin=*/true, Log, Error, /*bSavePackage=*/false))
	{
		OutReport += FString::Printf(TEXT("    ★ сохранение не удалось: %s\n"), *Error);
		return;
	}
	OutReport += FString::Printf(TEXT("    правка: запись id %d, поле «Очки славы» %d -> %d\n"),
		TestId, WasPoint, NewPoint);

	// 1. Читаем обратно ИЗ ФАЙЛА CSV.
	{
		FAstoryTitleData Back;
		FString E2;
		Back.LoadAll(E2);
		const int32 Got = Back.GetRecords().Num() > 0
			? Back.GetRecords()[0].GetInt(AstoryTitleOffsets::Point) : -1;
		OutReport += FString::Printf(TEXT("    из title_list.csv на диске: %d — %s\n"),
			Got, Got == NewPoint ? TEXT("СОВПАДАЕТ") : TEXT("★ НЕ СОВПАДАЕТ"));
	}

	// 2. Читаем обратно ИЗ DataTable — это то, что реально читает порт.
#if WITH_EDITOR
	{
		const TCHAR* DtPath = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_title_list.DT_title_list");
		int32 Got = -1;
		FString How = TEXT("★ DataTable не найден");
		if (UDataTable* Table = LoadObject<UDataTable>(nullptr, DtPath))
		{
			// Имя строки CSV-импортёр берёт из ПЕРВОЙ колонки — это `_row`.
			if (const uint8* RowData = Table->FindRowUnchecked(FName(TEXT("0"))))
			{
				if (const UScriptStruct* RowStruct = Table->GetRowStruct())
				{
					if (FIntProperty* P = CastField<FIntProperty>(
							RowStruct->FindPropertyByName(FName(TEXT("Point")))))
					{
						Got = P->GetPropertyValue_InContainer(RowData);
						How = (Got == NewPoint) ? TEXT("СОВПАДАЕТ") : TEXT("★ НЕ СОВПАДАЕТ");
					}
					else { How = TEXT("★ свойства Point нет в RowStruct"); }
				}
			}
			else { How = TEXT("★ строки «0» в таблице нет"); }
		}
		OutReport += FString::Printf(TEXT("    из DT_title_list (в памяти после импорта): %d — %s\n"),
			Got, *How);
	}
#endif

	// 3. Читаем обратно ИЗ .bin — расшифровав запись.
	{
		TArray<uint8> Bin;
		int32 Got = -1;
		if (FFileHelper::LoadFileToArray(Bin, *BinPath()) && Bin.Num() >= 20 + 256)
		{
			TArray<uint8> Payload;
			Payload.Append(Bin.GetData() + 20 + 16, 240);
			DecryptTitleRecord(Payload);
			FMemory::Memcpy(&Got, Payload.GetData() + AstoryTitleOffsets::Point, 4);
		}
		OutReport += FString::Printf(TEXT("    из title_list.bin на диске: %d — %s\n"),
			Got, Got == NewPoint ? TEXT("СОВПАДАЕТ") : TEXT("★ НЕ СОВПАДАЕТ"));
	}

	// 4. Возвращаем как было и сверяем все три файла с исходными БАЙТ В БАЙТ.
	{
		FAstoryTitleData Restore;
		FString E3, L3;
		Restore.LoadAll(E3);
		if (Restore.GetRecords().Num() > 0)
		{
			Restore.GetRecords()[0].SetInt(AstoryTitleOffsets::Point, WasPoint);
			Restore.Save(/*bAlsoWriteBin=*/true, L3, E3, /*bSavePackage=*/false);
		}
		TArray<uint8> NowCsv, NowJson, NowBin;
		FFileHelper::LoadFileToArray(NowCsv,  *CsvPath());
		FFileHelper::LoadFileToArray(NowJson, *JsonPath());
		FFileHelper::LoadFileToArray(NowBin,  *BinPath());
		auto Same = [](const TArray<uint8>& A, const TArray<uint8>& B)
		{
			return A.Num() == B.Num() && (A.Num() == 0 ||
				FMemory::Memcmp(A.GetData(), B.GetData(), A.Num()) == 0);
		};
		OutReport += FString::Printf(
			TEXT("    возврат к исходному: csv %s · json %s · bin %s\n"),
			Same(WasCsv, NowCsv)   ? TEXT("побайтово") : TEXT("★ РАЗОШЁЛСЯ"),
			Same(WasJson, NowJson) ? TEXT("побайтово") : TEXT("★ РАЗОШЁЛСЯ"),
			Same(WasBin, NowBin)   ? TEXT("побайтово") : TEXT("★ РАЗОШЁЛСЯ"));
	}

	// ── Б. СЕРВЕР: цикл «прочитать -> не менять -> собрать» ──
	OutReport += TEXT("\n[Б] СЕРВЕР: полный цикл на ЖИВОМ файле (только чтение)\n");

	FAstoryTitleData S;
	if (!S.LoadAll(Error) || S.GetServerRows().Num() == 0)
	{
		OutReport += TEXT("    сервер недоступен — проверка пропущена\n");
		return;
	}
	TArray<uint8> Orig;
	FFileHelper::LoadFileToArray(Orig, *S.GetServerFilePath());
	OutReport += FString::Printf(TEXT("    источник: %s (%d б, записей %d)\n"),
		*S.GetServerFilePath(), Orig.Num(), S.GetServerRows().Num());

	{
		TArray<uint8> Made;
		int32 Changed = 0;
		FString E;
		S.BuildServerFameInfo(Made, Changed, E, /*bAllMappedColumns=*/false);
		const TArray<int32> Diffs = AstoryVerify::DiffOffsets(Orig, Made);
		OutReport += FString::Printf(
			TEXT("    без правок: длина %d против %d · расхождений %d · строк изменено %d — %s\n"),
			Made.Num(), Orig.Num(), Diffs.Num(), Changed,
			(Made.Num() == Orig.Num() && Diffs.Num() == 0) ? TEXT("ПОБАЙТОВО СОВПАДАЕТ")
			                                               : TEXT("★ РАСХОЖДЕНИЕ"));
		OutReport += FString::Printf(
			TEXT("    ячеек с хвостовым пробелом: в оригинале %d, в собранном %d — %s\n"),
			AstoryVerify::CountTrailingSpaceCells(Orig),
			AstoryVerify::CountTrailingSpaceCells(Made),
			AstoryVerify::CountTrailingSpaceCells(Orig) == AstoryVerify::CountTrailingSpaceCells(Made)
				? TEXT("формат сохранён") : TEXT("★ ФОРМАТ ПОТЕРЯН"));
	}

	// ── В. СЕРВЕР: цикл с ОДНОЙ правкой ──
	OutReport += TEXT("\n[В] СЕРВЕР: ровно одна правка — отличаться должна ровно она\n");
	{
		// Берём первую запись, у которой серверная строка есть.
		int32 Idx = INDEX_NONE;
		for (int32 i = 0; i < S.GetRecords().Num(); ++i)
		{
			if (S.HasServerRow(S.GetRecords()[i].GetId())) { Idx = i; break; }
		}
		if (Idx == INDEX_NONE)
		{
			OutReport += TEXT("    подходящей записи нет\n");
			return;
		}
		FAstoryTitleRecord& R = S.GetRecords()[Idx];
		const int32 Id = R.GetId();
		const FAstoryFameServerRow* SrvRow = S.GetServerRows().Find(Id);
		const int32 SrvPoint = SrvRow->Cols.IsValidIndex(42) ? SrvRow->Cols[42] : 0;
		// Меняем последнюю цифру, чтобы длина числа не изменилась — тогда и сдвига файла не будет.
		const int32 NewVal = (SrvPoint % 10 == 9) ? SrvPoint - 1 : SrvPoint + 1;
		R.SetInt(AstoryTitleOffsets::Point, NewVal);

		TArray<uint8> Made;
		int32 Changed = 0;
		FString E;
		S.BuildServerFameInfo(Made, Changed, E, /*bAllMappedColumns=*/false);
		const TArray<int32> Diffs = AstoryVerify::DiffOffsets(Orig, Made);

		OutReport += FString::Printf(
			TEXT("    правка: id %d, колонка 42 (очки славы) %d -> %d\n"), Id, SrvPoint, NewVal);
		OutReport += FString::Printf(
			TEXT("    длина файла: %d против %d — %s\n"), Made.Num(), Orig.Num(),
			Made.Num() == Orig.Num() ? TEXT("не изменилась") : TEXT("★ ИЗМЕНИЛАСЬ"));
		OutReport += FString::Printf(TEXT("    строк изменено: %d · отличающихся байтов: %d\n"),
			Changed, Diffs.Num());

		if (Diffs.Num() > 0)
		{
			int32 Line = 0, Col = 0;
			AstoryVerify::LocateOffset(Orig, Diffs[0], Line, Col);
			// Строка 0 — счётчик записей, поэтому у записи с индексом N номер строки N+1.
			const int32 ExpectedLine = Idx + 1;
			const bool bRightPlace = (Col == 42) && (Line == ExpectedLine);
			OutReport += FString::Printf(
				TEXT("    отличие на смещении %d: строка %d (ожидалась %d), колонка %d (ожидалась 42) — %s\n"),
				Diffs[0], Line, ExpectedLine, Col,
				bRightPlace ? TEXT("ИМЕННО ТА ПОЗИЦИЯ") : TEXT("★ НЕ ТА ПОЗИЦИЯ"));
		}
		OutReport += FString::Printf(TEXT("    весь остальной файл: %s\n"),
			(Diffs.Num() == 1 && Made.Num() == Orig.Num())
				? TEXT("побайтово прежний") : TEXT("★ есть лишние отличия"));

		R.SetInt(AstoryTitleOffsets::Point, SrvPoint);
	}

	UE_LOG(LogAstoryTitles, Log, TEXT("проверка ввода-вывода:\n%s"), *OutReport);
}

void FAstoryTitleData::RunSelfCheck(FString& OutReport)
{
	OutReport.Empty();

	// 1. Обратная сборка .bin.
	FString BinReport;
	const bool bBinOk = SelfTestBinRoundTrip(BinPath(), BinReport);
	OutReport += TEXT("[1] Обратная сборка title_list.bin\n");
	OutReport += BinReport + TEXT("\n\n");

	// 2/3. Сериализация против файлов на диске.
	FAstoryTitleData D;
	FString Error;
	if (!D.LoadAll(Error))
	{
		OutReport += FString::Printf(TEXT("[2] Загрузка не удалась: %s\n"), *Error);
		return;
	}

	auto CompareWithFile = [&OutReport](const TCHAR* What, const FString& Path, const FString& Made)
	{
		FString OnDisk;
		if (!FFileHelper::LoadFileToString(OnDisk, *Path))
		{
			OutReport += FString::Printf(TEXT("%s: файл %s не читается\n"), What, *Path);
			return;
		}
		if (OnDisk == Made)
		{
			OutReport += FString::Printf(TEXT("%s: СОВПАДАЕТ с %s (%d симв.)\n"),
				What, *FPaths::GetCleanFilename(Path), Made.Len());
			return;
		}
		// Показываем ПЕРВОЕ расхождение — по нему сразу видно, что именно разъехалось.
		int32 Pos = 0;
		const int32 N = FMath::Min(OnDisk.Len(), Made.Len());
		while (Pos < N && OnDisk[Pos] == Made[Pos]) { ++Pos; }
		OutReport += FString::Printf(
			TEXT("%s: ★ РАСХОЖДЕНИЕ с %s (на диске %d симв., собрано %d).\n")
			TEXT("    первое отличие на позиции %d\n")
			TEXT("    на диске : …%s…\n")
			TEXT("    собрано  : …%s…\n"),
			What, *FPaths::GetCleanFilename(Path), OnDisk.Len(), Made.Len(), Pos,
			*OnDisk.Mid(FMath::Max(0, Pos - 30), 70),
			*Made.Mid(FMath::Max(0, Pos - 30), 70));
	};

	OutReport += TEXT("[2] CSV, собранный заново, против файла на диске\n");
	CompareWithFile(TEXT("    title_list.csv"), CsvPath(), D.SerializeCsv());
	OutReport += TEXT("\n[3] JSON, собранный заново, против файла на диске\n");
	CompareWithFile(TEXT("    title_list.json"), JsonPath(), D.SerializeJson());

	// 4. Выгрузка «вхолостую» обязана дать файл, побайтово равный серверному оригиналу.
	//    Это доказательство, что редактор не портит серверную таблицу форматированием
	//    (в живом файле есть значения с хвостовым пробелом — «15 »).
	OutReport += TEXT("\n[4] Выгрузка для сервера без правок против исходного FameInfo.txt\n");
	if (D.GetServerRows().Num() == 0)
	{
		OutReport += TEXT("    сервер недоступен — проверка пропущена\n");
	}
	else
	{
		TArray<uint8> Made;
		int32 Changed = 0;
		FString BuildError;
		if (!D.BuildServerFameInfo(Made, Changed, BuildError, /*bAllMappedColumns=*/false))
		{
			OutReport += FString::Printf(TEXT("    ★ не собралось: %s\n"), *BuildError);
		}
		else
		{
			TArray<uint8> Orig;
			FFileHelper::LoadFileToArray(Orig, *D.GetServerFilePath());
			const bool bSame = (Made.Num() == Orig.Num()) &&
				FMemory::Memcmp(Made.GetData(), Orig.GetData(), Orig.Num()) == 0;
			OutReport += FString::Printf(
				TEXT("    изменённых строк %d · %s (%d б против %d б)\n"),
				Changed,
				bSame ? TEXT("ПОБАЙТОВО СОВПАДАЕТ") : TEXT("★ РАСХОЖДЕНИЕ"),
				Made.Num(), Orig.Num());
		}
	}

	// 5. Сводка по данным — заодно видно, что модель прочитала всё.
	int32 Unreachable = 0, NoServer = 0, Mismatch = 0, TruncDesc = 0, BadCat = 0;
	for (const FAstoryTitleRecord& R : D.GetRecords())
	{
		if (R.IsUnreachable()) { ++Unreachable; }
		if (D.GetServerRows().Num() > 0 && !D.HasServerRow(R.GetId())) { ++NoServer; }
		if (D.DiffWithServer(R).Num() > 0) { ++Mismatch; }
		if (R.IsDescLikelyTruncated()) { ++TruncDesc; }
		const int32 C = R.GetInt(AstoryTitleOffsets::CategoryLocIndex);
		if (C < 0 || C > 5) { ++BadCat; }
	}
	OutReport += FString::Printf(
		TEXT("\n[5] Сводка по данным\n")
		TEXT("    записей клиента %d · серверных строк %d (%s)\n")
		TEXT("    недостижимых (id ≥ 256) %d · нет у сервера %d · расхождений с сервером %d\n")
		TEXT("    описаний обрезано %d · категория вне 1460…1465 у %d\n"),
		D.GetRecords().Num(), D.GetServerRows().Num(),
		D.GetServerFilePath().IsEmpty() ? TEXT("сервер недоступен") : *D.GetServerFilePath(),
		Unreachable, NoServer, Mismatch, TruncDesc, BadCat);
	OutReport += FString::Printf(TEXT("    расхождения по полям: %s\n"),
		*D.DescribeServerMismatchBreakdown());

	// 6. Раскладка по ВКЛАДКАМ окна 18700 и знаменатель «Собрано %».
	//    Вкладку задаёт ОДНО поле записи — `Group` (+84); эталон
	//    `QuestLog_PopulateCategoryList@0xE98C00` сравнивает `*(rec+84) == индекс списка 18713`.
	//    Знаменатель «Собрано %» (нотис 2408) — эталонный `totalCount`: все записи вкладки
	//    с `Enabled != 0`, прошедшие гейт класса; НЕполученные тоже считаются.
	{
		int32 PerTab[3] = { 0, 0, 0 };          // всего записей во вкладке
		int32 PerTabEnabled[3] = { 0, 0, 0 };   // из них с `Enabled != 0` — это и есть знаменатель
		int32 BadGroup = 0;                     // `Group` вне 0…2 ⇒ звание не попадёт ни во вкладку
		for (const FAstoryTitleRecord& R : D.GetRecords())
		{
			const int32 G = R.GetInt(AstoryTitleOffsets::Group);
			if (G < 0 || G > 2) { ++BadGroup; continue; }
			++PerTab[G];
			if (R.GetInt(AstoryTitleOffsets::Enabled) != 0) { ++PerTabEnabled[G]; }
		}
		OutReport += FString::Printf(
			TEXT("\n[6] Вкладки окна 18700 (поле Group +84)\n")
			TEXT("    0 «Обычный» (loc 2405): всего %d, знаменатель «Собрано %%» %d\n")
			TEXT("    1 «Соревнование» (loc 2406): всего %d, знаменатель %d\n")
			TEXT("    2 «Секретные» (loc 2407): всего %d, знаменатель %d\n")
			TEXT("    ★ Group вне 0…2 (звание не видно ни в одной вкладке): %d\n"),
			PerTab[0], PerTabEnabled[0], PerTab[1], PerTabEnabled[1],
			PerTab[2], PerTabEnabled[2], BadGroup);
	}

	// 7. Проверка ввода-вывода: правка доезжает до клиента, серверный файл собирается
	//    байт в байт. Прогоняется той же одной командой — вспоминать вручную не нужно.
	FString IoReport;
	RunIoVerification(IoReport);
	OutReport += TEXT("\n[7] Проверка ввода-вывода\n") + IoReport;

	UE_LOG(LogAstoryTitles, Log, TEXT("самопроверка:\n%s"), *OutReport);
}

// ═════════════════════════════════════════════════════════════════════════════
// Экспорт выборки
// ═════════════════════════════════════════════════════════════════════════════
bool FAstoryTitleData::ExportSelectionCsv(const TArray<int32>& RowIndices, const FString& FilePath,
                                          FString& OutError) const
{
	const TArray<FAstoryTitleFieldDef>& Fields = AstoryTitleFields();

	FString Csv;
	TArray<FString> Header;
	for (const FAstoryTitleFieldDef& F : Fields)
	{
		Header.Add(AstoryCsv::Quote(F.Label));
	}
	Header.Add(TEXT("Грейд (текст)"));
	Header.Add(TEXT("Категория (текст)"));
	Header.Add(TEXT("Очки сервера"));
	Header.Add(TEXT("Состояние"));
	Csv += FString::Join(Header, TEXT(","));
	Csv += TEXT("\r\n");

	for (int32 Idx : RowIndices)
	{
		if (!Records.IsValidIndex(Idx))
		{
			continue;
		}
		const FAstoryTitleRecord& Rec = Records[Idx];
		TArray<FString> Cells;
		for (const FAstoryTitleFieldDef& F : Fields)
		{
			Cells.Add(F.Type == EAstoryFieldType::Int32
				? FString::FromInt(Rec.GetInt(F.Offset))
				: AstoryCsv::Quote(Rec.GetStr(F.Offset)));
		}
		Cells.Add(AstoryCsv::Quote(Loc(1450 + Rec.GetInt(AstoryTitleOffsets::ColorIndex))));
		Cells.Add(AstoryCsv::Quote(Loc(1460 + Rec.GetInt(AstoryTitleOffsets::CategoryLocIndex))));

		const FAstoryFameServerRow* Srv = ServerRows.Find(Rec.GetId());
		Cells.Add(Srv && Srv->Cols.IsValidIndex(42) ? FString::FromInt(Srv->Cols[42]) : TEXT("—"));

		TArray<FString> State;
		if (Rec.IsUnreachable())               { State.Add(TEXT("недостижимо")); }
		if (Rec.IsDisabled())                  { State.Add(TEXT("выключено")); }
		if (!Srv && ServerRows.Num() > 0)      { State.Add(TEXT("нет у сервера")); }
		if (DiffWithServer(Rec).Num() > 0)     { State.Add(TEXT("расхождение")); }
		Cells.Add(AstoryCsv::Quote(FString::Join(State, TEXT("; "))));

		Csv += FString::Join(Cells, TEXT(","));
		Csv += TEXT("\r\n");
	}

	// С BOM: файл открывают в Excel, без BOM кириллица там ломается.
	if (!FFileHelper::SaveStringToFile(Csv, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8))
	{
		OutError = FString::Printf(TEXT("не удалось записать %s"), *FilePath);
		return false;
	}
	return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Пересборка серверного FameInfo.txt
// ═════════════════════════════════════════════════════════════════════════════
FString FAstoryTitleData::DescribeServerMismatchBreakdown() const
{
	if (ServerRows.Num() == 0)
	{
		return TEXT("сервер недоступен");
	}
	TArray<FString> Parts;
	for (const FAstoryServerColMap& M : AstoryServerColMap())
	{
		if (M.ServerCol == 0)
		{
			continue;
		}
		int32 N = 0;
		for (const FAstoryTitleRecord& R : Records)
		{
			const FAstoryFameServerRow* S = ServerRows.Find(R.GetId());
			if (S && S->Cols.IsValidIndex(M.ServerCol) && S->Cols[M.ServerCol] != R.GetInt(M.ClientOffset))
			{
				++N;
			}
		}
		if (N > 0)
		{
			Parts.Add(FString::Printf(TEXT("%s — %d"), M.Label, N));
		}
	}
	return Parts.Num() > 0 ? FString::Join(Parts, TEXT(", ")) : TEXT("расхождений нет");
}

bool FAstoryTitleData::IsFieldEditedSinceLoad(int32 Id, int32 ClientOffset) const
{
	const FAstoryTitleRecord* Was = Baseline.Find(Id);
	if (!Was)
	{
		return true;                       // записи при загрузке не было ⇒ она новая, значит правка
	}
	const FAstoryTitleRecord* Now = Records.FindByPredicate(
		[Id](const FAstoryTitleRecord& R) { return R.GetId() == Id; });
	return Now && Now->GetInt(ClientOffset) != Was->GetInt(ClientOffset);
}

void FAstoryTitleData::CountServerChanges(int32& OutMyEdits, int32& OutAllMapped) const
{
	TArray<uint8> Bytes;
	FString Error;
	OutMyEdits = OutAllMapped = 0;
	BuildServerFameInfo(Bytes, OutMyEdits, Error, /*bAllMappedColumns=*/false);
	BuildServerFameInfo(Bytes, OutAllMapped, Error, /*bAllMappedColumns=*/true);
}

bool FAstoryTitleData::BuildServerFameInfo(TArray<uint8>& OutBytes, int32& OutChangedRows,
                                           FString& OutError, bool bAllMappedColumns,
                                           bool bAddMissingRows, int32* OutAddedRows,
                                           TArray<int32>* OutAddedIds, bool bIgnoreLoaderLimit,
                                           TArray<int32>* OutSkippedIds) const
{
	OutBytes.Reset();
	OutChangedRows = 0;
	if (OutAddedRows) { *OutAddedRows = 0; }
	if (OutAddedIds)  { OutAddedIds->Reset(); }
	if (OutSkippedIds) { OutSkippedIds->Reset(); }
	if (ServerRawLines.Num() == 0)
	{
		OutError = TEXT("серверный FameInfo.txt не загружен");
		return false;
	}

	// Клиентские записи по id — для подстановки сопоставленных колонок.
	TMap<int32, const FAstoryTitleRecord*> ById;
	for (const FAstoryTitleRecord& R : Records)
	{
		ById.Add(R.GetId(), &R);
	}

	// ── строка-СОСЕД: шаблон для новых записей ──
	//   Берём запись с НАИБОЛЬШИМ id (у нашей таблицы это id 250 — последняя строка той же серии
	//   событийных званий, что и добавляемые 251…279). Колонки, у которых клиентского близнеца
	//   НЕТ (2, 3, 4, 5…11, 43, 44, 45), переносятся из неё как есть — «как у соседей», а не
	//   нулями наугад.
	TArray<FString> Template;
	int32 TemplateId = INDEX_NONE;
	for (const TPair<int32, FAstoryFameServerRow>& P : ServerRows)
	{
		if (P.Key > TemplateId) { TemplateId = P.Key; }
	}

	TArray<FString> OutLines;
	OutLines.Reserve(ServerRawLines.Num() + 32);

	for (int32 i = 0; i < ServerRawLines.Num(); ++i)
	{
		FString Line = ServerRawLines[i];
		if (i == 0 || Line.TrimStartAndEnd().IsEmpty())
		{
			// Первая строка — счётчик записей; правится ТОЛЬКО если дописываем строки.
			OutLines.Add(Line);
			continue;
		}

		TArray<FString> Parts;
		Line.ParseIntoArray(Parts, TEXT("\t"), /*CullEmpty=*/false);
		if (Parts.Num() >= 2)
		{
			const int32 Id = FCString::Atoi(*Parts[0]);
			if (Id == TemplateId)
			{
				Template = Parts;                       // запомнили строку-соседа
			}
			if (const FAstoryTitleRecord* const* RecPtr = ById.Find(Id))
			{
				bool bChanged = false;
				for (const FAstoryServerColMap& M : AstoryServerColMap())
				{
					if (M.ServerCol == 0 || !Parts.IsValidIndex(M.ServerCol))
					{
						continue;
					}
					// ★ В режиме «только мои правки» трогаем колонку, лишь если клиентское
					//   значение изменилось с момента загрузки. Расхождения, которые были в
					//   данных и до нас, не переписываем: авторитет данных — сервер.
					if (!bAllMappedColumns && !IsFieldEditedSinceLoad(Id, M.ClientOffset))
					{
						continue;
					}
					const int32 NewNum = (*RecPtr)->GetInt(M.ClientOffset);
					// ★ Сравнение ЧИСЛОВОЕ: в живом файле встречаются значения с хвостовым
					//   пробелом («15 »), и строковое сравнение объявляло бы правкой каждое
					//   такое поле — 84 ложных изменения на ровном месте.
					if (FCString::Atoi(*Parts[M.ServerCol]) != NewNum)
					{
						Parts[M.ServerCol] = FString::FromInt(NewNum);
						bChanged = true;
					}
				}
				if (bChanged)
				{
					++OutChangedRows;
					Line = FString::Join(Parts, TEXT("\t"));
				}
			}
		}
		OutLines.Add(Line);
	}

	// ── достройка недостающих записей ──
	if (bAddMissingRows)
	{
		if (Template.Num() < 46)
		{
			OutError = FString::Printf(
				TEXT("не найдена строка-шаблон (id %d): в ней %d колонок"),
				TemplateId, Template.Num());
			return false;
		}

		TArray<int32> Missing;
		for (const FAstoryTitleRecord& R : Records)
		{
			if (!ServerRows.Contains(R.GetId()))
			{
				Missing.Add(R.GetId());
			}
		}
		Missing.Sort();

		// Новые строки идут в КОНЕЦ, перед завершающей пустой строкой файла.
		int32 InsertAt = OutLines.Num();
		while (InsertAt > 0 && OutLines[InsertAt - 1].TrimStartAndEnd().IsEmpty())
		{
			--InsertAt;
		}

		TArray<FString> NewLines;
		for (int32 Id : Missing)
		{
			const FAstoryTitleRecord* const* RecPtr = ById.Find(Id);
			if (!RecPtr)
			{
				continue;
			}
			// ★★★ Предел загрузчика: объявишь больше 256 записей — сервер не загрузит таблицу
			//   ВООБЩЕ («FameInfo count over», sub_4A18F0@0x4A18F0), и пропадут все звания.
			if (!bIgnoreLoaderLimit &&
			    ServerRows.Num() + NewLines.Num() >= AstoryServerFameMaxRows)
			{
				if (OutSkippedIds) { OutSkippedIds->Add(Id); }
				continue;
			}
			TArray<FString> Cells = Template;           // всё «как у соседа»
			Cells[0] = FString::FromInt(Id);
			for (const FAstoryServerColMap& M : AstoryServerColMap())
			{
				if (M.ServerCol == 0 || !Cells.IsValidIndex(M.ServerCol))
				{
					continue;
				}
				Cells[M.ServerCol] = FString::FromInt((*RecPtr)->GetInt(M.ClientOffset));
			}
			NewLines.Add(FString::Join(Cells, TEXT("\t")));
			if (OutAddedIds) { OutAddedIds->Add(Id); }
		}
		OutLines.Insert(NewLines, InsertAt);
		if (OutAddedRows) { *OutAddedRows = NewLines.Num(); }

		// Счётчик записей в первой строке — единственная правка старого содержимого.
		if (NewLines.Num() > 0 && OutLines.Num() > 0)
		{
			OutLines[0] = FString::FromInt(ServerRows.Num() + NewLines.Num());
		}
	}

	// Серверные таблицы — однобайтовые (CP1251). В FameInfo.txt только числа и табы,
	//   но кодировку всё равно держим серверную, а не UTF-8.
	TArray<uint8> Bytes;
	StringToCp1251(FString::Join(OutLines, TEXT("\r\n")), Bytes);
	OutBytes = MoveTemp(Bytes);
	return true;
}

bool FAstoryTitleData::BuildServerExportRequest(FAstoryServerExportRequest& OutRequest,
                                                int32& OutChangedRows, FString& OutError,
                                                bool bAllMappedColumns,
                                                bool bAddMissingRows,
                                                bool bIgnoreLoaderLimit) const
{
	OutChangedRows = 0;
	if (ServerRows.Num() == 0)
	{
		OutError = TEXT("серверный FameInfo.txt не загружен — выгружать нечего");
		return false;
	}

	TArray<uint8> Content;
	int32 Added = 0;
	TArray<int32> AddedIds, SkippedIds;
	if (!BuildServerFameInfo(Content, OutChangedRows, OutError, bAllMappedColumns,
			bAddMissingRows, &Added, &AddedIds, bIgnoreLoaderLimit, &SkippedIds))
	{
		return false;
	}

	// ★ Записка о достроенных строках и о тех из них, что пока физически не работают.
	FString AddedNote;
	if (Added > 0)
	{
		TArray<int32> Unreachable;
		FString IdList;
		for (int32 k = 0; k < AddedIds.Num(); ++k)
		{
			IdList += (k ? TEXT(", ") : TEXT("")) + FString::FromInt(AddedIds[k]);
			if (AddedIds[k] >= AstoryFameMaskBits) { Unreachable.Add(AddedIds[k]); }
		}
		AddedNote = FString::Printf(
			TEXT("\r\n★ ДОБАВЛЕНО НОВЫХ ЗАПИСЕЙ: %d (было %d, стало %d).\r\n")
			TEXT("   id: %s\r\n")
			TEXT("   Сопоставленные колонки (0 Id, 1 Enabled, 12+5m OptionTypeId, 42 Point) взяты из\r\n")
			TEXT("   клиентской таблицы. Колонки без клиентского близнеца (2, 3, 4, 5..11, 43, 44, 45)\r\n")
			TEXT("   скопированы из строки-СОСЕДА — записи с наибольшим id (id %d), это последняя\r\n")
			TEXT("   строка той же серии событийных званий.\r\n")
			TEXT("   ⚠ Колонки 3 и 44 — УСЛОВИЕ ВЫДАЧИ звания (порог). Клиентского близнеца у них нет,\r\n")
			TEXT("   поэтому они взяты у соседа и их НУЖНО ПРОВЕРИТЬ перед боевым запуском: иначе\r\n")
			TEXT("   звание будет выдаваться не за то, за что задумано.\r\n")
			TEXT("   Существующие %d строк НЕ изменялись; в старом содержимом правится только\r\n")
			TEXT("   счётчик записей в первой строке (%d -> %d).\r\n"),
			Added, ServerRows.Num(), ServerRows.Num() + Added, *IdList,
			AddedIds.Num() > 0 ? ServerRows.Num() - 1 : 0,
			ServerRows.Num(), ServerRows.Num(), ServerRows.Num() + Added);

		if (SkippedIds.Num() > 0)
		{
			FString SkipList;
			for (int32 k = 0; k < SkippedIds.Num(); ++k)
			{
				SkipList += (k ? TEXT(", ") : TEXT("")) + FString::FromInt(SkippedIds[k]);
			}
			AddedNote += FString::Printf(
				TEXT("\r\n★★★ НЕ ДОБАВЛЕНО %d ЗАПИСЕЙ — упёрлись в предел загрузчика (%d).\r\n")
				TEXT("   Предел уже поднят патчем P21 до 512 (иммедиат 0x4A19AA). Столько\r\n")
				TEXT("   записей таблица не вмещает — нужен ещё один патч загрузчика.\r\n")
				TEXT("   id: %s\r\n"),
				SkippedIds.Num(), AstoryServerFameMaxRows, *SkipList);
		}
		if (Unreachable.Num() > 0)
		{
			FString UnrList;
			for (int32 k = 0; k < Unreachable.Num(); ++k)
			{
				UnrList += (k ? TEXT(", ") : TEXT("")) + FString::FromInt(Unreachable[k]);
			}
			AddedNote += FString::Printf(
				TEXT("\r\n★★★ ВНИМАНИЕ: %d из добавленных записей имеют id >= %d и СЕЙЧАС НЕ РАБОТАЮТ.\r\n")
				TEXT("   Маска «получено» в протоколе (опкоды 6066/6086) ровно %d бит, поэтому сервер\r\n")
				TEXT("   физически не может отметить такой бит. Строки в таблице лежат и ждут: они\r\n")
				TEXT("   заработают ТОЛЬКО после расширения маски (её сейчас расширяют до 512).\r\n")
				TEXT("   id: %s\r\n"),
				Unreachable.Num(), AstoryFameMaskBits, AstoryFameMaskBits, *UnrList);
		}
	}

	FAstoryServerExportFile File;
	File.FileName = TEXT("FameInfo.txt");
	File.Content = MoveTemp(Content);
	// ★ Тип сервера и путь определяются ФАКТОМ — сканом серверных корней, а не догадкой.
	File.AllPlacements = FAstoryServerExport::DiscoverPlacements(File.FileName, ServerFileHash);
	for (const FAstoryServerPlacement& P : File.AllPlacements)
	{
		if (P.bSameContent)
		{
			File.Placements.Add(P);
		}
	}

	OutRequest.ToolName = TEXT("Achievements Editor (звания / Fame)");
	OutRequest.ChangeSummary = FString::Printf(
		TEXT("Источник правок: клиентская таблица title_list (RawAssets/Data/ItemDB/title_list.csv).\r\n")
		TEXT("Клиентских записей: %d. Серверных строк в FameInfo.txt: %d.\r\n")
		TEXT("Строк, изменённых в серверном файле: %d.\r\n")
		TEXT("Режим: %s.\r\n")
		TEXT("\r\n")
		TEXT("Меняются ТОЛЬКО те колонки, чьё соответствие клиентскому полю доказано по коду\r\n")
		TEXT("загрузчика LoadFameInfo@0x49F410:\r\n")
		TEXT("    кол. 0       = Id\r\n")
		TEXT("    кол. 1       = гейт «показывать» (клиентское Enabled, +200)\r\n")
		TEXT("    кол. 12 + 5m = тип слот-баффа m (клиентское OptionTypeId(m), +176 + 4m)\r\n")
		TEXT("    кол. 42      = очки славы (клиентское Point, +96)\r\n")
		TEXT("Остальные колонки — условия выдачи, клиентского близнеца не имеют и перенесены 1:1.\r\n")
		TEXT("\r\n")
		TEXT("★★★ ПРЕДЕЛ ЗАГРУЗЧИКА: читатель этого файла — sub_4A18F0@0x4A18F0 (зовётся из\r\n")
		TEXT("LoadFameInfo@0x49F410) — проверяет  if (count > 0 && count <= 256).  Если объявить\r\n")
		TEXT("больше, он печатает «FameInfo count over» и НЕ ГРУЗИТ ТАБЛИЦУ ВООБЩЕ — пропадут ВСЕ\r\n")
		TEXT("звания, а не только лишние. Поэтому записей в файле не больше 256.\r\n")
		TEXT("\r\n")
		TEXT("★ Кто какой файл читает (проверено по строкам в самих .exe):\r\n")
		TEXT("    GameServer.exe  — GameServer\\ConfigSet\\FameInfo.txt (251 запись, 46 колонок);\r\n")
		TEXT("                      Indun_GameServer\\ConfigSet\\ — байт-в-байт тот же файл;\r\n")
		TEXT("    NpcServer.exe   — NpcServer\\FameInfo\\FameInfo.txt — ДРУГАЯ таблица (63 записи),\r\n")
		TEXT("                      к title_list отношения не имеет, её мы не трогаем;\r\n")
		TEXT("    не читает никто — GameServer\\ItemTable_Info\\FameInfo.txt (147 записей, 2009 г.).\r\n"),
		Records.Num(), ServerRows.Num(), OutChangedRows,
		bAllMappedColumns
			? TEXT("выровнять ВСЕ сопоставленные колонки по клиенту (включая расхождения, "
			       "возникшие до этой сессии)")
			: TEXT("перенести ТОЛЬКО правки этой сессии; ранее возникшие расхождения не "
			       "затрагиваются — авторитет данных остаётся за сервером"));
	OutRequest.ChangeSummary += AddedNote;
	OutRequest.RollbackHint = FString::Printf(
		TEXT("Исходный серверный файл мы НЕ трогали, он на месте: %s\r\n")
		TEXT("Перед подменой сделайте копию рядом (FameInfo.txt.bak) — и откат сводится к её возврату.\r\n")
		TEXT("Клиентская сторона откатывается из RawAssets/Data/ItemDB/_backup/ (копии с меткой\r\n")
		TEXT("времени) с последующим переимпортом DT_title_list."),
		*ServerFilePath);
	OutRequest.Files.Add(MoveTemp(File));
	return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Команда консоли редактора: полная самопроверка без запуска UI.
//   Нужна, чтобы прогонять доказательство обратной сборки headless-запуском, не открывая
//   ни одного окна:  UnrealEditor-Cmd.exe <проект> -run=... -ExecCmds="Astory.Titles.SelfCheck"
// ═════════════════════════════════════════════════════════════════════════════
static FAutoConsoleCommand GAstoryTitlesSelfCheckCmd(
	TEXT("Astory.Titles.SelfCheck"),
	TEXT("Звания: обратная сборка title_list.bin побайтово + сверка заново собранных CSV/JSON с файлами на диске."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		FString Report;
		FAstoryTitleData::RunSelfCheck(Report);
	}));

// Применение патча текстов: Astory.Titles.ApplyPatch [путь] [nobin]
static FAutoConsoleCommand GAstoryTitlesApplyPatchCmd(
	TEXT("Astory.Titles.ApplyPatch"),
	TEXT("Звания: применить патч полей (CSV Id,Field,Value,Origin) через редактор. "
	     "Ключи: nobin — не пересобирать .bin; export — заодно собрать выгрузку FameInfo.txt."),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		// ★★★ ПУТЬ ОБЯЗАТЕЛЕН. Раньше здесь стоял путь по умолчанию
		//   (`Reports/merged_titles/title_patch.csv`) — и это оказалось миной: стоит
		//   потерять аргумент (например, `-ExecCmds` без кавычек режет строку по первому
		//   пробелу), как команда МОЛЧА применяла старый слитый патч и откатывала свежие
		//   решения. Ровно так 30.08.2026 у званий 65…69 снова обнулились `Enabled` и `Point`.
		//   Теперь без пути команда ничего не делает и говорит об этом.
		if (Args.Num() == 0 || Args[0].StartsWith(TEXT("-")) ||
		    Args[0].Equals(TEXT("nobin"), ESearchCase::IgnoreCase) ||
		    Args[0].Equals(TEXT("export"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogAstoryTitles, Error,
				TEXT("Astory.Titles.ApplyPatch: ПЕРВЫМ аргументом обязан идти путь к файлу патча. "
				     "Пути по умолчанию больше нет — он молча применял устаревший title_patch.csv. "
				     "Пример: Astory.Titles.ApplyPatch D:/.../title_patch_xxx.csv export"));
			return;
		}
		const FString Path = Args[0];
		const bool bNoBin = Args.ContainsByPredicate([](const FString& A)
		{
			return A.Equals(TEXT("nobin"), ESearchCase::IgnoreCase);
		});
		// ★ Ключ `export` — заодно собрать выгрузку серверного FameInfo.txt в режиме
		//   «только правки этой сессии». Отдельной командой это сделать НЕЛЬЗЯ: снимок
		//   `Baseline` живёт ровно одну загрузку.
		const bool bExport = Args.ContainsByPredicate([](const FString& A)
		{
			return A.Equals(TEXT("export"), ESearchCase::IgnoreCase);
		});
		FString Report;
		FAstoryTitleData::ApplyTextPatch(Path, !bNoBin, Report, bExport);
		// ★ Печатаем отчёт ЗДЕСЬ, а не внутри: у ApplyTextPatch есть ранние выходы
		//   (файла нет, менять нечего, валидация не пустила) — без этого они молчали.
		UE_LOG(LogAstoryTitles, Log, TEXT("%s"), *Report);
	}));

// Финальная выгрузка обоих вариантов серверной таблицы.
static FAutoConsoleCommand GAstoryTitlesFinalExportCmd(
	TEXT("Astory.Titles.ServerFinalExport"),
	TEXT("Звания: выгрузить ДВА варианта FameInfo.txt — на 256 (сейчас) и на 280 (после патчей)."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		FString Report;
		FAstoryTitleData::BuildFinalServerExport(Report);
	}));

// Достройка серверной таблицы недостающими записями + выгрузка в обе ветки.
static FAutoConsoleCommand GAstoryTitlesServerAddMissingCmd(
	TEXT("Astory.Titles.ServerAddMissing"),
	TEXT("Звания: достроить серверную FameInfo.txt недостающими записями, проверить побайтово и выгрузить."),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		const bool bNoExport = Args.ContainsByPredicate([](const FString& A)
		{
			return A.Equals(TEXT("noexport"), ESearchCase::IgnoreCase);
		});
		const bool bFull = Args.ContainsByPredicate([](const FString& A)
		{
			return A.Equals(TEXT("full"), ESearchCase::IgnoreCase);
		});
		FString Report;
		FAstoryTitleData::BuildAndVerifyServerAddMissing(!bNoExport, Report, bFull);
	}));

// Только проверка ввода-вывода (клиент + сервер), без остальной самопроверки.
static FAutoConsoleCommand GAstoryTitlesVerifyIoCmd(
	TEXT("Astory.Titles.VerifyIO"),
	TEXT("Звания: правка доезжает до CSV/JSON/.bin/DataTable; серверный файл собирается байт в байт."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		FString Report;
		FAstoryTitleData::RunIoVerification(Report);
	}));

// Пробная выгрузка для сервера БЕЗ открытия проводника — чтобы проверять headless-запуском,
//   что места на серверах находятся фактом и раскладка папок верна.
static FAutoConsoleCommand GAstoryTitlesServerExportDryRunCmd(
	TEXT("Astory.Titles.ServerExportDryRun"),
	TEXT("Звания: собрать выгрузку FameInfo.txt во временную папку и НЕ открывать проводник."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		FAstoryTitleData D;
		FString Error;
		if (!D.LoadAll(Error))
		{
			UE_LOG(LogAstoryTitles, Error, TEXT("выгрузка: данные не загрузились — %s"), *Error);
			return;
		}
		int32 MyEdits = 0, AllMapped = 0;
		D.CountServerChanges(MyEdits, AllMapped);
		UE_LOG(LogAstoryTitles, Log,
			TEXT("выгрузка: правок этой сессии %d строк · выравнивание всех колонок %d строк"),
			MyEdits, AllMapped);

		FAstoryServerExportRequest Req;
		int32 Changed = 0;
		// В пробном прогоне правок нет, поэтому собираем в режиме полного выравнивания —
		//   иначе проверять было бы нечего.
		if (!D.BuildServerExportRequest(Req, Changed, Error, /*bAllMappedColumns=*/true))
		{
			UE_LOG(LogAstoryTitles, Error, TEXT("выгрузка: %s"), *Error);
			return;
		}
		for (const FAstoryServerExportFile& F : Req.Files)
		{
			for (const FAstoryServerPlacement& P : F.AllPlacements)
			{
				UE_LOG(LogAstoryTitles, Log, TEXT("  найдено: %s\\%s\\%s — %s (%lld б, md5 %s)"),
					*P.ServerKind, *P.RelativeDir, *F.FileName,
					P.bSameContent ? TEXT("ТО ЖЕ содержимое ⇒ выгружаем")
					               : TEXT("ДРУГОЕ содержимое ⇒ только предупреждение"),
					P.SourceSize, *P.SourceHash.Left(8));
			}
		}
		const FString Folder = FAstoryServerExport::Build(Req, Error);
		UE_LOG(LogAstoryTitles, Log, TEXT("выгрузка (пробная): изменено строк %d, папка %s %s"),
			Changed, *Folder, *Error);
	}));
