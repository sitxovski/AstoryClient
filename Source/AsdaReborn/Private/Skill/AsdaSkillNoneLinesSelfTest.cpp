// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ПОДСКАЗКА НАВЫКА: СТРОКИ «НЕТ» В EN» (05.11.2026, сессия gold-boast; баг: в EN-подсказке навыка «Required Skill» и
//   «Attribute» показывали русское «Нет»). Команда `asda.Skill.SelfTestNoneLines`.
//   Причина: нотисы GameText 2335 («Нет» — нет требуемого навыка) и 9497 («Нет» — нет атрибута) в RU — заглушки, EN-извлечение
//   заглушки не переводит, цепочка «нет EN → русский» давала «Нет». Лечение — данные: EN-оверлей дописан EN-оригиналом
//   (2335 «Do not have», 9497 «Does not exist», ещё 16 строк GameText, имена навыков skilltooltip 59 / skill_info 54);
//   код: `asda.Lang.DropRuPlaceholder` в `FAsdaLang::Resolve` — заглушка без перевода не печатается.
//   Путь игрока: окно навыков (SKILL_WIN 12000) воина, обычное дерево, полный список 5251; курсор наводится настоящими событиями
//   мыши Slate на ВСЕ узлы сетки `SKILLTREE_ICON{NN}_grp` по всем прокруткам; текст снимается с показанной плашки.
//   Проверки:
//     N1 найден узел, в подсказке которого «Атрибут : <9497>» и «Необходимый навык : <2335>» (значения — нотисы языка сессии);
//     N2 EN: 9497 = «Does not exist», 2335 = «Do not have» (EN-оригинал); RU: оба «Нет»;
//     N3 EN: ни в одной подсказке дерева нет кириллицы (все узлы);
//     K1/K2 ключ: оставшаяся заглушка без перевода (имя состояния stateinfotip 211, у EN-оригинала пусто) — EN: «» при 1 и «Нет»
//           при 0; RU: «Нет» при любом значении.
//     U1 (05.11, «12Seconds seconds»): ни в одной подсказке дерева за единицей длительности (нотисы 6824/6825) не стоит ещё одно
//        слово-единица (путь игрока, все узлы);
//     U2/U3 13 описаний с повтором в данных (502, 503, 515, 526, 560, 594, 604, 607, 705, 722, 757, 956, 1016): ключ
//        `asda.Skill.DurUnitDedup` 1 — повторов 0; ключ 0 (как эталон) — повтор есть у стольких же навыков, сколько в данных
//        языка (RU 13, EN 10).
//     H1–H4 (05.11, «в , раза»): RU-описания 594 «в 1,3 раза», 1034 «20%», 567 «до 5 раз» (дыры RU-перевода, правка — рецепт
//        item_name_overlay.csv + DT_skilltooltip), в EN — «1.3 times»/«20%»/«up to 5 times»; по всей базе навыков нет «в , раза».
//   Кадр — `Reports/gold_boast/frames/<ru|en>_skill_none.png`. Офлайн, карта мира. `delay=N` `quit`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSkillNoneTest, Log, All);

// Друг `UAsdaSkillWindowSubsystem` (объявлен в его заголовке): доступ к закрытому `ExpandSkillDescription` — тот же сборщик описания,
//   что у подсказки узла.
struct FAsdaSkillNoneLinesSelfTest
{
	// Описание навыка Code на уровне Lvl с подставленными токенами.
	static FString Expand(const UAsdaSkillWindowSubsystem* SW, int32 Code, int32 Lvl) { return SW ? SW->ExpandSkillDescription(Code, Lvl) : FString(); }
};

