// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ МНОГОЯЗЫЧНОГО ТЕКСТА ЧАТА (RU + EN + AR), 08.10.2026. [ПОРТ]
//   Отчёт: `Reports/owner_1008/arabic_text.md`. Кодек: `FAsdaLoginData::EncodeWireText/DecodeWireText` (`asda.Text.WireCodec`).
//
// ★ ЧТО ПРОВЕРЯЕТ (ожидаемые байты заданы ЯВНО, а не вызовом проверяемого кода):
//   A. Кодек: «Hello» → ASCII; «Привет» → cp1251 БАЙТ-В-БАЙТ как прежний `EncodeCp1251`; «مرحبا» → маркер 98 + cp1256 E3 D1 CD C8 C7;
//      смешанная RU+AR → UTF-8; круговой путь; граница (обрезка ТОЛЬКО по символу, маркер не одинокий); обрубок-хвост UTF-8 при приёме;
//      длина в байтах провода; регрессия cp1251 (алфавит + « » – — … №; ВСЕ пары букв C0..FF — как cp1251; энкодер cp1251 никогда
//      не выдаёт 0x98); режим 2 (без маркера: арабский → UTF-8, приём маркера всё равно понимает); откат `asda.Text.WireCodec 0`.
//   B. БОЕВЫЕ отправители под перехватом (сокета нет): 5084, 5086, 6560, 5088, 5105, 4120 — тело = ожидаемые байты, имена cp1251,
//      апостроф шёпота → обратная кавычка ДО кодирования, длинная арабская строка режется по символу в пределах буфера отправителя.
//   C. Приём через ШТАТНЫЙ `HandlePacket` (`DebugFeedPacket`): 5085, 5087, 5089, 4121, 5106, 6561, 6719 — тело UTF-8 (арабское и
//      смешанное) и старое cp1251 (русское) дают верную строку чата и облачко; имя cp1251 «Тест» не портится.
//
//   E. Имена/поля БД: публичные EncodeCp1251/DecodeCp1251 = гибрид (арабское имя — маркер+cp1256, русское — прежние байты),
//      правило имени по локалям оригинала (case 7 / case 8, смешение запрещено, `asda.Text.NameLocale 0`), лимиты с маркером.
//   D. ПУТЬ ИГРОКА (`shot`): свой павн, приём 5085/5087/5089/5106 (строки ленты + облачко над СВОЕЙ головой), НАСТОЯЩИЕ нажатия
//      в строку ввода чата через Slate (`ProcessKeyCharEvent`) — смешанная строка и переполнение арабским (стоп по БАЙТАМ провода),
//      общая плашка-подсказка с арабским, кадр `asda_ar_chat.png`.
//
// Консоль: `asda.Text.SelfTestArabic [shot] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки проверок — «САМОТЕСТ-AR», итог — «САМОТЕСТ-AR ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                    // ★ 05.11.2026: WrI16 — WORD@6 (userNum получателя) в телах 5089
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "Test/AsdaSelfTestRunGate.h"   // RefuseDebugWhenOnline — офлайн-гейт самотестов   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Login/AsdaLoginData.h"      // проверяемый кодек
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UI/AsdaChatWindowSubsystem.h"      // путь игрока: строка ввода чата (OpenInput/CloseInput)
#include "UI/AsdaItemTooltipSubsystem.h"     // путь игрока: общая плашка-подсказка (ShowTextTip/HideTooltip)
#include "UI/AsdaTipPanel.h"                 // EAsdaTipOwner / FAsdaTipMetrics
#include "World/AsdaWorldSubsystem.h"        // свой павн — облачко над головой; соседи с арабским/русским именем (плашка)
#include "World/AsdaCoord.h"                  // тайл своего павна → позиции соседей
#include "Kismet/GameplayStatics.h"           // проекция головы соседа на экран — клик игрока по соседу
#include "Engine/GameViewportClient.h"
#include "Widgets/SViewport.h"
#include "Framework/Application/SlateApplication.h"   // настоящие нажатия (ProcessKeyCharEvent) и фокус клавиатуры
#include "Widgets/Input/SEditableText.h"     // текст поля под фокусом
#include "HighResScreenshot.h"               // кадр (FScreenshotRequest)
#include "TimerManager.h"
#include "UI/AsdaWindowSubsystem.h"            // SimulateClickAt — клик мышью по живой геометрии
#include "UI/AsdaUiHelpers.h"                  // WidgetAbsRect — живой прямоугольник поля ввода
#include "Components/EditableText.h"
#include "UObject/UObjectIterator.h"
#include "Text/AsdaTextSubsystem.h"            // D2c: loc 6937 языка сессии (префикс клан-чата)
#include "Text/AsdaLang.h"                     // D2c: код языка сессии в строке проверки

DEFINE_LOG_CATEGORY_STATIC(LogAsdaArabicTest, Log, All);   // канал строк самотеста

// Друг `UAsdaNetSubsystem` (объявлен в его шапке) — нужен доступ к перехватчику исходящих (`ArmSendCapture`).
struct FAsdaArabicTextSelfTest
{
	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено

	// Одна проверка: строка в лог и счёт.
	void Expect(bool b, const FString& What)
	{
		if (b) { ++Ok; UE_LOG(LogAsdaArabicTest, Log, TEXT("САМОТЕСТ-AR [OK]   %s"), *What); }
		else   { ++Fail; UE_LOG(LogAsdaArabicTest, Error, TEXT("САМОТЕСТ-AR [FAIL] %s"), *What); }
	}

	// Байты → «D9 85 …» для строк лога.
	static FString Hex(const TArray<uint8>& B)
	{
		FString S;
		for (const uint8 c : B) { S += FString::Printf(TEXT("%02X "), c); }
		return S.TrimEnd();
	}

	// Закодировать свободный текст в массив (буфер с запасом).
	static TArray<uint8> Wire(const FString& S, int32 MaxBytes = 400)
	{
		TArray<uint8> Out; Out.SetNumZeroed(MaxBytes);
		const int32 N = FAsdaLoginData::EncodeWireText(S, Out.GetData(), MaxBytes);
		Out.SetNum(N);
		return Out;
	}

	// Прежний энкодер (эталон регрессии) — НАСТОЯЩИЙ cp1251 (`EncodeCp1251` с 08.10 — гибрид).
	static TArray<uint8> Cp(const FString& S)
	{
		TArray<uint8> Out; Out.SetNumZeroed(400);
		const int32 N = FAsdaLoginData::EncodeCp1251Strict(S, Out.GetData(), 400);
		Out.SetNum(N);
		return Out;
	}

	// Декод массива (без терминатора — DecodeWireText остановится на длине).
	static FString Dec(const TArray<uint8>& B) { return FAsdaLoginData::DecodeWireText(B.GetData(), B.Num()); }

