// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ПОДСКАЗКА PARRY (611) УПОМИНАЕТ ОБЕЗДВИЖИВАНИЕ» (05.11.2026, решение владельца: состояние 13 «Недвижимый Хранитель»
//   оставлено как в оригинале — сервер обездвиживает кастера на длительность buff0; в описании навыка это теперь сказано).
//   Команда `asda.Skill.SelfTestParryTip`. Путь игрока: окно навыков (SKILL_WIN 12000) воина с 4-й профессией (класс 3, SubJob 4 —
//   ветка «Неприступный щит»), вкладка 4-й профессии, полный список 5251; курсор наводится на узлы дерева `SKILLTREE_ICON{NN}_grp`
//   (настоящее событие движения мыши Slate) до узла, чья подсказка озаглавлена именем навыка 611. Проверяется текст плашки:
//   фраза об обездвиживании есть (RU «Недвижимый Хранитель» / EN «Receive Instead» и «cannot move»), токен [sdt1] подставлен
//   длительностью, сырых токенов нет. Данные — `DT_skilltooltip` (RU) / `DT_Loc_skilltooltip_en` (EN), строка 611.Text.
//   Кадр — `Reports/job4_skills/frames/<ru|en>_parry_tip.png`. Офлайн, карта мира. `delay=N` `quit`.
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

DEFINE_LOG_CATEGORY_STATIC(LogAsdaParryTipTest, Log, All);

