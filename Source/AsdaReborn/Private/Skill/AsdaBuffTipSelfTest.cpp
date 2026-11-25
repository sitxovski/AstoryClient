// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ОПИСАНИЕ БАФФА = ПАРАМЕТР» (08.11.2026, пачка 18 баг 5: Fearful Counterattack 791 даёт защиту +20 %, в описании 10 %).
//   Команда `asda.Skill.SelfTestBuffTip`. Причина — текст оригинала: один токен [s1] на два эффекта, а защита — слот 1 ([s2]); параметры
//   клиента (skill_info.sif) и сервера (SkillInfo.sif GameServer) совпадают. Тексты 791 и 765 исправлены в данных (ведомость
//   `Reports/batch18/skills/LEDGER_bug5_skilltooltip.md`). Путь игрока: окно навыков (SKILL_WIN 12000) класса полосы 2,
//   вкладка навыка (по grade), полный список 5251; курсор наводится на узлы `SKILLTREE_ICON{NN}_grp` (событие движения мыши Slate)
//   до плашки с именем навыка. Проверка: в тексте есть оба числа слотов 0 и 1 на одном уровне (L·perLevel + base по записи .sif),
//   они разные, сырых токенов нет. Кадры — `Reports/batch18/skills/<ru|en>_tip_<навык>.png`. Офлайн, карта мира. `delay=N` `quit`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaItemTooltipSubsystem.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBuffTipTest, Log, All);