	// Строгий UTF-8?
	static bool StrictUtf8(const TArray<uint8>& B)
	{
		bool bMulti = false; int32 Valid = 0;
		return FAsdaLoginData::IsStrictUtf8(B.GetData(), B.Num(), /*bAllowTruncTail*/false, bMulti, Valid) && Valid == B.Num();
	}

	// Тестовые строки (арабский — экранами \u, чтобы исходник не зависел от кодировки редактора).
	static FString Ar()    { return FString(TEXT("مرحبا")); }                       // «مرحبا»
	static FString Ru()    { return FString(TEXT("Привет")); }                 // «Привет»
	static FString En()    { return FString(TEXT("Hello")); }
	static FString Mixed() { return Ru() + TEXT(" ") + Ar() + TEXT(" Hello 123"); }
	static FString NameRu(){ return FString(TEXT("Тест")); }                             // «Тест»

	// ═══ A. Кодек ═══
	void RunCodec()
	{
		// A1. Английский — ASCII.
		{
			const TArray<uint8> B = Wire(En());
			Expect(B == TArray<uint8>({ 0x48, 0x65, 0x6C, 0x6C, 0x6F }) && Dec(B) == En(), FString::Printf(TEXT("A1 «Hello» → %s"), *Hex(B)));
		}
		// A2. Русский — cp1251 байт-в-байт как прежде.
		{
			const TArray<uint8> B = Wire(Ru());
			Expect(B == TArray<uint8>({ 0xCF, 0xF0, 0xE8, 0xE2, 0xE5, 0xF2 }) && B == Cp(Ru()) && Dec(B) == Ru(),
				FString::Printf(TEXT("A2 «Привет» → %s (= прежний cp1251)"), *Hex(B)));
		}
		// A3. Арабский — маркер 0x98 + cp1256 (1 байт на букву).
		{
			const TArray<uint8> B = Wire(Ar());
			const TArray<uint8> Want({ 0x98, 0xE3, 0xD1, 0xCD, 0xC8, 0xC7 });
			Expect(B == Want && Dec(B) == Ar() && !B.Contains((uint8)'?'), FString::Printf(TEXT("A3 «مرحبا» → %s (маркер + cp1256, без '?')"), *Hex(B)));
			const FString ArEn = Ar() + TEXT(" Hello 123 \u061F");
			const TArray<uint8> B2 = Wire(ArEn);
			Expect(B2.Num() == ArEn.Len() + 1 && B2[0] == 0x98 && Dec(B2) == ArEn, FString::Printf(TEXT("A3b арабский+латиница+«؟» → маркер + cp1256, %d б"), B2.Num()));
		}
		// A4. Смешанная — UTF-8 целиком, круговой путь.
		{
			const TArray<uint8> B = Wire(Mixed());
			Expect(StrictUtf8(B) && Dec(B) == Mixed() && B.Num() == FAsdaLoginData::WireTextByteLen(Mixed()),
				FString::Printf(TEXT("A4 смешанная «%s» → %d б UTF-8, круговой путь"), *Mixed(), B.Num()));
		}
		// A5. Граница cp1256: 100 арабских букв в 15 байт → маркер + 14 букв. UTF-8: смешанная в 15 байт — посреди символа не режем.
		{
			FString Long; for (int32 i = 0; i < 100; ++i) { Long.AppendChar((TCHAR)0x0628); }
			const TArray<uint8> B = Wire(Long, 15);
			Expect(B.Num() == 15 && B[0] == 0x98 && Dec(B) == Long.Left(14), FString::Printf(TEXT("A5 граница cp1256 15 б → %d б, %d букв"), B.Num(), Dec(B).Len()));
			FString LongMix = Ru(); for (int32 i = 0; i < 50; ++i) { LongMix.AppendChar((TCHAR)0x0628); }
			const TArray<uint8> M = Wire(LongMix, 15);
			Expect(M.Num() == 14 && StrictUtf8(M) && Dec(M) == LongMix.Left(7), FString::Printf(TEXT("A5b граница UTF-8 15 б → %d б, %d символов (посреди не режет)"), M.Num(), Dec(M).Len()));
			const TArray<uint8> B1 = Wire(Ar(), 1);
			Expect(B1.Num() == 0, FString::Printf(TEXT("A5c буфер 1 б для арабского → %d б (одинокий маркер не пишется)"), B1.Num()));
		}
		// A6. Обрубок-хвост UTF-8 при приёме (сервер зажал длину посреди символа).
		{
			const FString RuAr = Ru() + Ar();
			TArray<uint8> B = Wire(RuAr); B.Pop();
			Expect(Dec(B) == RuAr.Left(RuAr.Len() - 1), FString::Printf(TEXT("A6 хвост UTF-8 обрезан на 1 б → «%s»"), *Dec(B)));
		}
		// A7. Длина на проводе и обрезка ввода по символам.
		{
			Expect(FAsdaLoginData::WireTextByteLen(Ru()) == 6 && FAsdaLoginData::WireTextByteLen(Ar()) == 6 && FAsdaLoginData::WireTextByteLen(En()) == 5
				&& FAsdaLoginData::WireTextByteLen(Ru() + Ar()) == 22,
				TEXT("A7 длина провода: Привет=6, مرحبا=6 (маркер+5), Hello=5, Привет+مرحبا=22 (UTF-8)"));
			Expect(FAsdaLoginData::TruncateToWireBytes(Ar(), 5) == Ar().Left(4) && FAsdaLoginData::TruncateToWireBytes(Ru(), 4) == Ru().Left(4),
				TEXT("A7b обрезка ввода: مرحبا/5 б → 4 буквы, Привет/4 б → 4 буквы"));
			FString Name18; for (int32 i = 0; i < 25; ++i) { Name18.AppendChar((TCHAR)0x0645); }
			Expect(FAsdaLoginData::TruncateToWireBytes(Name18, 19).Len() == 18 && FAsdaLoginData::TruncateToWireBytes(Ru() + Ru() + Ru() + Ru(), 19).Len() == 19,
				TEXT("A7c имя char[20] (19 б): арабское — 18 букв, русское — 19 (как раньше)"));
		}
		// A8. Регрессия cp1251: алфавит и типографика — те же байты, тот же декод.
		{
			FString All;
			for (TCHAR c = 0x0410; c <= 0x044F; ++c) { All.AppendChar(c); }
			All += TEXT("Ёё«»–—…№ abc 123 ,.!?");
			const TArray<uint8> B = Wire(All);
			Expect(B == Cp(All) && Dec(B) == FAsdaLoginData::DecodeCp1251Strict(B.GetData(), B.Num()) && Dec(B) == All,
				FString::Printf(TEXT("A8 cp1251-регрессия: %d символов, байты и декод = прежние"), All.Len()));
		}
		// A9. Ни одна пара cp1251-байт 0x80..0xFF не принимается за UTF-8 там, где это изменило бы русскую строку из двух букв.
		{
			int32 Bad = 0;
			for (int32 a = 0xC0; a <= 0xFF; ++a)
			{
				for (int32 b = 0xC0; b <= 0xFF; ++b)
				{
					const uint8 P[2] = { (uint8)a, (uint8)b };
					if (FAsdaLoginData::DecodeWireText(P, 2) != FAsdaLoginData::DecodeCp1251Strict(P, 2)) { ++Bad; }
				}
			}
			Expect(Bad == 0, FString::Printf(TEXT("A9 все 4096 пар русских букв (C0..FF)² декодируются как cp1251: расхождений %d"), Bad));
		}
		// A9b. Энкодер cp1251 никогда не выдаёт маркер 0x98 — ни для одного символа BMP (маркер однозначен).
		{
			int32 Hits = 0;
			for (int32 c = 1; c < 0x10000; ++c)
			{
				if (c >= 0xD800 && c <= 0xDFFF) { continue; }
				uint8 Bt = 0; FString One; One.AppendChar((TCHAR)c);
				if (FAsdaLoginData::EncodeCp1251Strict(One, &Bt, 1) == 1 && Bt == 0x98) { ++Hits; }
			}
			Expect(Hits == 0, FString::Printf(TEXT("A9b cp1251-энкодер выдаёт 0x98: %d символов из BMP"), Hits));
		}
		// A9c. Ревизия С1/М2 (08.10.2026): русские строки, байты cp1251 которых выглядят как UTF-8 («Я…» DF 85, «Сё» D1 B8,
		//   «Х«» D5 AB, «Сё Я» D1 B8 20 DF с обрубком-хвостом), уходят веткой UTF-8 и принимаются без искажений; обычные — cp1251.
		{
			const TCHAR* Risky[] = { TEXT("Я…"), TEXT("И…"), TEXT("Сё"), TEXT("Лё"), TEXT("Тё"), TEXT("Х«"), TEXT("ok Я…"), TEXT("Сё Я") };
			for (const TCHAR* R : Risky)
			{
				const FString S(R);
				const TArray<uint8> B = Wire(S);
				Expect(Dec(B) == S && StrictUtf8(B) && B.Num() == FAsdaLoginData::WireTextByteLen(S),
					FString::Printf(TEXT("A9c «%s» → %s (UTF-8, приём = исходник, длина %d = WireTextByteLen)"), R, *Hex(B), B.Num()));
			}
			const TCHAR* Plain[] = { TEXT("Привет"), TEXT("Всё…"), TEXT("Ну и…"), TEXT("ок Я…"), TEXT("№1"), TEXT("Я") };
			for (const TCHAR* P : Plain)
			{
				const FString S(P);
				const TArray<uint8> B = Wire(S);
				Expect(B == Cp(S) && Dec(B) == S, FString::Printf(TEXT("A9c контроль «%s» → %s (cp1251 байт-в-байт, как прежде)"), P, *Hex(B)));
			}
			// Голова, обрезанная полем: «Сё Яблоко» в 4 байта — не «Сё Я» в cp1251 (приём потерял бы «Я»), а целые символы UTF-8.
			const FString Long(TEXT("Сё Яблоко"));
			const TArray<uint8> B4 = Wire(Long, 4);
			Expect(Long.StartsWith(Dec(B4)) && (B4.Num() == 0 || !Dec(B4).IsEmpty()) && !Dec(B4).Contains(TEXT("�")),
				FString::Printf(TEXT("A9c голова «Сё Яблоко»/4 б → %s → «%s» (префикс исходника, без порчи)"), *Hex(B4), *Dec(B4)));
			// Перебор (закрывает М2): все пары и тройки из алфавита cp1251 + типографики + пробела/латиницы/цифры — круговой путь точен.
			FString Abc;
			for (TCHAR c = 0x0410; c <= 0x044F; ++c) { Abc.AppendChar(c); }
			Abc += TEXT("Ёё«»–—…№ a1");
			int32 Bad = 0, Total = 0;
			FString FirstBad;
			for (int32 i = 0; i < Abc.Len(); ++i)
			{
				for (int32 j = 0; j < Abc.Len(); ++j)
				{
					FString S2; S2.AppendChar(Abc[i]); S2.AppendChar(Abc[j]);
					++Total; if (Dec(Wire(S2)) != S2) { ++Bad; if (FirstBad.IsEmpty()) { FirstBad = S2; } }
					for (int32 k = 0; k < Abc.Len(); ++k)
					{
						FString S3 = S2; S3.AppendChar(Abc[k]);
						++Total; if (Dec(Wire(S3)) != S3) { ++Bad; if (FirstBad.IsEmpty()) { FirstBad = S3; } }
					}
				}
			}
			Expect(Bad == 0, FString::Printf(TEXT("A9c перебор пар и троек (%d строк): круговой путь искажён у %d%s"), Total, Bad,
				FirstBad.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", первая «%s»"), *FirstBad)));
		}
		// A11. Режим 2 (без маркера): арабский → UTF-8; приём маркера по-прежнему понимается.
		{
			IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Text.WireCodec"));
			const int32 Was = Cv ? Cv->GetInt() : 1;
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Text.WireCodec"), 2); }
			const TArray<uint8> B = Wire(Ar());
			const TArray<uint8> Mk({ 0x98, 0xE3, 0xD1, 0xCD, 0xC8, 0xC7 });
			const bool bOk2 = B == TArray<uint8>({ 0xD9, 0x85, 0xD8, 0xB1, 0xD8, 0xAD, 0xD8, 0xA8, 0xD8, 0xA7 }) && Dec(B) == Ar() && Dec(Mk) == Ar();
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Text.WireCodec"), Was); }
			Expect(Cv && bOk2, FString::Printf(TEXT("A11 режим 2: «مرحبا» → %s (UTF-8), приём маркера — «مرحبا»"), *Hex(B)));
		}
		// A10. Откат `asda.Text.WireCodec 0`.
		{
			IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Text.WireCodec"));
			const int32 Was = Cv ? Cv->GetInt() : 1;
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Text.WireCodec"), 0); }
			const TArray<uint8> B = Wire(Ar());
			const TArray<uint8> U({ 0xD9, 0x85, 0xD8, 0xB1 });
			const bool bRoll = (B == TArray<uint8>({ '?', '?', '?', '?', '?' })) && Dec(U) == FAsdaLoginData::DecodeCp1251Strict(U.GetData(), U.Num());
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Text.WireCodec"), Was); }
			Expect(Cv && bRoll, FString::Printf(TEXT("A10 откат WireCodec 0: арабский → %s (прежнее поведение), приём — cp1251"), *Hex(B)));
		}
	}

	// ═══ E. Имена и поля БД: публичные EncodeCp1251/DecodeCp1251 = гибрид, правило имени, лимиты с бесплатным маркером ═══
	static FString NameAr() { return FString(TEXT("\u0633\u0627\u0645\u064A")); }   // «سامي»
	static constexpr int32 kPeerAr = 4301;   // фейковый сосед с арабским именем (плашка над головой)
	static constexpr int32 kPeerRu = 4302;   // фейковый сосед с русским именем
	void RunNames()
	{
		// E1. Публичный EncodeCp1251 = гибрид: русское — прежние байты, арабское — маркер+cp1256; круговой путь.
		{
			uint8 A[20] = {}, R[20] = {};
			const int32 NA = FAsdaLoginData::EncodeCp1251(NameAr(), A, 19);
			const int32 NR = FAsdaLoginData::EncodeCp1251(NameRu(), R, 19);
			Expect(NA == 5 && A[0] == 0x98 && A[1] == 0xD3 && FAsdaLoginData::DecodeCp1251(A, 20) == NameAr()
				&& NR == 4 && TArray<uint8>(R, 4) == Cp(NameRu()) && FAsdaLoginData::DecodeCp1251(R, 20) == NameRu(),
				TEXT("E1 имя «سامي» → 98 D3 C7 E3 ED (маркер+cp1256), «Тест» → прежний cp1251; круговой путь обоих"));
		}
		// E2. Правило имени — локали оригинала: кириллица (case 7) ИЛИ арабица (case 8), смешение запрещено.
		{
			struct FN { const TCHAR* S; bool bOk; };
			const FN Cases[] = {
				{ TEXT("Hero123"), true }, { TEXT("\u0421\u0430\u0448\u0430"), true },            // Саша
				{ TEXT("\u0401\u0436Ik"), true },                                                     // Ёж + латиница (case 7 пускает)
				{ TEXT("\u0633\u0627\u0645\u064A"), true }, { TEXT("\u0633\u0627\u0645\u064A77"), true },   // سامي, سامي77
				{ TEXT("\u0633\u0627\u0645\u064A\u0421"), false },                                 // смешение арабица+кириллица
				{ TEXT("\u0633\u00D7"), false },                                                      // «×» (cp1256 0xD7) — запрет case 8
				{ TEXT("a b"), false }, { TEXT("a_b"), false }, { TEXT("\u4E2D"), false },             // пробел, '_', иероглиф
			};
			// Ключ правила имени (по умолчанию 0 — решение владельца 08.10: только латиница); E2 проверяет режим 1.
			IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Text.NameLocale"));
			const int32 Was = Cv ? Cv->GetInt() : 0;
			Expect(Was == 0, TEXT("E2a asda.Text.NameLocale по умолчанию 0 — имена только латиницей (решение владельца 08.10)"));
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Text.NameLocale"), 1); }
			int32 Bad = 0; FString Why;
			for (const FN& C : Cases)
			{
				if (FAsdaLoginData::IsValidCharNameByLocale(C.S) != C.bOk) { ++Bad; Why += FString::Printf(TEXT(" «%s»"), C.S); }
			}
			Expect(Bad == 0, FString::Printf(TEXT("E2 правило имени по локалям оригинала (режим 1): расхождений %d%s"), Bad, *Why));
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Text.NameLocale"), 0); }
			const bool b0 = FAsdaLoginData::IsValidCharNameByLocale(TEXT("Hero123")) && !FAsdaLoginData::IsValidCharNameByLocale(NameAr())
				&& !FAsdaLoginData::IsValidCharNameByLocale(NameRu());
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Text.NameLocale"), Was); }
			Expect(Cv && b0, TEXT("E2b asda.Text.NameLocale 0 — прежнее правило эталона порта (только A-Z a-z 0-9)"));
		}
		// E3. Лимит с бесплатным маркером: кличка 14 букв/поле 16, лавка 50/50 (маркер съедает букву), «о себе» 128/128 (с 08.10.2026, пакет 2).
		{
			FString Ar20; for (int32 i = 0; i < 60; ++i) { Ar20.AppendChar((TCHAR)0x0645); }
			FString Ru20; for (int32 i = 0; i < 60; ++i) { Ru20.AppendChar((TCHAR)0x0416); }
			const int32 Pet = FAsdaLoginData::TruncateToWireLimit(Ar20, 14, 16).Len();
			const int32 PetRu = FAsdaLoginData::TruncateToWireLimit(Ru20, 14, 16).Len();
			const int32 Shop = FAsdaLoginData::TruncateToWireLimit(Ar20, 50, 50).Len();
			const int32 Mix = FAsdaLoginData::TruncateToWireLimit(Ru() + Ar20, 14, 16).Len();
			Expect(Pet == 14 && PetRu == 14 && Shop == 49 && Mix == 7,
				FString::Printf(TEXT("E3 лимиты: кличка арабская %d (=14), русская %d (=14); лавка арабская %d (=49, поле 50 без запаса); смешанная кличка %d симв. (UTF-8, 14 б)"),
					Pet, PetRu, Shop, Mix));
			// ★ 08.10.2026 (пакет 3, МЕЛК-4 ревизии пакета 2): «о себе» 128/128 — русский 128 букв (без обязательного нуля),
			//   арабский 127 букв + маркер (поле 128 без запаса).
			FString Ru200, Ar200;   // 200 русских «Ж» / 200 арабских «م» — длиннее поля
			for (int32 i = 0; i < 200; ++i) { Ru200.AppendChar((TCHAR)0x0416); Ar200.AppendChar((TCHAR)0x0645); }
			const int32 IntroRu = FAsdaLoginData::TruncateToWireLimit(Ru200, 128, 128).Len();
			const int32 IntroAr = FAsdaLoginData::TruncateToWireLimit(Ar200, 128, 128).Len();
			Expect(IntroRu == 128 && IntroAr == 127,
				FString::Printf(TEXT("E3b «о себе» 128/128: русский %d (=128), арабский %d (=127, маркер + 127)"), IntroRu, IntroAr));
		}
	}

	// ═══ B. Боевые отправители под перехватом ═══
	struct FCap { uint16 Op = 0; TArray<uint8> P; };   // перехваченный пакет

	void RunSenders(UAsdaNetSubsystem* Net)
	{
		TArray<FCap> Caps;
		const uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [&Caps](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			FCap C; C.Op = Op; C.P.Append(P, Len); Caps.Add(MoveTemp(C));
		}, /*OnlyOpcode*/0, TEXT("asda.Text.SelfTestArabic"));
		auto Last = [&Caps](uint16 Op) -> const FCap* { for (int32 i = Caps.Num() - 1; i >= 0; --i) { if (Caps[i].Op == Op) { return &Caps[i]; } } return nullptr; };
		auto Tail = [](const FCap* C, int32 Off) { TArray<uint8> T; if (C && C->P.Num() >= Off) { T.Append(C->P.GetData() + Off, C->P.Num() - Off); } return T; };

		for (const FString& S : { En(), Ru(), Ar(), Mixed() })
		{
			const TArray<uint8> Want = Wire(S);
			Caps.Reset();
			Net->SendChatShout5084(S);
			Expect(Tail(Last(5084), 0) == Want, FString::Printf(TEXT("B 5084 «%s» тело %s"), *S, *Hex(Tail(Last(5084), 0))));
			Net->SendChatNormal5086(S, NameRu(), 0);
			const FCap* P86 = Last(5086);
			Expect(Tail(P86, 27) == Want && P86 && FAsdaLoginData::DecodeCp1251(P86->P.GetData() + 3, 24) == NameRu(),
				FString::Printf(TEXT("B 5086 «%s» тело+27 = ожидаемое, имя+3 cp1251 «Тест»"), *S));
			Net->SendChatChannel6560(1, S);
			Expect(Tail(Last(6560), 1) == Want, FString::Printf(TEXT("B 6560 «%s» тело+1 = ожидаемое"), *S));
			Net->SendChatWhisper5088(0, NameRu(), S);
			const FCap* P88 = Last(5088);
			Expect(Tail(P88, 21) == Want && P88 && FAsdaLoginData::DecodeCp1251(P88->P.GetData() + 1, 20) == NameRu(),
				FString::Printf(TEXT("B 5088 «%s» тело+21 = ожидаемое, адресат cp1251"), *S));
			Net->SendChatParty5105(7, S);
			Expect(Tail(Last(5105), 2) == Want, FString::Printf(TEXT("B 5105 «%s» тело+2 = ожидаемое"), *S));
			Net->SendChatClan4120(1, NameRu(), S);
			const TArray<uint8> Want4120 = Wire(Net->GetClanChatSoulPrefix() + S);
			Expect(Tail(Last(4120), 28) == Want4120, FString::Printf(TEXT("B 4120 «%s» тело+28 = ожидаемое"), *S));
		}
		// Арабские ИМЕНА в теле: своё имя 5086 (+3) и адресат шёпота 5088 (+1) — маркер + cp1256.
		{
			Caps.Reset();
			Net->SendChatNormal5086(Ru(), NameAr(), 0);
			Net->SendChatWhisper5088(0, NameAr(), Ru());
			const FCap* P86 = Last(5086); const FCap* P88 = Last(5088);
			Expect(P86 && P86->P[3] == 0x98 && FAsdaLoginData::DecodeCp1251(P86->P.GetData() + 3, 24) == NameAr()
				&& P88 && P88->P[1] == 0x98 && FAsdaLoginData::DecodeCp1251(P88->P.GetData() + 1, 20) == NameAr(),
				TEXT("B имена: своё «سامي» в 5086+3 и адресат шёпота в 5088+1 — маркер + cp1256"));
		}
		// Апостроф шёпота меняется на обратную кавычку ДО кодирования (анти-SQLi эталона), арабский при этом цел.
		{
			Caps.Reset();
			const FString S = FString(TEXT("it's ")) + Ar();
			Net->SendChatWhisper5088(0, NameRu(), S);
			const TArray<uint8> T = Tail(Last(5088), 21);
			Expect(!T.Contains((uint8)0x27) && T.Contains((uint8)0x60) && FAsdaLoginData::DecodeWireText(T.GetData(), T.Num()) == FString(TEXT("it`s ")) + Ar(),
				FString::Printf(TEXT("B 5088 апостроф → ` до кодирования: %s"), *Hex(T)));
		}
		// Длинная арабская строка: буфер отправителя 5086 = 172 б → маркер + 171 буква. Длинная смешанная — строгий UTF-8, посреди не режет.
		{
			Caps.Reset();
			FString Long; for (int32 i = 0; i < 200; ++i) { Long.AppendChar((TCHAR)0x0645); }
			Net->SendChatNormal5086(Long, NameRu(), 0);
			const TArray<uint8> T = Tail(Last(5086), 27);
			Expect(T.Num() == 172 && T[0] == 0x98, FString::Printf(TEXT("B 5086 длинная арабская: тело %d б (маркер + 171 буква)"), T.Num()));
			FString LongMix = Ru(); for (int32 i = 0; i < 200; ++i) { LongMix.AppendChar((TCHAR)0x0645); }
			Net->SendChatNormal5086(LongMix, NameRu(), 0);
			const TArray<uint8> T2 = Tail(Last(5086), 27);
			Expect(T2.Num() <= 172 && T2.Num() >= 171 && StrictUtf8(T2), FString::Printf(TEXT("B 5086 длинная смешанная: тело %d б, строгий UTF-8"), T2.Num()));
		}
		// ★ 08.10.2026 (пакет 3, МЕЛК-4): отправитель 6080 «о себе» — поле 128 б без обязательного нуля, как эталон
		//   (`netSecure_sendTextInput@0xE74220`): 128 русских букв → 128 б cp1251, 128-й байт ненулевой; арабский — маркер + 127.
		{
			FString Ru200, Ar200;   // длиннее поля: обрезку делает вызывающий (`SendIntroText` → `TruncateToWireLimit(…,128,128)`)
			for (int32 i = 0; i < 200; ++i) { Ru200.AppendChar((TCHAR)0x0416); Ar200.AppendChar((TCHAR)0x0645); }
			const FString Ru128 = FAsdaLoginData::TruncateToWireLimit(Ru200, 128, 128);
			const FString Ar127 = FAsdaLoginData::TruncateToWireLimit(Ar200, 128, 128);
			Caps.Reset();
			Net->SendSoulmateIntroduction6080(Ru128);
			const TArray<uint8> TR = Tail(Last(6080), 0);
			Expect(TR.Num() == 128 && TR[127] != 0 && FAsdaLoginData::DecodeWireText(TR.GetData(), TR.Num()) == Ru128,
				FString::Printf(TEXT("B 6080 «о себе» 128 русских: поле %d б (=128), 128-й байт 0x%02X (≠0), круговой путь"),
					TR.Num(), TR.Num() == 128 ? TR[127] : 0));
			Caps.Reset();
			Net->SendSoulmateIntroduction6080(Ar127);
			const TArray<uint8> TA = Tail(Last(6080), 0);
			Expect(TA.Num() == 128 && TA[0] == 0x98 && TA[127] != 0 && FAsdaLoginData::DecodeWireText(TA.GetData(), TA.Num()) == Ar127,
				FString::Printf(TEXT("B 6080 «о себе» арабский: поле %d б, маркер + 127 букв, 128-й байт ненулевой"), TA.Num()));
		}
		AsdaSelfTestKit::DisarmCapture(Net, Gen);
	}

	// ═══ C. Приём через штатный HandlePacket ═══
	void RunReceive(UAsdaNetSubsystem* Net)
	{
		TArray<UAsdaNetSubsystem::FAsdaChatLineIn> Lines;
		const FDelegateHandle H = Net->OnChatLine.AddLambda([&Lines](const UAsdaNetSubsystem::FAsdaChatLineIn& L) { Lines.Add(L); });

		// Собрать тело: нули длины Head, имя cp1251 по смещению, текст-байты по смещению.
		auto Body = [](int32 TextOff, int32 NameOff, const TArray<uint8>& Txt, TFunction<void(TArray<uint8>&)> Fix)
		{
			TArray<uint8> B; B.SetNumZeroed(TextOff);
			uint8 Nm[20] = {}; FAsdaLoginData::EncodeCp1251(NameRu(), Nm, 19);
			FMemory::Memcpy(B.GetData() + NameOff, Nm, 20);
			if (Fix) { Fix(B); }
			B.Append(Txt); B.Add(0);
			return B;
		};
		struct FCase { int32 Op; int32 TextOff; int32 NameOff; TFunction<void(TArray<uint8>&)> Fix; };
		const TArray<FCase> Cases = {
			{ 5085, 26, 2,  nullptr },
			{ 5087, 27, 3,  nullptr },
			{ 5089, 29, 8,  [Net](TArray<uint8>& B) { B[0] = 0; B[1] = 1; AsdaBytes::WrI16(B.GetData() + 6, Net->SelfUserNum); } },   // входящий шёпот: Flag 0, MsgType 1   // ★ 05.11.2026: WORD@6 = userNum получателя (свой) — иначе `asda.Chat.WhisperUserNumCheck` отбросит как «шёпот не мне»
			{ 4121, 28, 8,  nullptr },
			{ 5106, 25, 2,  nullptr },
			{ 6561, 32, 12, [](TArray<uint8>& B) { B[0] = 1; B[1] = 1; } },   // kind 1 (шёпот/канал), канал 1
			{ 6719, 21, 1,  [](TArray<uint8>& B) { B[0] = 1; } },             // в сети
		};
		for (const FCase& K : Cases)
		{
			for (const FString& S : { Ar(), Mixed(), Ru(), En() })
			{
				Lines.Reset();
				// Русский/английский — старые байты cp1251 (как шлют прежние клиенты и как лежит в чужих логах); прочее — UTF-8.
				const TArray<uint8> Txt = FAsdaLoginData::IsCp1251Representable(S) ? Cp(S) : Wire(S);
				Net->DebugFeedPacket(K.Op, Body(K.TextOff, K.NameOff, Txt, K.Fix));
				const UAsdaNetSubsystem::FAsdaChatLineIn* L = Lines.Num() ? &Lines.Last() : nullptr;
				const bool bText = L && L->Text.Contains(S) && L->Text.Contains(NameRu()) && !L->Text.Contains(TEXT("?"));
				const bool bBalloon = !L || L->BalloonVariant < 0 || L->BalloonText.Contains(S);
				Expect(bText && bBalloon, FString::Printf(TEXT("C %d приём «%s» → строка «%s»%s"),
					K.Op, *S, L ? *L->Text : TEXT("<нет строки>"), (L && L->BalloonVariant >= 0) ? *FString::Printf(TEXT(", облачко «%s»"), *L->BalloonText) : TEXT("")));
			}
		}
		// Арабское имя отправителя (из БД — маркер+cp1256) на приёме 5085.
		{
			Lines.Reset();
			TArray<uint8> B; B.SetNumZeroed(26);
			uint8 Nm[20] = {}; FAsdaLoginData::EncodeCp1251(NameAr(), Nm, 19);
			FMemory::Memcpy(B.GetData() + 2, Nm, 20);
			B.Append(Wire(Ar())); B.Add(0);
			Net->DebugFeedPacket(5085, B);
			const UAsdaNetSubsystem::FAsdaChatLineIn* L = Lines.Num() ? &Lines.Last() : nullptr;
			Expect(L && L->SenderName == NameAr() && L->Text.Contains(NameAr()) && L->Text.Contains(Ar()),
				FString::Printf(TEXT("C 5085 имя отправителя «سامي» (маркер+cp1256) → «%s»"), L ? *L->Text : TEXT("<нет строки>")));
		}
		Net->OnChatLine.Remove(H);
	}

	// Итог и выход.
	void Finish(UWorld* W, bool bQuit)
	{
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи прогона (Text.WireCodec, Text.NameLocale) — к исходному значению
		//   И приоритету SetBy (прежде блоки A10/A11/E2 возвращали прежнее значение, но с SetByConsole).
		AsdaCVarSave::RestoreAll(TEXT("asda.Text.SelfTestArabic"));
		UE_LOG(LogAsdaArabicTest, Log, TEXT("САМОТЕСТ-AR ИТОГ: ok=%d fail=%d"), Ok, Fail);
		AsdaSelfTestKit::Quit(W, bQuit);
	}

	// Текст поля ввода под фокусом клавиатуры (или пометка, если фокус не на поле).
	static FString FocusedEditText()
	{
		if (!FSlateApplication::IsInitialized()) { return FString(); }
		const TSharedPtr<SWidget> Wd = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (!Wd.IsValid() || Wd->GetType() != FName(TEXT("SEditableText"))) { return FString(TEXT("<фокус не на поле>")); }
		return StaticCastSharedPtr<SEditableText>(Wd)->GetText().ToString();
	}

	// Набрать строку настоящими символьными событиями Slate (как WM_CHAR с клавиатуры).
	static void TypeChars(const FString& S)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		for (int32 i = 0; i < S.Len(); ++i)
		{
			FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(S[i], FModifierKeysState(), 0, false));
		}
	}

	// Ctrl+A настоящим нажатием (выделить всё в поле под фокусом).
	static void SelectAll()
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		const FModifierKeysState Ctrl(false, false, true, false, false, false, false, false, false);
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::A, Ctrl, 0, false, 0, 0));
		FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::A, Ctrl, 0, false, 0, 0));
	}

	// Backspace настоящим нажатием.
	static void Backspace()
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::BackSpace, FModifierKeysState(), 0, false, 0, 0));
		FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::BackSpace, FModifierKeysState(), 0, false, 0, 0));
	}

	// D. Путь игрока — по таймерам мира (кадры идут, геометрия и фокус успевают).
	static void RunPlayerPath(TSharedRef<FAsdaArabicTextSelfTest> T, UWorld* W, UAsdaNetSubsystem* Net, bool bQuit)
	{
		UGameInstance* GI = W->GetGameInstance();
		UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
		UAsdaChatWindowSubsystem* Chat = GI->GetSubsystem<UAsdaChatWindowSubsystem>();
		UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>();
		if (!WS || !Chat || !Tip) { T->Expect(false, TEXT("D нет подсистем мира/чата/подсказки")); T->Finish(W, bQuit); return; }
		constexpr int32 kMe = 4242;   // свой userIdx на время прогона
		const int32 UserNumWas = Net->SelfUserNum;
		const bool bKnownWas = Net->bSelfUserNumKnown;
		if (!::IsValid(WS->LocalPlayerActor.Get()) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			WS->DebugSpawnSelfAt(256, 256, NameRu());
		}
		Net->SelfUserNum = kMe; Net->bSelfUserNumKnown = true;
		// Плашки над головой: два соседа рядом — с арабским и русским именем (имя на плашке — штатный путь соседа).
		{
			FIntPoint MyTile(256, 256);
			if (const AActor* Me = WS->LocalPlayerActor.Get()) { MyTile = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(Me->GetActorLocation())); }
			WS->DebugSpawnFakePeer(kPeerAr, NameAr(), AsdaCoord::TileCenterGame(MyTile.X + 2, MyTile.Y + 1));
			WS->DebugSpawnFakePeer(kPeerRu, NameRu() + TEXT("Ru"), AsdaCoord::TileCenterGame(MyTile.X - 2, MyTile.Y + 1));
		}
		// Строки ленты и облачко над своей головой (5085 от СВОЕГО userIdx).
		auto Feed = [Net](int32 Op, int32 TextOff, int32 NameOff, const TArray<uint8>& Txt, TFunction<void(TArray<uint8>&)> Fix)
		{
			TArray<uint8> B; B.SetNumZeroed(TextOff);
			uint8 Nm[20] = {}; FAsdaLoginData::EncodeCp1251(NameRu(), Nm, 19);
			FMemory::Memcpy(B.GetData() + NameOff, Nm, 20);
			if (Fix) { Fix(B); }
			B.Append(Txt); B.Add(0);
			Net->DebugFeedPacket(Op, B);
		};
		Feed(5087, 27, 3, Wire(Mixed()), nullptr);
		Feed(5089, 29, 8, Wire(FString(TEXT("كيف حالك؟"))), [](TArray<uint8>& B) { B[0] = 0; B[1] = 1; AsdaBytes::WrI16(B.GetData() + 6, kMe); });   // «كيف حالك؟»   // ★ 05.11.2026: WORD@6 = userNum получателя (свой) — иначе `asda.Chat.WhisperUserNumCheck` отбросит как «шёпот не мне»
		Feed(5106, 25, 2, Cp(Ru() + TEXT(", Hello!")), nullptr);
		Feed(5085, 26, 2, Wire(Ar() + TEXT(" ") + Ru()), [](TArray<uint8>& B) { B[0] = (uint8)(kMe & 0xFF); B[1] = (uint8)(kMe >> 8); });
		Chat->OpenInput(0);
		// ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте — общий флаг двух
		//   отложенных шагов (клик и набор); при отказе шаг набора только убирает подмену userNum (уборка H3 не взводится).
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Text.SelfTestArabic"), bQuit);
		FTimerHandle H0;
		W->GetTimerManager().SetTimer(H0, FTimerDelegate::CreateLambda([T, W, Chat, Gate]()
		{
			if (!Gate->Pass(W)) { return; }
			// Как игрок: открыть строку ввода и КЛИКНУТЬ мышью по живому прямоугольнику поля 2609 (`CHAT_NEW_Edit2`).
			Chat->OpenInput(0);
			UEditableText* Edit = nullptr;
			for (TObjectIterator<UEditableText> It; It; ++It)
			{
				if (It->GetFName() == FName(TEXT("CHAT_NEW_Edit2")) && It->GetCachedWidget().IsValid() && It->GetWorld() == W) { Edit = *It; break; }
			}
			FVector2D A = FVector2D::ZeroVector, B = FVector2D::ZeroVector;   // ★ R5-fix (C4701): при несработавшем WidgetAbsRect в строку итога шли неинициализированные числа
			UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
			const bool bR = Edit && WM && AsdaUi::WidgetAbsRect(Edit, A, B);
			if (bR) { WM->SimulateClickAt((A + B) * 0.5, /*HoldFrames*/ 3); }
			T->Expect(bR, FString::Printf(TEXT("D0 клик по полю ввода чата 2609 (%.0f,%.0f)-(%.0f,%.0f)"), A.X, A.Y, B.X, B.Y));
		}), 1.0f, false);
		FTimerHandle H1;
		W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([T, W, Net, Chat, Tip, UserNumWas, bKnownWas, bQuit, Gate]()
		{
			if (!Gate->Pass(W))   // отказ на отложенном старте: только уборка подмены (H3 не взводится)
			{
				Tip->HideTooltip();
				Chat->CloseInput();
				Net->SelfUserNum = UserNumWas; Net->bSelfUserNumKnown = bKnownWas;
				AsdaCVarSave::RestoreAll(TEXT("asda.Text.SelfTestArabic"));   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): и на этом выходе
				return;
			}
			// D1. Смешанная строка настоящими нажатиями.
			TypeChars(Mixed());
			const FString Got = FocusedEditText();
			T->Expect(Got == Mixed(), FString::Printf(TEXT("D1 ввод с клавиатуры (Slate) «%s» → в поле «%s»"), *Mixed(), *Got));
			// D2. Переполнение арабским: лимит поля в БАЙТАХ провода (умолчание 136 ⇒ 68 арабских букв).
			SelectAll(); Backspace();
			FString Many; for (int32 i = 0; i < 150; ++i) { Many.AppendChar((TCHAR)0x0645); }
			TypeChars(Many);
			const FString Over = FocusedEditText();
			const int32 Lim = Chat->GetActiveInputMaxChars();
			const int32 OverBytes = FAsdaLoginData::WireTextByteLen(Over);
			T->Expect(OverBytes <= Lim && OverBytes > Lim - 2,
				FString::Printf(TEXT("D2 набор 150 арабских букв при лимите %d б → в поле %d букв = %d б провода"), Lim, Over.Len(), OverBytes));
			// Русский при том же лимите — прежнее число символов (cp1251: символ = байт).
			SelectAll(); Backspace();
			FString ManyRu; for (int32 i = 0; i < 150; ++i) { ManyRu.AppendChar((TCHAR)0x0416); }
			TypeChars(ManyRu);
			const FString OverRu = FocusedEditText();
			T->Expect(OverRu.Len() == Lim, FString::Printf(TEXT("D2b набор 150 русских букв при лимите %d б → в поле %d букв (как до правки)"), Lim, OverRu.Len()));
			// D2c. Ревизия С3 (08.10.2026): клан-чат (режим 6) при «Друг» — префикс loc 6937 + арабский текст; лимит набора —
			//   префикс+текст ≤ 160 б провода (P14), а не символы. Проверка общей точкой лимита ввода.
			//   ★ 09.10.2026 (решение владельца): префикс — ТЕКУЩЕГО ЯЗЫКА сессии (RU «(Друг)» — кириллица, ветка UTF-8, 2 б на
			//   арабскую букву; EN «(soul)» — латиница, ветка cp1256 с маркером, 1 б на букву). Прежние фиксированные 135 букв
			//   в EN давали 142 б — до предела не доходило, и тест падал при верном лимите. Теперь букв заведомо больше
			//   предела в ЛЮБОЙ ветке кодека (160 + 20), а ожидание — «длиннейшая голова»: она помещается, а ещё одна буква
			//   уже не помещается (по P14 или по общему лимиту поля — что наступит раньше).
			{
				const uint8 SoulWas = Net->SelfSoulSilhouette;
				const int32 StateWas = Chat->GetInputState();
				Net->SelfSoulSilhouette = 1;
				Chat->SetInputState(6);
				const FString Prefix = Net->GetClanChatSoulPrefix();
				const UAsdaTextSubsystem* Txt = W->GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>();
				const FString Loc6937 = Txt ? Txt->GetTextById(6937) : FString();   // префикс языка сессии (та же точка, что у чата)
				const int32 kP14 = 160;                                             // предел текста 4120 на живом сервере (P14), байт
				const int32 NAr = kP14 + 20;                                        // больше предела в любой ветке кодека
				FString ArMany; for (int32 i = 0; i < NAr; ++i) { ArMany.AppendChar((TCHAR)0x0645); }
				const FString Cut = Chat->ClampToActiveInputLimit(ArMany);
				const FString Next = Cut + TEXT("م");
				const int32 FullB = FAsdaLoginData::WireTextByteLen(Prefix + Cut);
				const int32 OverB = FAsdaLoginData::WireTextByteLen(Prefix + Next);
				const int32 FieldLim = Chat->GetActiveInputMaxChars();
				const bool bNextOver = OverB > kP14 || FAsdaLoginData::WireTextByteLen(Next) > FieldLim;
				T->Expect(!Prefix.IsEmpty() && Prefix == Loc6937 && Cut.Len() > 0 && Cut.Len() < NAr && FullB <= kP14
						&& Chat->IsWithinActiveInputLimit(Cut) && !Chat->IsWithinActiveInputLimit(Next) && bNextOver,
					FString::Printf(TEXT("D2c клан-чат «Друг» (язык «%s»): префикс «%s» (loc 6937 «%s»), %d арабских букв → в поле %d букв, префикс+текст = %d б (≤ %d, ещё буква — %d б, лимит поля %d б)"),
						FAsdaLang::Code(FAsdaLang::Current()), *Prefix, *Loc6937, NAr, Cut.Len(), FullB, kP14, OverB, FieldLim));
				Chat->SetInputState(StateWas);
				Net->SelfSoulSilhouette = SoulWas;
			}
			// Вернуть в поле смешанную строку — для кадра.
			SelectAll(); Backspace();
			TypeChars(Mixed());
			// D3. Общая плашка-подсказка с арабским и смешанным текстом.
			Tip->ShowTextTip(EAsdaTipOwner::SysMenu, Ar(), Mixed() + TEXT("\n") + Ru() + TEXT(" / Hello"), FAsdaTipMetrics());
			T->Expect(Tip->GetTipOwner() == EAsdaTipOwner::SysMenu, TEXT("D3 подсказка с арабским заголовком показана"));
			// D4. Как игрок: клик мышью по соседу «سامي» (голова, спроецированная на экран) — цель и плашка с арабским именем.
			{
				UAsdaWorldSubsystem* WS2 = W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
				UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
				// поиск соседа ПО ИМЕНИ (общая точка `FindPlayerActorByName` — тот же путь, что у облачков/меню игрока): заодно проверка,
				//   что арабское имя находится так же, как русское
				AActor* Peer = WS2 ? WS2->FindPlayerActorByName(NameAr()) : nullptr;
				T->Expect(Peer != nullptr, FString::Printf(TEXT("D4a сосед найден по арабскому имени «%s» (FindPlayerActorByName)"), *NameAr()));
				APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
				FVector2D Px(0.f, 0.f);
				bool bProj = Peer && PC && UGameplayStatics::ProjectWorldToScreen(PC, Peer->GetActorLocation() + FVector(0.f, 0.f, 60.f), Px);
				FVector2D Abs = Px;
				if (bProj && GEngine && GEngine->GameViewport)
				{
					if (const TSharedPtr<SViewport> Vp = GEngine->GameViewport->GetGameViewportWidget())
					{
						const FGeometry& G = Vp->GetCachedGeometry();
						Abs = G.LocalToAbsolute(Px / FMath::Max(0.01f, G.Scale));
					}
				}
				if (bProj && WM) { WM->SimulateClickAt(Abs, /*HoldFrames*/ 3); }
				T->Expect(bProj, FString::Printf(TEXT("D4 клик по соседу «%s» на экране (%.0f,%.0f)"), *NameAr(), Abs.X, Abs.Y));
			}
			FTimerHandle H2;
			W->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateLambda([]()
			{
				UE_LOG(LogAsdaArabicTest, Warning, TEXT("САМОТЕСТ-AR КАДР → asda_ar_chat.png"));
				FScreenshotRequest::RequestScreenshot(TEXT("asda_ar_chat"), /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			}), 1.0f, false);
			FTimerHandle H3;
			W->GetTimerManager().SetTimer(H3, FTimerDelegate::CreateLambda([T, W, Net, Chat, Tip, UserNumWas, bKnownWas, bQuit]()
			{
				Tip->HideTooltip();
				Chat->CloseInput();
				Net->SelfUserNum = UserNumWas; Net->bSelfUserNumKnown = bKnownWas;
				for (const int32 U : { kPeerAr, kPeerRu })   // соседи уходят штатным 4011
				{
					TArray<uint8> B; B.SetNumZeroed(2); B[0] = (uint8)(U & 0xFF); B[1] = (uint8)(U >> 8);
					Net->DebugFeedPacket(4011, B);
				}
				T->Finish(W, bQuit);
			}), 3.0f, false);
		}), 2.0f, false);
	}

	static void Run(UWorld* W, bool bShot, bool bQuit)
	{
		TSharedRef<FAsdaArabicTextSelfTest> T = MakeShared<FAsdaArabicTextSelfTest>();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		T->RunCodec();
		T->RunNames();
		if (!Net) { T->Expect(false, TEXT("нет сетевой подсистемы")); }
		else if (Net->SendCaptureForStand) { T->Expect(false, TEXT("перехватчик сети занят другим инструментом — B/C пропущены")); }
		else
		{
			T->RunSenders(Net);
			T->RunReceive(Net);
		}
		if (bShot && Net && W) { RunPlayerPath(T, W, Net, bQuit); return; }
		T->Finish(W, bQuit);
	}
};

static FAutoConsoleCommandWithWorldAndArgs GAsdaArabicTextSelfTest(
	TEXT("asda.Text.SelfTestArabic"),
	TEXT("asda.Text.SelfTestArabic [shot] [quit] — ОФЛАЙН-самотест многоязычного чата (RU/EN/AR): кодек, байты 5084/5086/6560/5088/5105/4120, приём 5085/5087/5089/4121/5106/6561/6719; shot — путь игрока (ввод, лента, облачко, подсказка, кадр)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Text.SelfTestArabic"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Text.SelfTestArabic"), KitArgs)) { return; }
		bool bQuit = false;   // выйти после прогона
		bool bShot = false;   // путь игрока + кадр
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
			else if (Arg.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { bShot = true; }
		}
		FAsdaArabicTextSelfTest::Run(W, bShot, bQuit);
	}));

#endif // !UE_BUILD_SHIPPING