namespace AsdaParryTipTest
{
	static const TCHAR* kCmd = TEXT("asda.Skill.SelfTestParryTip");   // имя команды
	static const TCHAR* kPfx = TEXT("ПОДСКАЗКА-PARRY");                // префикс строк и итога
	constexpr int32 kSkill = 611;          // Parry
	constexpr int32 kJob = 3;              // воин (полоса 1)
	constexpr int32 kSubJob = 4;           // 4-я профессия ветки «Неприступный щит» (подпись вкладки 9423)
	constexpr int32 kTree = 3;             // вкладка 4-й профессии
	constexpr int32 kSlots = 45;           // узлов сетки 5×9
	constexpr float kHoverStep = 0.2f;     // пауза между наведениями, с

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;            // пройдено / провалено
		TWeakObjectPtr<UWorld> W;          // мир прогона
		bool bQuit = false;                // выйти после итога
		int32 NextSlot = 0;                // следующий узел для наведения
		int32 Scroll = 0;                  // текущая прокрутка сетки
		int32 Hovered = 0;                 // узлов наведено (диагностика)
		FString Found;                     // текст плашки найденного узла
		FString FrameDir;                  // куда класть кадр

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaParryTipTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaParryTipTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
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
		Out.ReplaceInline(TEXT("&quot;"), TEXT("\""));
		Out.ReplaceInline(TEXT("&apos;"), TEXT("'"));
		Out.ReplaceInline(TEXT("&lt;"), TEXT("<"));
		Out.ReplaceInline(TEXT("&gt;"), TEXT(">"));
		Out.ReplaceInline(TEXT("&amp;"), TEXT("&"));
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
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaParryTipTest, kPfx, C->Ok, C->Fail);
	}

	// Проверка найденного текста и кадр.
	static void Judge(const FCtxRef& C)
	{
		const FString& T = C->Found;
		const bool bEn = FAsdaLang::Current() == EAsdaLang::En;
		UE_LOG(LogAsdaParryTipTest, Log, TEXT("%s текст плашки: %s"), kPfx, *T.Replace(TEXT("\n"), TEXT(" | ")));
		C->Expect(!T.IsEmpty(), TEXT("подсказка узла навыка 611 показана наведением"));
		if (bEn)
		{
			C->Expect(T.Contains(TEXT("Receive Instead")) && T.Contains(TEXT("you cannot move")),
				TEXT("EN: фраза «While Receive Instead is active (…), you cannot move.» есть"));
			C->Expect(T.Contains(TEXT("Decreases damage received")), TEXT("EN: прежнее описание сохранено"));
		}
		else
		{
			C->Expect(T.Contains(TEXT("Недвижимый Хранитель")) && T.Contains(TEXT("не может двигаться")),
				TEXT("RU: фраза «На время действия состояния «Недвижимый Хранитель» (…) персонаж не может двигаться.» есть"));
			C->Expect(T.Contains(TEXT("Уменьшает урон")), TEXT("RU: прежнее описание сохранено"));
		}
		C->Expect(!T.Contains(TEXT("[sdt1]")) && !T.Contains(TEXT("[s1]")) && !T.Contains(TEXT("()")),
			TEXT("токены [s1]/[sdt1] подставлены (сырых нет, скобки длительности не пустые)"));
		const FString File = C->FrameDir / FString::Printf(TEXT("%s_parry_tip.png"), FAsdaLang::Code(FAsdaLang::Current()));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogAsdaParryTipTest, Display, TEXT("%s кадр заказан → %s"), kPfx, *File);
		At(C, 1.0f, [C]() { Finish(C); });
	}

	// Наведение на очередной узел; найденный по заголовку — в Judge.
	static void HoverNext(const FCtxRef& C)
	{
		UAsdaSkillWindowSubsystem* SW = C->Sub<UAsdaSkillWindowSubsystem>();
		const UAsdaSkillDatabase* Db = C->Sub<UAsdaSkillDatabase>();
		const FAsdaSkillDef* D = Db ? Db->Get(kSkill) : nullptr;
		if (!SW || !D) { C->Expect(false, TEXT("нет окна навыков или записи 611")); Finish(C); return; }
		while (C->NextSlot < kSlots)
		{
			FVector2D P;
			if (!Center(Node(C, C->NextSlot++), P)) { continue; }
			Hover(P);
			At(C, kHoverStep, [C, D]()
			{
				const FString T = TipText(C);
				++C->Hovered;
				UE_LOG(LogAsdaParryTipTest, Log, TEXT("%s узел %d (прокрутка %d): плашка «%s»"), kPfx, C->NextSlot - 1, C->Scroll,
					*T.Left(60).Replace(TEXT("\n"), TEXT(" | ")));
				if (!T.IsEmpty() && T.Contains(D->Name)) { C->Found = T; Judge(C); return; }
				HoverNext(C);
			});
			return;
		}
		// Узел не виден на этой прокрутке — следующий ряд сетки.
		if (C->Scroll < 8)
		{
			C->Scroll += 3; C->NextSlot = 0;
			SW->SetScrollRow(C->Scroll);
			At(C, 0.4f, [C]() { HoverNext(C); });
			return;
		}
		C->Expect(false, FString::Printf(TEXT("узел навыка 611 не найден наведением ни на одной прокрутке (наведено узлов %d)"), C->Hovered));
		Finish(C);
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaSkillWindowSubsystem* SW = C->Sub<UAsdaSkillWindowSubsystem>();
		if (!SW) { C->Expect(false, TEXT("нет подсистемы окна навыков")); Finish(C); return; }
		C->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/job4_skills/frames"));
		IFileManager::Get().MakeDirectory(*C->FrameDir, /*Tree*/ true);
		SW->SetJobForTest(kJob, kSubJob);
		SW->OpenWindow();
		SW->SetTree(kTree);
		SW->SimulateFullList(/*bAllAvailable*/ true);
		C->Expect(SW->IsWindowOpen() && SW->GetTree() == kTree, FString::Printf(TEXT("окно навыков открыто, вкладка %d (4-я профессия)"), SW->GetTree()));
		At(C, 1.0f, [C]() { HoverNext(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestParryTip(
	TEXT("asda.Skill.SelfTestParryTip"),
	TEXT("Самотест подсказки Parry (611): окно навыков воина 4-й профессии, наведение на узел — в описании есть обездвиживание "
	     "«Недвижимый Хранитель» / «Receive Instead» с длительностью [sdt1]. Кадр в Reports/job4_skills/frames. Офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaParryTipTest;
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