namespace AsdaBuffTipTest
{
	static const TCHAR* kCmd = TEXT("asda.Skill.SelfTestBuffTip");   // имя команды
	static const TCHAR* kPfx = TEXT("ПОДСКАЗКА-БАФФА");               // префикс строк и итога
	// ★ Случаи: навык, класс (полоса по grade), подпрофессии для вкладки 4-й профессии (−1 — не нужна), какие слоты должны быть
	//   в тексте числом (бит k — слот k), с «%» или без, и запрет «0%» (1045: прежде на месте числа стоял пустой слот 0).
	struct FCase { int32 Code; int32 Job; int32 SubA; int32 SubB; int32 SlotMask; bool bPct; bool bNoZero; };
	static const FCase kCases[] = {
		{ 791,  12, -1, -1, 0b011, true,  false },   // Fearful Counterattack: атака [s1], защита [s2]
		{ 765,  12, -1, -1, 0b011, true,  false },   // Мастер стрельбы: кровотечение [s1], отравление [s2]
		{ 974,  23, -1, -1, 0b011, false, false },   // Последняя воля: лечение [s1] ОЗ, щит 190 [s2] урона (08.11, решение владельца)
		{ 1045, 23, 26, 28, 0b010, true,  true  },   // Источник жизни: восстановление [s2]% (08.11, решение владельца)
		{ 711,  12, -1, -1, 0b111, true,  false },   // Снайпер: кровотечение [s1], отравление [s2], заражение [s3] (08.11, решение владельца «Да, заражение»)
	};
	constexpr int32 kSlots = 45;           // узлов сетки 5×9
	constexpr float kHoverStep = 0.2f;     // пауза между наведениями, с

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;            // пройдено / провалено
		TWeakObjectPtr<UWorld> W;          // мир прогона
		bool bQuit = false;                // выйти после итога
		int32 SkillIdx = 0;                // какой случай из kCases проверяем
		bool bAltSub = false;              // идёт вторая попытка с подпрофессией SubB
		int32 NextSlot = 0;                // следующий узел для наведения
		int32 Scroll = 0;                  // текущая прокрутка сетки
		int32 Hovered = 0;                 // узлов наведено (диагностика)
		FString FrameDir;                  // куда класть кадр

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaBuffTipTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaBuffTipTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		const FCase& Case() const { return kCases[SkillIdx]; }
		int32 Skill() const { return Case().Code; }
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
	// Узел сетки окна по номеру.
	static UWidget* Node(const FCtxRef& C, int32 N)
	{
		UAsdaSkillWindowSubsystem* SW = C->Sub<UAsdaSkillWindowSubsystem>();
		UUserWidget* Win = SW ? SW->GetWindowWidget() : nullptr;
		return (Win && Win->WidgetTree) ? Win->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("SKILLTREE_ICON%02d_grp"), N))) : nullptr;
	}

	static void Finish(const FCtxRef& C)
	{
		if (UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>()) { T->HideTooltipFor(EAsdaTipOwner::SkillWin); }
		if (UAsdaSkillWindowSubsystem* SW = C->Sub<UAsdaSkillWindowSubsystem>()) { SW->SetJobForTest(-1, -1); }   // −1 — без подмены класса
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaBuffTipTest, kPfx, C->Ok, C->Fail);
	}

	static void StartSkill(const FCtxRef& C);

	// Проверка текста: числа слотов 0 и 1 одного уровня есть в плашке и различны, сырых токенов нет. Кадр.
	static void Judge(const FCtxRef& C, const FString& T)
	{
		const UAsdaSkillDatabase* Db = C->Sub<UAsdaSkillDatabase>();
		const FAsdaSkillDef* D = Db ? Db->Get(C->Skill()) : nullptr;
		UE_LOG(LogAsdaBuffTipTest, Log, TEXT("%s навык %d, текст плашки: %s"), kPfx, C->Skill(), *T.Replace(TEXT("\n"), TEXT(" | ")));
		C->Expect(!T.IsEmpty() && D, FString::Printf(TEXT("навык %d: подсказка узла показана наведением"), C->Skill()));
		const FCase& K = C->Case();
		int32 FoundL = -1; FString Vals;
		if (D)
		{
			for (int32 L = FMath::Max(1, D->MaxLevel); L >= 1 && FoundL < 0; --L)
			{
				const int32 V[3] = { L * D->S1PerLevel + D->S1Base, L * D->S2PerLevel + D->S2Base, L * D->S3PerLevel + D->S3Base };
				bool bAll = true; FString Seen;
				for (int32 k = 0; k < 3; ++k)
				{
					if (!(K.SlotMask & (1 << k))) { continue; }
					const FString Num = K.bPct ? FString::Printf(TEXT("%d%%"), V[k]) : FString::FromInt(V[k]);
					bAll = bAll && V[k] != 0 && T.Contains(Num);
					Seen += FString::Printf(TEXT(" слот %d = %s"), k, *Num);
				}
				if (bAll) { FoundL = L; Vals = Seen; }
			}
		}
		C->Expect(FoundL > 0, FString::Printf(TEXT("навык %d: в описании настоящие параметры —%s (уровень %d)"), C->Skill(), *Vals, FoundL));
		if (K.bNoZero)
		{
			C->Expect(!T.Contains(TEXT(" 0%")), FString::Printf(TEXT("навык %d: «0%%» на месте числа больше нет"), C->Skill()));
		}
		C->Expect(!T.Contains(TEXT("[s1]")) && !T.Contains(TEXT("[s2]")) && !T.Contains(TEXT("[sdt1]")), FString::Printf(TEXT("навык %d: сырых токенов нет"), C->Skill()));
		const FString File = C->FrameDir / FString::Printf(TEXT("%s_tip_%d.png"), FAsdaLang::Code(FAsdaLang::Current()), C->Skill());
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogAsdaBuffTipTest, Display, TEXT("%s кадр заказан → %s"), kPfx, *File);
		At(C, 1.0f, [C]()
		{
			if (++C->SkillIdx < (int32)UE_ARRAY_COUNT(kCases)) { C->bAltSub = false; StartSkill(C); return; }
			Finish(C);
		});
	}

	// Наведение на очередной узел; найденный по заголовку — в Judge.
	static void HoverNext(const FCtxRef& C)
	{
		UAsdaSkillWindowSubsystem* SW = C->Sub<UAsdaSkillWindowSubsystem>();
		const UAsdaSkillDatabase* Db = C->Sub<UAsdaSkillDatabase>();
		const FAsdaSkillDef* D = Db ? Db->Get(C->Skill()) : nullptr;
		if (!SW || !D) { C->Expect(false, FString::Printf(TEXT("нет окна навыков или записи %d"), C->Skill())); Finish(C); return; }
		while (C->NextSlot < kSlots)
		{
			FVector2D P;
			if (!Center(Node(C, C->NextSlot++), P)) { continue; }
			Hover(P);
			At(C, kHoverStep, [C, D]()
			{
				const FString T = TipText(C);
				++C->Hovered;
				if (!T.IsEmpty() && T.Contains(D->Name)) { Judge(C, T); return; }
				HoverNext(C);
			});
			return;
		}
		if (C->Scroll < 8)
		{
			C->Scroll += 3; C->NextSlot = 0;
			SW->SetScrollRow(C->Scroll);
			At(C, 0.4f, [C]() { HoverNext(C); });
			return;
		}
		if (!C->bAltSub && C->Case().SubB > 0)   // вкладка 4-й профессии: вторая подпрофессия полосы
		{
			C->bAltSub = true;
			StartSkill(C);
			return;
		}
		C->Expect(false, FString::Printf(TEXT("узел навыка %d не найден наведением (наведено узлов %d)"), C->Skill(), C->Hovered));
		Finish(C);
	}

	// Вкладка навыка по grade записи (`UAsdaSkillDatabase::TreeFromGrade`), полный список, наведение с первой прокрутки.
	static void StartSkill(const FCtxRef& C)
	{
		UAsdaSkillWindowSubsystem* SW = C->Sub<UAsdaSkillWindowSubsystem>();
		const UAsdaSkillDatabase* Db = C->Sub<UAsdaSkillDatabase>();
		const FAsdaSkillDef* D = Db ? Db->Get(C->Skill()) : nullptr;
		if (!SW || !D) { C->Expect(false, FString::Printf(TEXT("нет окна навыков или записи %d"), C->Skill())); Finish(C); return; }
		const int32 Tree = UAsdaSkillDatabase::TreeFromGrade(D->Grade);
		C->NextSlot = 0; C->Scroll = 0; C->Hovered = 0;
		if (SW->IsWindowOpen()) { SW->CloseWindow(); }   // класс меняется между случаями — окно открываем заново
		SW->SetJobForTest(C->Case().Job, C->bAltSub ? C->Case().SubB : C->Case().SubA);
		SW->OpenWindow();                                 // класс — до открытия окна (как ParryTip)
		SW->SetTree(Tree);
		SW->SetScrollRow(0);
		SW->SimulateFullList(/*bAllAvailable*/ true);
		C->Expect(SW->IsWindowOpen() && SW->GetTree() == Tree, FString::Printf(TEXT("навык %d: окно навыков, вкладка %d (grade %d)"), C->Skill(), SW->GetTree(), D->Grade));
		At(C, 1.0f, [C]() { HoverNext(C); });
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaSkillWindowSubsystem* SW = C->Sub<UAsdaSkillWindowSubsystem>();
		if (!SW) { C->Expect(false, TEXT("нет подсистемы окна навыков")); Finish(C); return; }
		C->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/batch18/skills"));
		IFileManager::Get().MakeDirectory(*C->FrameDir, /*Tree*/ true);
		StartSkill(C);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestBuffTip(
	TEXT("asda.Skill.SelfTestBuffTip"),
	TEXT("Самотест описаний баффов 791/765/974/1045/711: окно навыков, наведение на узел — в описании оба параметра (атака и защита; "
	     "кровотечение и отравление). Кадры в Reports/batch18/skills. Офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaBuffTipTest;
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