namespace AsdaSkillNoneTest
{
	static const TCHAR* kCmd = TEXT("asda.Skill.SelfTestNoneLines");   // имя команды
	static const TCHAR* kPfx = TEXT("НАВЫК-НЕТ");                        // префикс строк и итога
	static const TCHAR* kKey = TEXT("asda.Lang.DropRuPlaceholder");     // ключ исправления (переключается через AsdaCVarSave)
	constexpr int32 kJob = 3;              // воин (как самотест Parry)
	constexpr int32 kSubJob = 4;           // ветка «Неприступный щит»
	constexpr int32 kTree = 0;             // обычное дерево (первые навыки — без цепочки и без атрибута)
	constexpr int32 kSlots = 45;           // узлов сетки 5×9
	constexpr int32 kMaxScroll = 8;        // последняя прокрутка сетки (шаг 3)
	constexpr float kHoverStep = 0.2f;     // пауза между наведениями, с
	constexpr int32 kLocAttr = 9496;       // «Атрибут»
	constexpr int32 kLocAttrNone = 9497;   // «Нет» атрибута (EN-оригинал «Does not exist»)
	constexpr int32 kLocReqSkill = 9553;   // «Необходимый навык»
	constexpr int32 kLocReqNone = 2335;    // «Нет» требуемого навыка (EN-оригинал «Do not have»)
	constexpr int32 kLocSec = 6824;        // единица секунд за длительностью («сек.» / «Seconds»)
	constexpr int32 kLocMin = 6825;        // единица минут («мин.» / «Minutes»)
	static const TCHAR* kUnitKey = TEXT("asda.Skill.DurUnitDedup");   // ключ исправления повтора единицы
	// Навыки, у которых описание пишет единицу ещё раз за [sdt*]/[sdtt*] (скан Skilltooltip RU/EN/ORIG, 05.11).
	static const int32 kDupSkills[] = { 502, 503, 515, 526, 560, 594, 604, 607, 705, 722, 757, 956, 1016 };
	// Слова-единицы, которые не должны стоять за подставленной единицей (кириллица — явными формами).
	static const TCHAR* const kUnitWords[] = { TEXT("seconds"), TEXT("second"), TEXT("secs"), TEXT("sec"), TEXT("minutes"),
		TEXT("minute"), TEXT("mins"), TEXT("min"), TEXT("сек"), TEXT("секунд"), TEXT("мин"), TEXT("минут") };

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;            // пройдено / провалено
		TWeakObjectPtr<UWorld> W;          // мир прогона
		bool bQuit = false;                // выйти после итога
		int32 NextSlot = 0;                // следующий узел для наведения
		int32 Scroll = 0;                  // текущая прокрутка сетки
		int32 Hovered = 0;                 // подсказок снято (непустых)
		int32 CyrTips = 0;                 // подсказок с кириллицей (для EN — дефект)
		FString FirstCyr;                  // первая такая подсказка (диагностика)
		int32 DupTips = 0;                 // подсказок с повтором единицы (U1)
		FString FirstDup;                  // первая такая подсказка (диагностика)
		int32 SavedUnitKey = 1;            // значение asda.Skill.DurUnitDedup до прогона
		FString Found;                     // подсказка узла со строками «нет»
		bool bShot = false;                // кадр уже заказан
		int32 SavedKey = 1;                // значение ключа до прогона
		FString FrameDir;                  // куда класть кадр

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaSkillNoneTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaSkillNoneTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Курсор в точку + событие движения (наведение без щелчка).
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Центр живого прямоугольника узла (false — не отрисован / скрыт).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Весь текст показанной плашки (заголовок + тело).
	static FString TipText(const FCtxRef& C)
	{
		UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		UAsdaTipPanel* P = T ? T->GetMainPanel() : nullptr;
		UUserWidget* Wd = P ? P->GetWidget() : nullptr;
		if (!T || !T->IsTooltipShown() || !Wd || !Wd->WidgetTree) { return FString(); }
		TArray<UWidget*> All;
		Wd->WidgetTree->GetAllWidgets(All);
		FString Out;
		for (UWidget* X : All)
		{
			if (!X || !AsdaUi::IsShown(X)) { continue; }
			if (const UTextBlock* TB = Cast<UTextBlock>(X))              { Out += TB->GetText().ToString() + TEXT("\n"); }
			else if (const URichTextBlock* RB = Cast<URichTextBlock>(X)) { Out += RB->GetText().ToString() + TEXT("\n"); }
		}
		return Out;
	}
	// Есть ли кириллица (U+0400…U+04FF).
	using AsdaSelfTestInput::HasCyrillic;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия отличалась только именами параметров.
	// Повтор единицы: за подписью единицы U (6824/6825) через пробелы стоит ещё одно слово-единица.
	static bool HasDupUnit(const FString& T, const FString& U)
	{
		if (U.IsEmpty()) { return false; }
		int32 From = 0;
		for (int32 P = T.Find(U, ESearchCase::CaseSensitive, ESearchDir::FromStart, From); P != INDEX_NONE;
			P = T.Find(U, ESearchCase::CaseSensitive, ESearchDir::FromStart, From))
		{
			int32 i = P + U.Len();
			From = i;
			int32 Sp = i;
			while (Sp < T.Len() && T[Sp] == TEXT(' ')) { ++Sp; }
			if (Sp == i) { continue; }
			for (const TCHAR* W : kUnitWords)
			{
				const int32 N = FCString::Strlen(W);
				if (Sp + N <= T.Len() && T.Mid(Sp, N).Equals(W, ESearchCase::IgnoreCase)
					&& (Sp + N == T.Len() || !FChar::IsAlpha(T[Sp + N]))) { return true; }
			}
		}
		return false;
	}
	// Подпись нотиса на языке сессии.
	static FString Loc(const FCtxRef& C, int32 Id) { return AsdaNotice::LocRaw(C->GI(), Id, /*bDebugFallback*/ false).TrimStartAndEnd(); }
	// Узел сетки окна по номеру.
	static UWidget* Node(const FCtxRef& C, int32 N)
	{
		UAsdaSkillWindowSubsystem* SW = C->Sub<UAsdaSkillWindowSubsystem>();
		UUserWidget* Win = SW ? SW->GetWindowWidget() : nullptr;
		return (Win && Win->WidgetTree) ? Win->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("SKILLTREE_ICON%02d_grp"), N))) : nullptr;
	}

	static void Finish(const FCtxRef& C)
	{
		AsdaCVarSave::SetForTest(kKey, C->SavedKey);
		AsdaCVarSave::SetForTest(kUnitKey, C->SavedUnitKey);
		if (UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>()) { T->HideTooltipFor(EAsdaTipOwner::SkillWin); }
		if (UAsdaSkillWindowSubsystem* SW = C->Sub<UAsdaSkillWindowSubsystem>()) { SW->SetJobForTest(-1, -1); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaSkillNoneTest, kPfx, C->Ok, C->Fail);
	}

	// Итог обхода: строки «нет» найденного узла, кириллица в EN, ключ на оставшейся заглушке.
	static void Judge(const FCtxRef& C)
	{
		const bool bEn = !FAsdaLang::IsRu();
		const FString AttrNone = Loc(C, kLocAttrNone), ReqNone = Loc(C, kLocReqNone);
		UE_LOG(LogAsdaSkillNoneTest, Display, TEXT("%s узел со строками «нет»:\n%s"), kPfx, *C->Found);
		C->Expect(!C->Found.IsEmpty() && C->Found.Contains(Loc(C, kLocAttr)) && C->Found.Contains(AttrNone)
			&& C->Found.Contains(Loc(C, kLocReqSkill)) && C->Found.Contains(ReqNone),
			FString::Printf(TEXT("N1 подсказка навыка со строками «%s : %s» и «%s : %s» найдена (подсказок снято %d)"),
				*Loc(C, kLocAttr), *AttrNone, *Loc(C, kLocReqSkill), *ReqNone, C->Hovered));
		if (bEn)
		{
			C->Expect(AttrNone == TEXT("Does not exist") && ReqNone == TEXT("Do not have"),
				FString::Printf(TEXT("N2 EN: 9497 «%s» (ждём Does not exist), 2335 «%s» (ждём Do not have) — EN-оригинал"), *AttrNone, *ReqNone));
			C->Expect(C->CyrTips == 0, FString::Printf(TEXT("N3 EN: подсказок дерева с кириллицей %d из %d%s"), C->CyrTips, C->Hovered,
				C->FirstCyr.IsEmpty() ? TEXT("") : *(TEXT(", первая: ") + C->FirstCyr.Replace(TEXT("\n"), TEXT(" | ")))));
		}
		else
		{
			C->Expect(AttrNone == TEXT("Нет") && ReqNone == TEXT("Нет"),
				FString::Printf(TEXT("N2 RU: 9497 «%s», 2335 «%s» — оба «Нет» (RU-оригинал)"), *AttrNone, *ReqNone));
		}
		// Ключ — на оставшейся заглушке (у EN-оригинала пусто, в оверлей не дописывалась): имя состояния 211.
		const FString Ru = TEXT("Нет");
		AsdaCVarSave::SetForTest(kKey, 1);
		const FString On = FAsdaLang::Resolve(TEXT("stateinfotip"), TEXT("211.Name"), Ru);
		AsdaCVarSave::SetForTest(kKey, 0);
		const FString Off = FAsdaLang::Resolve(TEXT("stateinfotip"), TEXT("211.Name"), Ru);
		AsdaCVarSave::SetForTest(kKey, C->SavedKey);
		C->Expect(bEn ? On.IsEmpty() : On == Ru, FString::Printf(TEXT("K1 ключ 1: stateinfotip 211.Name → «%s» (ждём %s)"), *On, bEn ? TEXT("«»") : TEXT("«Нет»")));
		C->Expect(Off == Ru, FString::Printf(TEXT("K2 ключ 0 (прежнее): stateinfotip 211.Name → «%s» (ждём «Нет»)"), *Off));
		// U1 — путь игрока: повтор единицы ни в одной подсказке дерева.
		C->Expect(C->DupTips == 0, FString::Printf(TEXT("U1 подсказок дерева с повтором единицы %d из %d%s"), C->DupTips, C->Hovered,
			C->FirstDup.IsEmpty() ? TEXT("") : *(TEXT(", первая: ") + C->FirstDup.Replace(TEXT("\n"), TEXT(" | ")))));
		// U2/U3 — 13 описаний с повтором в данных, ключ 1 и 0.
		const UAsdaSkillWindowSubsystem* SW = C->Sub<UAsdaSkillWindowSubsystem>();
		const UAsdaSkillDatabase* Db = C->Sub<UAsdaSkillDatabase>();
		const FString Sec = Loc(C, kLocSec), Min = Loc(C, kLocMin);
		int32 Have = 0, DupOn = 0, DupOff = 0;
		FString Sample;
		for (const int32 Code : kDupSkills)
		{
			if (!Db || !Db->Get(Code)) { continue; }
			++Have;
			AsdaCVarSave::SetForTest(kUnitKey, 1);
			const FString A = FAsdaSkillNoneLinesSelfTest::Expand(SW, Code, 1);
			AsdaCVarSave::SetForTest(kUnitKey, 0);
			const FString B = FAsdaSkillNoneLinesSelfTest::Expand(SW, Code, 1);
			const bool bOn = HasDupUnit(A, Sec) || HasDupUnit(A, Min);
			const bool bOff = HasDupUnit(B, Sec) || HasDupUnit(B, Min);
			DupOn += bOn ? 1 : 0;
			DupOff += bOff ? 1 : 0;
			UE_LOG(LogAsdaSkillNoneTest, Display, TEXT("%s навык %d: ключ 1 «%s» | ключ 0 «%s»"), kPfx, Code, *A.Left(160), *B.Left(160));
			if (Code == 502) { Sample = A; }
		}
		AsdaCVarSave::SetForTest(kUnitKey, 1);
		const int32 WantOff = bEn ? 10 : 13;
		C->Expect(Have == UE_ARRAY_COUNT(kDupSkills) && DupOn == 0,
			FString::Printf(TEXT("U2 ключ 1: описаний с повтором единицы %d из %d (навыков в базе %d); 502: «%s»"), DupOn, Have,
				Have, *Sample.Left(120)));
		C->Expect(DupOff == WantOff, FString::Printf(TEXT("U3 ключ 0 (как эталон): повтор у %d описаний (ждём %d — данные %s)"),
			DupOff, WantOff, bEn ? TEXT("EN = EN-оригинал") : TEXT("RU")));
		// H1–H3 (05.11, «в , раза»): дыры на месте числа в RU-переводе (Фантазиум 2012) — 594, 1034, 567; в EN число есть.
		struct FHole { int32 Code; const TCHAR* Ru; const TCHAR* En; };
		static const FHole kHoles[] = {
			{ 594,  TEXT("в 1,3 раза"),          TEXT("1.3 times") },
			{ 1034, TEXT("поглощается 20% урона"), TEXT("absorb 20%") },
			{ 567,  TEXT("до 5 раз"),            TEXT("up to 5 times") } };
		for (const FHole& H : kHoles)
		{
			const FString Desc = FAsdaSkillNoneLinesSelfTest::Expand(SW, H.Code, 1);
			const TCHAR* Want = bEn ? H.En : H.Ru;
			C->Expect(Desc.Contains(Want), FString::Printf(TEXT("H навык %d: «%s» в описании — «%s»"), H.Code, Want, *Desc.Left(200)));
		}
		// H4 — по всем навыкам базы: нет «в , раза» / «на , » / «поглощается 0%» (пустое место или ноль на месте числа).
		int32 Holes = 0, Scanned = 0;
		FString FirstHole;
		for (int32 Code = 1; Code <= 1300; ++Code)
		{
			if (!Db || !Db->Get(Code)) { continue; }
			++Scanned;
			const FString Desc = FAsdaSkillNoneLinesSelfTest::Expand(SW, Code, 1);
			if (Desc.Contains(TEXT(" в , ")) || Desc.Contains(TEXT(" на , ")) || Desc.Contains(TEXT(" by , ")) || Desc.Contains(TEXT(" for , "))
				|| Desc.Contains(TEXT("в , раза")) || Desc.Contains(TEXT(" , times")))
			{
				++Holes;
				if (FirstHole.IsEmpty()) { FirstHole = FString::Printf(TEXT("%d: %s"), Code, *Desc.Left(160)); }
			}
		}
		C->Expect(Scanned > 0 && Holes == 0, FString::Printf(TEXT("H4 описаний с дырой на месте числа %d из %d%s"), Holes, Scanned,
			FirstHole.IsEmpty() ? TEXT("") : *(TEXT(", первое: ") + FirstHole)));
		At(C, 1.0f, [C]() { Finish(C); });
	}

	// Наведение на очередной узел; обход ВСЕХ узлов всех прокруток, кадр — на узле со строками «нет».
	static void HoverNext(const FCtxRef& C)
	{
		UAsdaSkillWindowSubsystem* SW = C->Sub<UAsdaSkillWindowSubsystem>();
		if (!SW) { C->Expect(false, TEXT("нет окна навыков")); Finish(C); return; }
		while (C->NextSlot < kSlots)
		{
			FVector2D P;
			if (!Center(Node(C, C->NextSlot++), P)) { continue; }
			Hover(P);
			At(C, kHoverStep, [C]()
			{
				const FString T = TipText(C);
				if (!T.IsEmpty())
				{
					++C->Hovered;
					if (HasCyrillic(T)) { ++C->CyrTips; if (C->FirstCyr.IsEmpty()) { C->FirstCyr = T.Left(120); } }
					if (HasDupUnit(T, Loc(C, kLocSec)) || HasDupUnit(T, Loc(C, kLocMin)))
					{
						++C->DupTips;
						if (C->FirstDup.IsEmpty()) { C->FirstDup = T.Left(200); }
					}
					const bool bNone = T.Contains(Loc(C, kLocAttrNone)) && T.Contains(Loc(C, kLocReqNone)) && T.Contains(Loc(C, kLocReqSkill));
					if (bNone && C->Found.IsEmpty())
					{
						C->Found = T;
						const FString File = C->FrameDir / FString::Printf(TEXT("%s_skill_none.png"), FAsdaLang::Code(FAsdaLang::Current()));
						FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
						UE_LOG(LogAsdaSkillNoneTest, Display, TEXT("%s кадр заказан → %s"), kPfx, *File);
					}
				}
				HoverNext(C);
			});
			return;
		}
		if (C->Scroll < kMaxScroll)
		{
			C->Scroll = FMath::Min(C->Scroll + 3, kMaxScroll); C->NextSlot = 0;
			SW->SetScrollRow(C->Scroll);
			At(C, 0.4f, [C]() { HoverNext(C); });
			return;
		}
		Judge(C);
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaSkillWindowSubsystem* SW = C->Sub<UAsdaSkillWindowSubsystem>();
		if (!SW) { C->Expect(false, TEXT("нет подсистемы окна навыков")); Finish(C); return; }
		if (const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kKey)) { C->SavedKey = V->GetInt(); }
		if (const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kUnitKey)) { C->SavedUnitKey = V->GetInt(); }
		AsdaCVarSave::SetForTest(kKey, 1);
		AsdaCVarSave::SetForTest(kUnitKey, 1);
		C->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/gold_boast/frames"));
		IFileManager::Get().MakeDirectory(*C->FrameDir, /*Tree*/ true);
		SW->SetJobForTest(kJob, kSubJob);
		SW->OpenWindow();
		SW->SetTree(kTree);
		SW->SimulateFullList(/*bAllAvailable*/ true);
		C->Expect(SW->IsWindowOpen() && SW->GetTree() == kTree, FString::Printf(TEXT("окно навыков открыто, вкладка %d"), SW->GetTree()));
		At(C, 1.0f, [C]() { HoverNext(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSkillNoneLines(
	TEXT("asda.Skill.SelfTestNoneLines"),
	TEXT("Самотест подсказки навыка: строки «Атрибут»/«Необходимый навык» без значения — EN «Does not exist»/«Do not have» (EN-оригинал), "
	     "RU «Нет»; в EN ни одна подсказка дерева не содержит кириллицы; ключ asda.Lang.DropRuPlaceholder на оставшейся заглушке. "
	     "Кадр в Reports/gold_boast/frames. Офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaSkillNoneTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
