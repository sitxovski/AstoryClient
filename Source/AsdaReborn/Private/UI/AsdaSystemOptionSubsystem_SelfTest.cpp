// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSystemOptionSubsystem_SelfTest.cpp — ★ R2 10.11.2026 (разрез AsdaSystemOptionSubsystem.cpp, дословный перенос): самотесты окна (RunSelfTest/DmgText/Mouse/CrouchMode).
//   Общее для частей — в AsdaSystemOptionSubsystemInternal.h; шапка и реверс — в AsdaSystemOptionSubsystem.cpp.
#include "UI/AsdaSystemOptionSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaEscChain.h"   // ★ 25.10.2026: шаг «ESC снимает выбранную цель» эталонной цепочки (между проходом окон и меню 69400)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaLocIds.h"
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaShowOffSubsystem.h"
#include "UI/AsdaChatBalloonSubsystem.h"
#include "UI/AsdaAfterDeathSubsystem.h"   // ★ гейт ESC, ветка 1: принудительный экран смерти
#include "Resources/AsdaVideoSettingsSubsystem.h"
#include "Resources/AsdaBenchSubsystem.h"   // ★ 12.10.2026: «Автоподбор» = замер настоящего кадра на месте
#include "Resources/AsdaGameUserSettings.h"   // ★ 19.09.2026: профиль игрока — штатное хранилище настроек окна
#include "Resources/AsdaSoundSubsystem.h"      // ★ 19.09.2026: громкость — через профиль, мимо приоритетов ключей консоли
#include "World/AsdaKeybindConfig.h"
#include "World/AsdaKeybindProfile.h"
#include "World/AsdaPlayerCharacter.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Text/AsdaLang.h"   // ★ 09.10.2026: строка «Язык» (мультиязычность)
#include "Scalability.h"   // ★ фаза 2: уровни групп качества при открытии окна
#include "Test/AsdaSelfTestKit.h"   // ★ 12.10.2026: самотест asda.SysOpt.SelfTest (аргументы, отказ онлайн, итог)
#include "World/AsdaWorldSubsystem.h"   // ★ 18.10.2026: самотест строки «Цифры урона» — GetDamageTextMode/ShouldShowDamageText
#include "UnrealClient.h"                // ★ 18.10.2026: FScreenshotRequest — кадры окна в самотесте «Цифры урона»
#include "Misc/FileHelper.h"             // ★ 18.10.2026: чтение GameUserSettings.ini в самотесте «Цифры урона»
#include "Containers/Ticker.h"           // ★ 18.10.2026: пошаговый самотест «Цифры урона»
#include "UI/AsdaJuiWindowHandle.h"        // ★ Дедуп №2, В1-B (U22): каркас `EnsureLazy` окон 69400/69500
#include "World/AsdaCursorClip.h"           // ★ 01.11.2026: курсор не покидает окно игры
#include "World/AsdaOrbitCursor.h"          // ★ 07.11.2026: курсор при зажатой ПКМ скрыт и стоит на месте
#include "World/AsdaCursorSpeed.h"          // ★ 19.10.2026: «Чувствительность мыши» (строка I2) — скорость курсора
#include "AsdaSystemOptionSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaSystemOptionSubsystem*.cpp

// ═══════════════════════════ ★ 12.10.2026 САМОТЕСТ ОКНА `asda.SysOpt.SelfTest [quit]` ═══════════════════════════════
//   По образцу `asda.Bench.SelfTest` (каркас `Test/AsdaSelfTestKit.h`). Офлайн (`-AsdaNoNet`): «ОК» окна шлёт серверу 4070,
//   поэтому старт — через `RefuseStart` (онлайн — отказ; офлайн — окно защиты на все опкоды). Проверки:
//     1) окно 69500 открывается; 2) у каждой строки `kRows` (22 записи: 20 контролов + 2 примечания) ключевой узел в ассете есть; списков подключено ровно столько,
//        сколько строк-списков; флажков фазы 2 привязано ровно столько, сколько строк-флажков; полоса Q9 собрана;
//     3) узлов снятых строк (`kRemovedTags`) в ассете НЕТ (иначе шаг ассета не прогнан);
//     4) `SetSliderValue` полосы Q9 → буфер `State.DrawDistance`; `CloseOptions(true)` («ОК») → `asda.Cull.Scale` = значение/100
//        и поле профиля `DrawDistanceScale`; окно закрыто;
//     5) откат: прежнее значение возвращается тем же общим путём (`SetRenderOverride`/`ClearRenderOverride`, с записью профиля).
//   Итог — `САМОТЕСТ-НАСТРОЙКИ ИТОГ: ok=N fail=M` (ловит `asda.SelfTest.Batch`), при `quit` — выход.
void UAsdaSystemOptionSubsystem::RunSelfTest(const TArray<FString>& Args)
{
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping тела нет
	static const TCHAR* const kCmd = TEXT("asda.SysOpt.SelfTest");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-НАСТРОЙКИ");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (AsdaSelfTestKit::RefuseStart(this, kCmd, KitArgs)) { return; }
	UAsdaVideoSettingsSubsystem* Vid = GI ? GI->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
	UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get();
	if (!W || !Vid || !Profile) { AsdaSelfTestKit::Refused(this, kCmd, KitArgs.bQuit, TEXT("нет мира, подсистемы видео или профиля игрока")); return; }
	// ★ 26.10.2026 (ревизия wave_1025d п. 7): «ОК» ниже пишет раскладку — только в песочницу (схема asda.SelfTest.KeybindActions).
	FAsdaKeybindTestSandbox KeySandbox;
	{
		FString Why;
		if (!KeySandbox.Begin(GetKeybinds(), TEXT("sysopt"), Why)) { AsdaSelfTestKit::Refused(this, kCmd, KitArgs.bQuit, *Why); return; }
	}

	int32 Ok = 0, Fail = 0;
	auto Check = [&Ok, &Fail](bool bPass, const FString& What)
	{
		if (bPass) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-НАСТРОЙКИ %s %s"), bPass ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
	};
	auto CullScale = []() -> float
	{
		const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Cull.Scale"));
		return V ? V->GetFloat() : -1.f;
	};

	// 1. Окно открыто.
	if (IsOptionsShown()) { CloseOptions(/*bApply*/ false); }
	ShowOptions();
	Check(IsOptionsShown() && OptWidget != nullptr, TEXT("окно 69500 открыто (asda.SysOpt.Open)"));
	if (!OptWidget)
	{
		AsdaSelfTestKit::Finish(W, kCmd, KitArgs.bQuit, LogAsdaSysOpt, kPrefix, Ok, Fail, TEXT(" (виджета окна нет)"), /*bWarning*/ true);
		return;
	}

	// 2. Состав строк: ровно ожидаемые есть.
	int32 WantCombos = 0, WantChecks = 0;
	for (const SysOptExtra::FRowDef& R : SysOptExtra::kRows)
	{
		const FString Node = SysOptExtra::KeyNode(R);
		Check(OptWidget->GetWidgetFromName(FName(*Node)) != nullptr, FString::Printf(TEXT("строка %s: узел %s есть"), R.Tag, *Node));
		if (R.Kind == SysOptExtra::Combo) { ++WantCombos; }
		if (R.Kind == SysOptExtra::Check) { ++WantChecks; }
	}
	Check(ExtraCombos.Num() == WantCombos, FString::Printf(TEXT("списков фазы 2 подключено %d (ждали %d)"), ExtraCombos.Num(), WantCombos));
	int32 BoundChecks = 0;
	for (const FAsdaOptCheck& C : Checks) { if (C.IdNum >= SysOptExtra::kIdBase && C.Box) { ++BoundChecks; } }
	Check(BoundChecks == WantChecks, FString::Printf(TEXT("флажков фазы 2 привязано %d (ждали %d)"), BoundChecks, WantChecks));
	FAsdaOptSlider* Draw = FindSlider(SysOptExtra::kIdBase + SysOptExtra::Q9_Draw);
	Check(Draw && Draw->Track && Draw->Thumb && Draw->Min == 50 && Draw->Max == 200,
		TEXT("полоса «Дальность прорисовки» (Q9) найдена, планы собраны, диапазон 50…200"));

	// 3. Снятых строк в ассете нет.
	for (const TCHAR* Tag : SysOptExtra::kRemovedTags)
	{
		FString Left;
		for (const TCHAR* Suf : SysOptExtra::kRowSuffixes)
		{
			if (OptWidget->GetWidgetFromName(FName(*SysOptExtra::NodeName(Tag, Suf)))) { Left += FString::Printf(TEXT("%s_%s "), Tag, Suf); }
		}
		Check(Left.IsEmpty(), Left.IsEmpty() ? FString::Printf(TEXT("снятая строка %s: узлов в ассете нет"), Tag)
		                                     : FString::Printf(TEXT("снятая строка %s: остались узлы [%s] — шаг ассета не прогнан"), Tag, *Left.TrimEnd()));
	}
	{
		int32 Missing = 0, Stale = 0;
		const FString Audit = SysOptRowsAudit(OptWidget, Missing, Stale);
		Check(Missing == 0 && Stale == 0, FString::Printf(TEXT("аудит состава: %s"), *Audit));
	}

	// 4. Ползунок → буфер → «ОК» → ключ и профиль.
	const float ScaleBefore = CullScale();
	const float ProfileBefore = Profile->DrawDistanceScale;   // < 0 = «не менялась»
	const int32 Want = (State.DrawDistance == 150) ? 125 : 150;   // заведомо отличное от текущего
	if (Draw)
	{
		SetSliderValue(*Draw, Want);
		Check(State.DrawDistance == Want && Draw->Value == Want, FString::Printf(TEXT("ползунок Q9 → буфер DrawDistance=%d (ждали %d)"), State.DrawDistance, Want));
	}
	CloseOptions(/*bApply*/ true);
	Check(!IsOptionsShown(), TEXT("окно закрыто по «ОК»"));
	if (Draw)
	{
		const float ScaleAfter = CullScale();
		Check(FMath::IsNearlyEqual(ScaleAfter, Want / 100.f, 0.001f), FString::Printf(TEXT("«ОК»: asda.Cull.Scale = %.3f (ждали %.2f)"), ScaleAfter, Want / 100.f));
		Check(FMath::IsNearlyEqual(Profile->DrawDistanceScale, Want / 100.f, 0.001f),
			FString::Printf(TEXT("«ОК»: профиль DrawDistanceScale = %.3f (ждали %.2f)"), Profile->DrawDistanceScale, Want / 100.f));
	}

	// 5. Откат прежнего значения тем же общим путём (окно и dev-меню пишут через него же).
	if (ProfileBefore < 0.f) { Vid->ClearRenderOverride(TEXT("asda.Cull.Scale"), /*bSave*/ true); }
	else                     { Vid->SetRenderOverride(TEXT("asda.Cull.Scale"), FString::SanitizeFloat(ProfileBefore), /*bSave*/ true); }
	Check(FMath::IsNearlyEqual(CullScale(), ScaleBefore, 0.001f) && (ProfileBefore < 0.f ? Profile->DrawDistanceScale < 0.f : FMath::IsNearlyEqual(Profile->DrawDistanceScale, ProfileBefore, 0.001f)),
		FString::Printf(TEXT("откат: asda.Cull.Scale = %.3f (было %.3f), профиль %.3f (было %.3f)"), CullScale(), ScaleBefore, Profile->DrawDistanceScale, ProfileBefore));
	Check(KeySandbox.End(), TEXT("файл раскладки владельца system/Shortcutkey.ini не тронут («ОК» писал в песочницу)"));

	AsdaSelfTestKit::Finish(W, kCmd, KitArgs.bQuit, LogAsdaSysOpt, kPrefix, Ok, Fail, TEXT(""), /*bWarning*/ true);
#else
	(void)Args;
#endif // ASDA_DEV_CONSOLE
}

// ═══════════════════ ★ 18.10.2026 САМОТЕСТ СТРОКИ «ЦИФРЫ УРОНА» `asda.SysOpt.SelfTestDmgText [quit] [shot=<папка>]` ═══════════════════
//   [ПОРТ] Проверка ПУТЁМ ИГРОКА (закон `ui-check-game-path-geometry`): окно настроек открывается штатно, вкладка «Настройки игры» и
//   под-страница «Отображение имён» выбираются НАСТОЯЩИМИ кликами Slate (`UAsdaWindowSubsystem::SimulateClickAt`), список — кликом по
//   его телу и по строке раскрытого списка, «Применить»/«Отмена»/«ОК» — кликами по кнопкам окна. Геометрия — живая (`GetCachedGeometry`
//   после отрисовки). Офлайн (`-AsdaNoNet`): «ОК» шлёт 4070 — старт через `RefuseStart` (окно защиты на все опкоды).
//   Проверки:
//     A. строка I1 в ассете: подпись = loc 990357 языка сессии, пунктов 3 = loc 990358…990360 в порядке значений 0/1/2,
//        выбранный пункт = поле профиля при открытии;
//     B. геометрия на экране: каждый узел строки видим и отрисован, строка внутри страницы, НЕ пересекается с видимыми строками
//        страницы, подпись и подпись выбранного пункта влезают (желаемая ширина ≤ отведённой; у пункта — до стрелки), пункты
//        раскрытого списка влезают в строки списка;
//     C. 0 → 1 → 2: выбор меняет только буфер окна; «Применить» → поле профиля, `GetDamageTextMode`/`ShouldShowDamageText` — сразу,
//        и строка `DamageTextMode=N` в GameUserSettings.ini;
//     D. «Отмена» откатывает буфер (профиль не меняется); повторное открытие показывает сохранённое; «ОК» применяет и закрывает;
//     E. исходный режим профиля возвращается.
//   Кадры (аргумент `shot=<папка>`): закрытый список, раскрытый список, повторное открытие — `<папка>/sysopt_dmgtext_<язык>_<кадр>.png`.
//   Итог — `САМОТЕСТ-ЦИФРЫ-УРОНА ИТОГ: ok=N fail=M` (ловит `asda.SelfTest.Batch`), при `quit` — выход.
void UAsdaSystemOptionSubsystem::RunDmgTextSelfTest(const TArray<FString>& Args)
{
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping тела нет
	static const TCHAR* const kCmd = TEXT("asda.SysOpt.SelfTestDmgText");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-ЦИФРЫ-УРОНА");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (AsdaSelfTestKit::RefuseStart(this, kCmd, KitArgs)) { return; }
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!W || !WM || !UAsdaGameUserSettings::Get())
	{
		AsdaSelfTestKit::Refused(this, kCmd, KitArgs.bQuit, TEXT("нет мира, подсистемы окон или профиля игрока"));
		return;
	}

	// Состояние прогона (живёт в шагах планировщика).
	struct FCtx
	{
		int32 Ok = 0;                          // пройдено
		int32 Fail = 0;                        // провалено
		int32 OrigMode = 1;                    // режим профиля до прогона (вернуть в конце)
		int32 Applied = 1;                     // режим, последним применённый в профиль
		FString ShotDir;                       // папка кадров (пусто — без кадров)
		FString Lang;                          // язык сессии (метка кадров и журнала)
		TArray<TFunction<void()>> Steps;       // шаги — по одному за тик планировщика
		int32 Next = 0;                        // следующий шаг
		FAsdaKeybindTestSandbox KeySandbox;    // ★ 26.10.2026: «Применить»/«ОК» пишут раскладку только в песочницу
	};
	TSharedRef<FCtx> C = MakeShared<FCtx>();
	{
		FString Why;
		if (!C->KeySandbox.Begin(GetKeybinds(), TEXT("dmgtext"), Why)) { AsdaSelfTestKit::Refused(this, kCmd, KitArgs.bQuit, *Why); return; }
	}
	C->OrigMode = FMath::Clamp(UAsdaGameUserSettings::Get()->DamageTextMode, 0, 2);
	C->Applied = C->OrigMode;
	C->Lang = FAsdaLang::Code(FAsdaLang::Current());
	for (const FString& P : KitArgs.Pos) { if (P.StartsWith(TEXT("shot="))) { C->ShotDir = P.Mid(5); } }

	const FString CmbName = SysOptExtra::NodeName(TEXT("I1"), TEXT("CMB"));   // список (движковый узел; класс списка его гасит)
	const FString TxtName = SysOptExtra::NodeName(TEXT("I1"), TEXT("TXT"));   // подпись строки
	const FString RowName = SysOptExtra::NodeName(TEXT("I1"), TEXT("grp"));   // корень строки
	const FString BodyName = TEXT("Bk_ComboBox_9502_0");                        // тело списка (план 0)
	const FString ArrowName = TEXT("Bk_ComboBox_9502_1");                       // стрелка (план 1)
	const FString ValueName = TEXT("Bk_ComboBox_9502_txt");                     // подпись выбранного пункта
	const FString PageName = TEXT("SYSTEM_Op_INT_2_grp");                       // под-страница 70250 «Отображение имён»

	auto Check = [C](bool bPass, const FString& What)
	{
		if (bPass) { ++C->Ok; } else { ++C->Fail; }
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ЦИФРЫ-УРОНА [%s] %s %s"), *C->Lang, bPass ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
	};
	auto Node = [this](const FString& N) -> UWidget* { return OptWidget ? OptWidget->GetWidgetFromName(FName(*N)) : nullptr; };
	auto Rect = [](const UWidget* Wd) -> FBox2D
	{
		FVector2D Mn, Mx;
		return (Wd && AsdaUi::WidgetAbsRect(Wd, Mn, Mx)) ? FBox2D(Mn, Mx) : FBox2D(ForceInit);
	};
	// Видимость по цепочке родителей (свёрнутый/скрытый предок прячет узел).
	auto Shown = [](const UWidget* Wd) -> bool
	{
		for (const UWidget* P = Wd; P; P = P->GetParent())
		{
			const ESlateVisibility V = P->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
		}
		return Wd != nullptr;
	};
	auto Overlap = [](const FBox2D& A, const FBox2D& B) -> bool
	{
		return A.bIsValid && B.bIsValid && A.Min.X < B.Max.X - 0.5f && B.Min.X < A.Max.X - 0.5f && A.Min.Y < B.Max.Y - 0.5f && B.Min.Y < A.Max.Y - 0.5f;
	};
	auto Desc = [Rect](const UWidget* Wd) -> FString
	{
		if (!Wd) { return TEXT("НЕТ УЗЛА"); }
		const FBox2D R = Rect(Wd);
		FString S = FString::Printf(TEXT("%s: экран (%.1f,%.1f)-(%.1f,%.1f) %.1f×%.1f вид=%s"), *Wd->GetName(), R.Min.X, R.Min.Y, R.Max.X, R.Max.Y,
			R.Max.X - R.Min.X, R.Max.Y - R.Min.Y, *StaticEnum<ESlateVisibility>()->GetNameStringByValue((int64)Wd->GetVisibility()));
		if (const UTextBlock* T = Cast<UTextBlock>(Wd))
		{
			const FVector2D Ds = T->GetDesiredSize();
			const FVector2D Lc = T->GetCachedGeometry().GetLocalSize();
			S += FString::Printf(TEXT(" текст«%s» желаемо %.1f×%.1f отведено %.1f×%.1f перенос=%d обрезка=%s"), *T->GetText().ToString(), Ds.X, Ds.Y,
				Lc.X, Lc.Y, T->GetAutoWrapText() ? 1 : 0, *StaticEnum<EWidgetClipping>()->GetNameStringByValue((int64)T->GetClipping()));
		}
		return S;
	};
	auto Combo = [this]() -> UAsdaJuiComboBox*
	{
		for (const FAsdaOptExtraCombo& X : ExtraCombos) { if (X.Key == SysOptExtra::I1_DmgText) { return X.Combo; } }
		return nullptr;
	};
	auto ClickW = [WM, Rect, Check](const UWidget* Wd, const TCHAR* What)
	{
		const FBox2D R = Rect(Wd);
		if (!R.bIsValid) { Check(false, FString::Printf(TEXT("клик по %s: нет живой геометрии"), What)); return; }
		WM->SimulateClickAt(R.GetCenter(), /*HoldFrames*/ 3);
	};
	auto Shot = [C](const TCHAR* Name)
	{
		if (C->ShotDir.IsEmpty()) { return; }
		const FString F = FPaths::Combine(C->ShotDir, FString::Printf(TEXT("sysopt_dmgtext_%s_%s"), *C->Lang, Name));
		FScreenshotRequest::RequestScreenshot(F, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ЦИФРЫ-УРОНА [%s] кадр: %s.png"), *C->Lang, *F);
	};
	// Подпись выбранного пункта влезает до стрелки (в экранных точках: желаемая ширина × масштаб строки).
	auto ValueFits = [Node, Rect, ValueName, ArrowName](FString& Out) -> bool
	{
		const UTextBlock* V = Cast<UTextBlock>(Node(ValueName));
		const FBox2D RV = Rect(V), RA = Rect(Node(ArrowName));
		if (!V || !RV.bIsValid || !RA.bIsValid) { Out = TEXT("нет подписи/стрелки"); return false; }
		const float Scale = (RV.Max.X - RV.Min.X) / FMath::Max(1.f, (float)V->GetCachedGeometry().GetLocalSize().X);
		const float Right = RV.Min.X + V->GetDesiredSize().X * Scale;
		Out = FString::Printf(TEXT("«%s»: правый край текста %.1f, стрелка с %.1f (масштаб %.3f)"), *V->GetText().ToString(), Right, RA.Min.X, Scale);
		return Right <= RA.Min.X + 0.5f;
	};
	// Режим в профиле, ключах чтения и в GameUserSettings.ini на диске.
	auto CheckApplied = [C, Check](int32 Mode, const TCHAR* Via)
	{
		const UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
		const int32 Got = P ? P->DamageTextMode : -1;
		const bool bMine = UAsdaWorldSubsystem::ShouldShowDamageText(true);
		const bool bOther = UAsdaWorldSubsystem::ShouldShowDamageText(false);
		const bool bWantMine = (Mode != 2), bWantOther = (Mode == 1);
		Check(Got == Mode && UAsdaWorldSubsystem::GetDamageTextMode() == Mode && bMine == bWantMine && bOther == bWantOther,
			FString::Printf(TEXT("%s: профиль DamageTextMode=%d, GetDamageTextMode=%d, своё=%d чужое=%d (ждали %d, %d/%d) — сразу, без перезапуска"),
				Via, Got, UAsdaWorldSubsystem::GetDamageTextMode(), bMine ? 1 : 0, bOther ? 1 : 0, Mode, bWantMine ? 1 : 0, bWantOther ? 1 : 0));
		// `GGameUserSettingsIni` — ключ ветки («GameUserSettings»), файл на диске — `IniPath` этой ветки.
		const FConfigBranch* Br = GConfig ? GConfig->FindBranch(FName(*GGameUserSettingsIni), GGameUserSettingsIni) : nullptr;
		const FString IniFile = Br ? FPaths::ConvertRelativePathToFull(Br->IniPath) : FString();
		FString Ini;
		const bool bRead = !IniFile.IsEmpty() && FFileHelper::LoadFileToString(Ini, *IniFile);
		const FString Want = FString::Printf(TEXT("DamageTextMode=%d"), Mode);
		const bool bHas = bRead && Ini.Contains(Want);
		Check(bHas, FString::Printf(TEXT("%s: в %s строка «%s» %s"), Via, IniFile.IsEmpty() ? TEXT("<нет пути ветки>") : *IniFile, *Want,
			bHas ? TEXT("есть") : TEXT("НЕТ")));
		C->Applied = Mode;
	};
	// Выбор пункта списка путём игрока: клик по телу (раскрыть) → клик по строке пункта (следующим шагом).
	auto PickSteps = [C, Node, ClickW, Check, Combo, CmbName, BodyName](int32 Idx)
	{
		C->Steps.Add([Node, ClickW, BodyName]() { ClickW(Node(BodyName), TEXT("тело списка «Цифры урона»")); });
		C->Steps.Add([Node, ClickW, Check, Combo, CmbName, Idx]()
		{
			UAsdaJuiComboBox* Cb = Combo();
			Check(Cb && Cb->IsOpen(), FString::Printf(TEXT("клик по телу — список раскрыт (для пункта %d)"), Idx));
			ClickW(Node(FString::Printf(TEXT("%s_Row%d"), *CmbName, Idx)), TEXT("строка раскрытого списка"));
		});
	};

	// ── A. Окно, вкладка и страница — путём игрока ──
	C->Steps.Add([this, C, Check, Node, ClickW, Combo, TxtName]()
	{
		if (IsOptionsShown()) { CloseOptions(/*bApply*/ false); }
		ShowOptions();
		Check(IsOptionsShown() && OptWidget != nullptr, TEXT("окно 69500 открыто"));
		UAsdaJuiComboBox* Cb = Combo();
		Check(Cb != nullptr, TEXT("список «Цифры урона» (SYSTEM_OX_I1_CMB) подключён"));
		const UTextBlock* T = Cast<UTextBlock>(Node(TxtName));
		const FString WantLabel = LocUi(SysOptLoc::kDmgText);
		Check(T && !WantLabel.IsEmpty() && T->GetText().ToString() == WantLabel, FString::Printf(TEXT("подпись строки «%s» (loc 990357 «%s»)"),
			T ? *T->GetText().ToString() : TEXT("—"), *WantLabel));
		if (Cb)
		{
			const int32 L[3] = { SysOptLoc::kDmgMine, SysOptLoc::kDmgAll, SysOptLoc::kDmgOff };
			bool bItems = (Cb->NumItems() == 3);
			FString Got;
			for (int32 i = 0; i < Cb->NumItems(); ++i)
			{
				Got += FString::Printf(TEXT("«%s» "), *Cb->GetItemLabel(i));
				if (i < 3) { bItems &= (!LocUi(L[i]).IsEmpty() && Cb->GetItemLabel(i) == LocUi(L[i])); }
			}
			Check(bItems, FString::Printf(TEXT("пунктов %d: %s(ждали loc 990358/990359/990360 в порядке 0/1/2)"), Cb->NumItems(), *Got));
			Check(Cb->GetSelectedIndex() == C->OrigMode, FString::Printf(TEXT("при открытии выбран пункт %d = режим профиля %d"), Cb->GetSelectedIndex(), C->OrigMode));
		}
	});
	C->Steps.Add([]() {});   // кадр на раскладку только что показанного окна (до него у кнопок нет геометрии)
	// Сначала уходим на «Настройки системы» — иначе клик по уже выбранной вкладке «Настройки игры» ничего бы не доказывал.
	C->Steps.Add([this, ClickW]() { ClickW(OptTabBtn[1], TEXT("вкладка «Настройки системы»")); });
	C->Steps.Add([this, Check, ClickW]()
	{
		Check(CurrentTab == 1, FString::Printf(TEXT("клик по вкладке «Настройки системы» → вкладка %d (ждали 1)"), CurrentTab));
		ClickW(OptTabBtn[0], TEXT("вкладка «Настройки игры»"));
	});
	C->Steps.Add([this, Check, ClickW]()
	{
		Check(CurrentTab == 0, FString::Printf(TEXT("клик по вкладке «Настройки игры» → вкладка %d (ждали 0)"), CurrentTab));
		if (CurrentTab != 0) { SelectTab(0); }
		ClickW(OptListBtn[PageSlot(0, 1)], TEXT("ячейка «Отображение имён»"));
	});
	C->Steps.Add([this, Check]()
	{
		Check(CurrentPage[0] == 1, FString::Printf(TEXT("клик по ячейке → под-страница %d (ждали 1)"), CurrentPage[0]));
		if (CurrentPage[0] != 1) { SelectSubPage(0, 1); }
	});
	C->Steps.Add([]() {});   // кадр на раскладку

	// ── B. Геометрия на экране ──
	C->Steps.Add([Check, Node, Rect, Shown, Overlap, Desc, ValueFits, Shot, RowName, PageName, CmbName, TxtName, BodyName, ArrowName, ValueName]()
	{
		UWidget* Page = Node(PageName);
		UWidget* Row = Node(RowName);
		const FBox2D RP = Rect(Page), RR = Rect(Row);
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ЦИФРЫ-УРОНА ГЕОМЕТРИЯ страница %s"), *Desc(Page));
		const FString RowNodes[6] = { RowName, BodyName, ArrowName, CmbName, ValueName, TxtName };
		for (const FString& N : RowNodes)
		{
			UWidget* Wd = Node(N);
			const bool bDrawn = Rect(Wd).bIsValid;
			UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ЦИФРЫ-УРОНА ГЕОМЕТРИЯ строка  %s"), *Desc(Wd));
			// Движковый `UComboBoxString` класс списка гасит сам (рисует планы) — для него видимость не требуется.
			if (N != CmbName) { Check(Wd && Shown(Wd) && bDrawn, FString::Printf(TEXT("%s видим и отрисован"), *N)); }
		}
		Check(RR.bIsValid && RP.bIsValid && RR.Min.X >= RP.Min.X - 0.5f && RR.Min.Y >= RP.Min.Y - 0.5f && RR.Max.X <= RP.Max.X + 0.5f && RR.Max.Y <= RP.Max.Y + 0.5f,
			FString::Printf(TEXT("строка внутри страницы: (%.1f,%.1f)-(%.1f,%.1f) в (%.1f,%.1f)-(%.1f,%.1f)"), RR.Min.X, RR.Min.Y, RR.Max.X, RR.Max.Y,
				RP.Min.X, RP.Min.Y, RP.Max.X, RP.Max.Y));
		// Соседи — видимые строки той же страницы (канвасы-дети); фон-рамка страницы (планы 296_*) — не соседи.
		int32 Neighbours = 0;
		if (UPanelWidget* PP = Cast<UPanelWidget>(Page))
		{
			for (int32 i = 0; i < PP->GetChildrenCount(); ++i)
			{
				UWidget* K = PP->GetChildAt(i);
				if (!K || K == Row || !Cast<UCanvasPanel>(K) || !Shown(K)) { continue; }
				++Neighbours;
				UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ЦИФРЫ-УРОНА ГЕОМЕТРИЯ сосед  %s (ребёнок %d)"), *Desc(K), i);
				if (UPanelWidget* KP = Cast<UPanelWidget>(K))
				{
					for (int32 j = 0; j < KP->GetChildrenCount(); ++j)
					{
						if (Cast<UTextBlock>(KP->GetChildAt(j))) { UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ЦИФРЫ-УРОНА ГЕОМЕТРИЯ         %s"), *Desc(KP->GetChildAt(j))); }
					}
				}
				Check(!Overlap(RR, Rect(K)), FString::Printf(TEXT("строка не пересекается с соседом %s"), *K->GetName()));
			}
		}
		Check(Neighbours == 3, FString::Printf(TEXT("видимых строк-соседей на странице %d (ждали 3: ОЗ монстра / моё имя и ОЗ / имена игроков)"), Neighbours));
		const UTextBlock* T = Cast<UTextBlock>(Node(TxtName));
		Check(T && T->GetDesiredSize().X <= T->GetCachedGeometry().GetLocalSize().X + 0.5f && !T->GetAutoWrapText(),
			FString::Printf(TEXT("подпись строки влезает без переноса: желаемо %.1f, отведено %.1f"), T ? T->GetDesiredSize().X : -1.f,
				T ? T->GetCachedGeometry().GetLocalSize().X : -1.f));
		FString Why;
		const bool bFits = ValueFits(Why);
		Check(bFits, FString::Printf(TEXT("подпись выбранного пункта влезает до стрелки: %s"), *Why));
		Shot(TEXT("closed"));
	});

	// ── C. 0 → 1 → 2: выбор → буфер; «Применить» → профиль и чтение сразу ──
	for (int32 Mode = 0; Mode < 3; ++Mode)
	{
		C->Steps.Add([Node, ClickW, BodyName]() { ClickW(Node(BodyName), TEXT("тело списка «Цифры урона»")); });
		C->Steps.Add([Check, Combo, Node, Rect, Desc, Shot, CmbName, Mode]()
		{
			UAsdaJuiComboBox* Cb = Combo();
			Check(Cb && Cb->IsOpen(), FString::Printf(TEXT("клик по телу — список раскрыт (для пункта %d)"), Mode));
			if (Mode != 0) { return; }
			// Строки раскрытого списка: текст влезает в строку (без обрезки).
			for (int32 i = 0; i < 3; ++i)
			{
				const UTextBlock* RowT = Cast<UTextBlock>(Node(FString::Printf(TEXT("%s_Row%d"), *CmbName, i)));
				const UWidget* RowBg = Node(FString::Printf(TEXT("%s_ListBg"), *CmbName));   // подложка всего списка (плиты строк видны не всегда)
				UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ЦИФРЫ-УРОНА ГЕОМЕТРИЯ список %s | %s"), *Desc(RowT), *Desc(RowBg));
				const FBox2D RT = Rect(RowT), RB = Rect(RowBg);
				const float Scale = RowT ? (RT.Max.X - RT.Min.X) / FMath::Max(1.f, (float)RowT->GetCachedGeometry().GetLocalSize().X) : 1.f;
				const float Right = RowT ? RT.Min.X + RowT->GetDesiredSize().X * Scale : -1.f;
				Check(RowT && RT.bIsValid && RB.bIsValid && Right <= RB.Max.X + 0.5f,
					FString::Printf(TEXT("пункт %d списка влезает в список: правый край текста %.1f, подложка до %.1f"), i, Right, RB.Max.X));
			}
			Shot(TEXT("open"));
		});
		C->Steps.Add([Node, ClickW, CmbName, Mode]() { ClickW(Node(FString::Printf(TEXT("%s_Row%d"), *CmbName, Mode)), TEXT("строка раскрытого списка")); });
		C->Steps.Add([this, C, Check, Combo, ValueFits, ClickW, Mode]()
		{
			UAsdaJuiComboBox* Cb = Combo();
			const UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
			Check(State.DamageTextMode == Mode && Cb && Cb->GetSelectedIndex() == Mode && !Cb->IsOpen(),
				FString::Printf(TEXT("клик по пункту %d: буфер окна %d, выбран пункт %d, список закрыт"), Mode, State.DamageTextMode, Cb ? Cb->GetSelectedIndex() : -1));
			Check(P && P->DamageTextMode == C->Applied, FString::Printf(TEXT("до «Применить» профиль не тронут: %d (ждали %d)"), P ? P->DamageTextMode : -1, C->Applied));
			FString Why;
			const bool bFits = ValueFits(Why);
			Check(bFits, FString::Printf(TEXT("подпись пункта %d влезает до стрелки: %s"), Mode, *Why));
			ClickW(OptBtnApply, TEXT("кнопка «Применить»"));
		});
		C->Steps.Add([this, Check, CheckApplied, Mode]()
		{
			Check(IsOptionsShown(), TEXT("«Применить»: окно осталось открытым"));
			CheckApplied(Mode, TEXT("«Применить»"));
		});
	}

	// ── D. «Отмена», повторное открытие, «ОК» ──
	PickSteps(0);
	C->Steps.Add([this, Check, ClickW]()
	{
		Check(State.DamageTextMode == 0, FString::Printf(TEXT("перед «Отмена» в буфере 0 (есть %d)"), State.DamageTextMode));
		ClickW(OptBtnCancel, TEXT("кнопка «Отмена»"));
	});
	C->Steps.Add([this, C, Check]()
	{
		const UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
		Check(!IsOptionsShown(), TEXT("«Отмена»: окно закрыто"));
		Check(P && P->DamageTextMode == C->Applied && UAsdaWorldSubsystem::GetDamageTextMode() == C->Applied && State.DamageTextMode == C->Applied,
			FString::Printf(TEXT("«Отмена»: профиль %d, чтение %d, буфер %d (ждали %d — выбор отброшен)"), P ? P->DamageTextMode : -1,
				UAsdaWorldSubsystem::GetDamageTextMode(), State.DamageTextMode, C->Applied));
		ShowOptions();   // повторное открытие — та же функция, что кнопка «Настройки» ESC-меню
	});
	C->Steps.Add([this, C, Check, Combo, Node, Shown, Rect, Shot, RowName, ValueName]()
	{
		UAsdaJuiComboBox* Cb = Combo();
		const UTextBlock* V = Cast<UTextBlock>(Node(ValueName));
		Check(Cb && Cb->GetSelectedIndex() == C->Applied && V && V->GetText().ToString() == Cb->GetItemLabel(C->Applied),
			FString::Printf(TEXT("повторное открытие: выбран пункт %d «%s» (ждали %d «%s»)"), Cb ? Cb->GetSelectedIndex() : -1,
				V ? *V->GetText().ToString() : TEXT("—"), C->Applied, Cb ? *Cb->GetItemLabel(C->Applied) : TEXT("—")));
		Check(CurrentTab == 0 && CurrentPage[0] == 1 && Shown(Node(RowName)) && Rect(Node(RowName)).bIsValid,
			FString::Printf(TEXT("повторное открытие: вкладка %d, страница %d, строка видна"), CurrentTab, CurrentPage[0]));
		Shot(TEXT("reopen"));
	});
	PickSteps(1);
	C->Steps.Add([this, ClickW]() { ClickW(OptBtnOk, TEXT("кнопка «ОК»")); });
	C->Steps.Add([this, Check, CheckApplied]()
	{
		Check(!IsOptionsShown(), TEXT("«ОК»: окно закрыто"));
		CheckApplied(1, TEXT("«ОК»"));
		ShowOptions();
	});
	C->Steps.Add([this, Check, Combo]()
	{
		UAsdaJuiComboBox* Cb = Combo();
		Check(Cb && Cb->GetSelectedIndex() == 1, FString::Printf(TEXT("после «ОК» окно показывает пункт %d (ждали 1)"), Cb ? Cb->GetSelectedIndex() : -1));
		CloseOptions(/*bApply*/ false);
	});

	// ── E. Возврат исходного режима ──
	C->Steps.Add([C, Check]()
	{
		if (UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get())
		{
			P->DamageTextMode = C->OrigMode;
			P->SaveSettings();
		}
		Check(UAsdaWorldSubsystem::GetDamageTextMode() == C->OrigMode, FString::Printf(TEXT("исходный режим %d возвращён"), C->OrigMode));
	});

	TWeakObjectPtr<UAsdaSystemOptionSubsystem> WeakThis(this);
	TWeakObjectPtr<UWorld> WW(W);
	const bool bQuit = KitArgs.bQuit;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, WW, C, bQuit](float) -> bool
	{
		if (!WeakThis.IsValid() || !WW.IsValid())
		{
			UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ЦИФРЫ-УРОНА ИТОГ: ok=%d fail=%d (ПРЕРВАН: мир или подсистема ушли)"), C->Ok, C->Fail + 1);
			return false;
		}
		if (C->Next < C->Steps.Num()) { C->Steps[C->Next++](); return true; }
		// ★ 26.10.2026: песочница раскладки снята, файл владельца сверен байт в байт.
		const bool bOwnerKept = C->KeySandbox.End();
		if (bOwnerKept) { ++C->Ok; } else { ++C->Fail; }
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ЦИФРЫ-УРОНА [%s] %s файл раскладки владельца system/Shortcutkey.ini не тронут (песочница)"), *C->Lang,
			bOwnerKept ? TEXT("✔") : TEXT("✘ ПРОВАЛ"));
		AsdaSelfTestKit::Finish(WW.Get(), kCmd, bQuit, LogAsdaSysOpt, kPrefix, C->Ok, C->Fail,
			*FString::Printf(TEXT(" [язык %s]"), *C->Lang), /*bWarning*/ true);
		return false;
	}), 0.3f);
	UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ЦИФРЫ-УРОНА [%s]: запуск, шагов %d, режим профиля %d, кадры: %s"), *C->Lang, C->Steps.Num(), C->OrigMode,
		C->ShotDir.IsEmpty() ? TEXT("нет") : *C->ShotDir);
#else
	(void)Args;
#endif // ASDA_DEV_CONSOLE
}

// ═══════════════════ ★ 19.10.2026 САМОТЕСТ СТРОК МЫШИ/КАМЕРЫ `asda.SysOpt.SelfTestMouse [quit] [shot=<папка>]` ═══════════════════
//   [ПОРТ] Проверка ПУТЁМ ИГРОКА (закон `ui-check-game-path-geometry`) строк I2 «Чувствительность мыши» и I3 «Чувствительность
//   поворота камеры» страницы «Разное»: окно открывается штатно, вкладка и под-страница — настоящими кликами Slate
//   (`UAsdaWindowSubsystem::SimulateClickAt`), стрелки полос и «Применить»/«Отмена» — тоже кликами. Геометрия — живая.
//   Офлайн (`-AsdaNoNet`): «Применить» шлёт 4070 — старт через `RefuseStart`.
//   Проверки:
//     A. обе полосы собраны (жёлоб, стрелки, бегунок), подписи = loc 990361/990362 языка сессии, бегунки = поля профиля;
//     B. геометрия: узлы строк видимы и отрисованы, строки внутри страницы, НЕ пересекаются ни с видимыми строками страницы, ни
//        друг с другом, видимых строк-флажков страницы 10, подписи влезают без переноса;
//     C. клик по стрелке «+» камеры и «−» мыши: меняется только буфер (±6, бегунок сдвинулся), профиль не тронут; «Применить» →
//        поля профиля, `GetCameraTurnScale` сразу, строки в GameUserSettings.ini; скорость указателя Windows НЕ изменилась
//        (прогон без окна — системная скорость не наша, `FAsdaCursorSpeed`);
//     D. «Отмена» откатывает буфер, профиль не меняется;
//     E. таблица уровней Windows (`LevelForPercent`) на опорных точках; исходные значения профиля возвращаются.
//   Итог — `САМОТЕСТ-МЫШЬ ИТОГ: ok=N fail=M` (ловит `asda.SelfTest.Batch`), при `quit` — выход.
void UAsdaSystemOptionSubsystem::RunMouseSelfTest(const TArray<FString>& Args)
{
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping тела нет
	static const TCHAR* const kCmd = TEXT("asda.SysOpt.SelfTestMouse");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-МЫШЬ");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (AsdaSelfTestKit::RefuseStart(this, kCmd, KitArgs)) { return; }
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!W || !WM || !UAsdaGameUserSettings::Get())
	{
		AsdaSelfTestKit::Refused(this, kCmd, KitArgs.bQuit, TEXT("нет мира, подсистемы окон или профиля игрока"));
		return;
	}

	// Состояние прогона (живёт в шагах планировщика).
	struct FCtx
	{
		int32 Ok = 0;                          // пройдено
		int32 Fail = 0;                        // провалено
		int32 OrigCursor = 100;                // поля профиля до прогона (вернуть в конце)
		int32 OrigCamera = 100;
		int32 WinLevel = 0;                    // скорость указателя Windows до прогона (сверка «не тронута»)
		int32 BufCursor = 100;                 // буфер окна после кликов по стрелкам
		int32 BufCamera = 100;
		float ThumbX[2] = { 0.f, 0.f };        // левый край бегунков I2/I3 до кликов (экран)
		FString ShotDir;                       // папка кадров (пусто — без кадров)
		FString Lang;                          // язык сессии (метка кадров и журнала)
		TArray<TFunction<void()>> Steps;       // шаги — по одному за тик планировщика
		int32 Next = 0;                        // следующий шаг
		FAsdaKeybindTestSandbox KeySandbox;    // ★ 26.10.2026: «Применить»/«ОК» пишут раскладку только в песочницу
	};
	TSharedRef<FCtx> C = MakeShared<FCtx>();
	{
		FString Why;
		if (!C->KeySandbox.Begin(GetKeybinds(), TEXT("mouse"), Why)) { AsdaSelfTestKit::Refused(this, kCmd, KitArgs.bQuit, *Why); return; }
	}
	C->OrigCursor = UAsdaGameUserSettings::Get()->CursorSpeedPercent;
	C->OrigCamera = UAsdaGameUserSettings::Get()->CameraTurnPercent;
	C->WinLevel = FAsdaCursorSpeed::ReadWindowsLevel();
	C->Lang = FAsdaLang::Code(FAsdaLang::Current());
	for (const FString& P : KitArgs.Pos) { if (P.StartsWith(TEXT("shot="))) { C->ShotDir = P.Mid(5); } }

	const TCHAR* const Tags[2] = { TEXT("I2"), TEXT("I3") };
	const int32 Ids[2] = { SysOptExtra::kIdBase + SysOptExtra::I2_Cursor, SysOptExtra::kIdBase + SysOptExtra::I3_Camera };
	const int32 Locs[2] = { SysOptLoc::kCursorSpeed, SysOptLoc::kCameraTurn };
	const FString PageName = TEXT("SYSTEM_Op_INT_3_grp");   // под-страница 70350 «Разное»

	auto Check = [C](bool bPass, const FString& What)
	{
		if (bPass) { ++C->Ok; } else { ++C->Fail; }
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-МЫШЬ [%s] %s %s"), *C->Lang, bPass ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
	};
	auto Node = [this](const FString& N) -> UWidget* { return OptWidget ? OptWidget->GetWidgetFromName(FName(*N)) : nullptr; };
	auto Rect = [](const UWidget* Wd) -> FBox2D
	{
		FVector2D Mn, Mx;
		return (Wd && AsdaUi::WidgetAbsRect(Wd, Mn, Mx)) ? FBox2D(Mn, Mx) : FBox2D(ForceInit);
	};
	auto Shown = [](const UWidget* Wd) -> bool
	{
		for (const UWidget* P = Wd; P; P = P->GetParent())
		{
			const ESlateVisibility V = P->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
		}
		return Wd != nullptr;
	};
	auto Overlap = [](const FBox2D& A, const FBox2D& B) -> bool
	{
		return A.bIsValid && B.bIsValid && A.Min.X < B.Max.X - 0.5f && B.Min.X < A.Max.X - 0.5f && A.Min.Y < B.Max.Y - 0.5f && B.Min.Y < A.Max.Y - 0.5f;
	};
	auto Desc = [Rect](const UWidget* Wd) -> FString
	{
		if (!Wd) { return TEXT("НЕТ УЗЛА"); }
		const FBox2D R = Rect(Wd);
		FString S = FString::Printf(TEXT("%s: экран (%.1f,%.1f)-(%.1f,%.1f) %.1f×%.1f"), *Wd->GetName(), R.Min.X, R.Min.Y, R.Max.X, R.Max.Y,
			R.Max.X - R.Min.X, R.Max.Y - R.Min.Y);
		if (const UTextBlock* T = Cast<UTextBlock>(Wd))
		{
			S += FString::Printf(TEXT(" текст«%s» желаемо %.1f отведено %.1f"), *T->GetText().ToString(), T->GetDesiredSize().X,
				T->GetCachedGeometry().GetLocalSize().X);
		}
		return S;
	};
	auto ClickW = [WM, Rect, Check](const UWidget* Wd, const TCHAR* What)
	{
		const FBox2D R = Rect(Wd);
		if (!R.bIsValid) { Check(false, FString::Printf(TEXT("клик по %s: нет живой геометрии"), What)); return; }
		WM->SimulateClickAt(R.GetCenter(), /*HoldFrames*/ 3);
	};
	auto Shot = [C](const TCHAR* Name)
	{
		if (C->ShotDir.IsEmpty()) { return; }
		const FString F = FPaths::Combine(C->ShotDir, FString::Printf(TEXT("sysopt_mouse_%s_%s"), *C->Lang, Name));
		FScreenshotRequest::RequestScreenshot(F, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-МЫШЬ [%s] кадр: %s.png"), *C->Lang, *F);
	};
	// Поля профиля, множитель камеры и строки ini на диске.
	auto CheckProfile = [Check](int32 WantCursor, int32 WantCamera, const TCHAR* Via)
	{
		const UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
		Check(P && P->CursorSpeedPercent == WantCursor && P->CameraTurnPercent == WantCamera
			&& FMath::IsNearlyEqual(UAsdaGameUserSettings::GetCameraTurnScale(), WantCamera / 100.f, 0.001f),
			FString::Printf(TEXT("%s: профиль мышь %d / камера %d, множитель камеры %.3f (ждали %d / %d)"), Via, P ? P->CursorSpeedPercent : -1,
				P ? P->CameraTurnPercent : -1, UAsdaGameUserSettings::GetCameraTurnScale(), WantCursor, WantCamera));
	};

	// ── A. Окно и полосы ──
	C->Steps.Add([this, C, Check, Node, Tags, Ids, Locs]()
	{
		if (IsOptionsShown()) { CloseOptions(/*bApply*/ false); }
		ShowOptions();
		Check(IsOptionsShown() && OptWidget != nullptr, TEXT("окно 69500 открыто"));
		const int32 Want[2] = { C->OrigCursor, C->OrigCamera };
		for (int32 i = 0; i < 2; ++i)
		{
			const FAsdaOptSlider* S = FindSlider(Ids[i]);
			Check(S && S->Track && S->Thumb && S->BtnLeft && S->BtnRight && S->Tab == 0 && S->Page == 2,
				FString::Printf(TEXT("полоса %s собрана (жёлоб, бегунок, стрелки), вкладка 0 страница 2"), Tags[i]));
			Check(S && S->Value == Want[i] && S->Min == UAsdaGameUserSettings::kMouseSensMin && S->Max == UAsdaGameUserSettings::kMouseSensMax,
				FString::Printf(TEXT("полоса %s: значение %d = поле профиля %d, диапазон %d…%d"), Tags[i], S ? S->Value : -1, Want[i], S ? S->Min : -1, S ? S->Max : -1));
			const UTextBlock* T = Cast<UTextBlock>(Node(SysOptExtra::NodeName(Tags[i], TEXT("TXT"))));
			const FString WantLabel = LocUi(Locs[i]);
			Check(T && !WantLabel.IsEmpty() && T->GetText().ToString() == WantLabel,
				FString::Printf(TEXT("подпись %s «%s» = loc %d «%s»"), Tags[i], T ? *T->GetText().ToString() : TEXT("—"), Locs[i], *WantLabel));
		}
	});
	C->Steps.Add([]() {});   // кадр на раскладку только что показанного окна
	C->Steps.Add([this, ClickW]() { ClickW(OptTabBtn[1], TEXT("вкладка «Настройки системы»")); });
	C->Steps.Add([this, Check, ClickW]()
	{
		Check(CurrentTab == 1, FString::Printf(TEXT("клик по вкладке «Настройки системы» → вкладка %d (ждали 1)"), CurrentTab));
		ClickW(OptTabBtn[0], TEXT("вкладка «Настройки игры»"));
	});
	C->Steps.Add([this, Check, ClickW]()
	{
		Check(CurrentTab == 0, FString::Printf(TEXT("клик по вкладке «Настройки игры» → вкладка %d (ждали 0)"), CurrentTab));
		if (CurrentTab != 0) { SelectTab(0); }
		ClickW(OptListBtn[PageSlot(0, 2)], TEXT("ячейка «Разное»"));
	});
	C->Steps.Add([this, Check]()
	{
		Check(CurrentPage[0] == 2, FString::Printf(TEXT("клик по ячейке → под-страница %d (ждали 2 «Разное»)"), CurrentPage[0]));
		if (CurrentPage[0] != 2) { SelectSubPage(0, 2); }
	});
	C->Steps.Add([]() {});   // кадр на раскладку

	// ── B. Геометрия ──
	C->Steps.Add([this, C, Check, Node, Rect, Shown, Overlap, Desc, Shot, Tags, Ids, PageName]()
	{
		UWidget* Page = Node(PageName);
		const FBox2D RP = Rect(Page);
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-МЫШЬ ГЕОМЕТРИЯ страница %s"), *Desc(Page));
		for (int32 i = 0; i < 2; ++i)
		{
			UWidget* Row = Node(SysOptExtra::NodeName(Tags[i], TEXT("grp")));
			const FBox2D RR = Rect(Row);
			TArray<UWidget*> Parts = { Row, Node(SysOptExtra::NodeName(Tags[i], TEXT("TXT"))) };
			if (const FAsdaOptSlider* S = FindSlider(Ids[i])) { Parts.Append({ S->Track, S->BtnLeft, S->BtnRight, S->Thumb }); }
			for (UWidget* Wd : Parts)
			{
				UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-МЫШЬ ГЕОМЕТРИЯ %s  %s"), Tags[i], *Desc(Wd));
				Check(Wd && Shown(Wd) && Rect(Wd).bIsValid, FString::Printf(TEXT("%s: %s видим и отрисован"), Tags[i], Wd ? *Wd->GetName() : TEXT("НЕТ УЗЛА")));
			}
			Check(RR.bIsValid && RP.bIsValid && RR.Min.X >= RP.Min.X - 0.5f && RR.Min.Y >= RP.Min.Y - 0.5f && RR.Max.X <= RP.Max.X + 0.5f && RR.Max.Y <= RP.Max.Y + 0.5f,
				FString::Printf(TEXT("строка %s внутри страницы: (%.1f,%.1f)-(%.1f,%.1f) в (%.1f,%.1f)-(%.1f,%.1f)"), Tags[i], RR.Min.X, RR.Min.Y, RR.Max.X, RR.Max.Y,
					RP.Min.X, RP.Min.Y, RP.Max.X, RP.Max.Y));
			int32 CheckRows = 0;
			if (UPanelWidget* PP = Cast<UPanelWidget>(Page))
			{
				for (int32 k = 0; k < PP->GetChildrenCount(); ++k)
				{
					UWidget* K = PP->GetChildAt(k);
					if (!K || K == Row || !Cast<UCanvasPanel>(K) || !Shown(K)) { continue; }
					if (K->GetName().StartsWith(TEXT("Box_Check_grp"))) { ++CheckRows; }
					Check(!Overlap(RR, Rect(K)), FString::Printf(TEXT("строка %s не пересекается с %s"), Tags[i], *K->GetName()));
				}
			}
			Check(CheckRows == 10, FString::Printf(TEXT("видимых строк-флажков на «Разном» %d (ждали 10)"), CheckRows));
			const UTextBlock* T = Cast<UTextBlock>(Node(SysOptExtra::NodeName(Tags[i], TEXT("TXT"))));
			Check(T && T->GetDesiredSize().X <= T->GetCachedGeometry().GetLocalSize().X + 0.5f && !T->GetAutoWrapText(),
				FString::Printf(TEXT("подпись %s влезает без переноса: желаемо %.1f, отведено %.1f"), Tags[i], T ? T->GetDesiredSize().X : -1.f,
					T ? T->GetCachedGeometry().GetLocalSize().X : -1.f));
			if (const FAsdaOptSlider* S = FindSlider(Ids[i])) { C->ThumbX[i] = Rect(S->Thumb).Min.X; }
		}
		Shot(TEXT("before"));
	});

	// ── C. Стрелки → буфер; «Применить» → профиль ──
	C->Steps.Add([this, ClickW, Ids]()
	{
		if (const FAsdaOptSlider* S = FindSlider(Ids[1])) { ClickW(S->BtnRight, TEXT("стрелка «+» камеры")); }
	});
	C->Steps.Add([this, ClickW, Ids]()
	{
		if (const FAsdaOptSlider* S = FindSlider(Ids[0])) { ClickW(S->BtnLeft, TEXT("стрелка «−» мыши")); }
	});
	C->Steps.Add([this, C, Check, Rect, CheckProfile, ClickW, Ids, Shot]()
	{
		const int32 WantCam = FMath::Clamp(C->OrigCamera + 6, UAsdaGameUserSettings::kMouseSensMin, UAsdaGameUserSettings::kMouseSensMax);
		const int32 WantCur = FMath::Clamp(C->OrigCursor - 6, UAsdaGameUserSettings::kMouseSensMin, UAsdaGameUserSettings::kMouseSensMax);
		Check(State.CameraTurn == WantCam && State.CursorSpeed == WantCur,
			FString::Printf(TEXT("клики по стрелкам: буфер камера %d, мышь %d (ждали %d / %d)"), State.CameraTurn, State.CursorSpeed, WantCam, WantCur));
		const FAsdaOptSlider* S0 = FindSlider(Ids[0]);
		const FAsdaOptSlider* S1 = FindSlider(Ids[1]);
		const float X0 = S0 ? Rect(S0->Thumb).Min.X : 0.f, X1 = S1 ? Rect(S1->Thumb).Min.X : 0.f;
		Check(S0 && S1 && X0 < C->ThumbX[0] && X1 > C->ThumbX[1],
			FString::Printf(TEXT("бегунки сдвинулись: мышь %.1f → %.1f (влево), камера %.1f → %.1f (вправо)"), C->ThumbX[0], X0, C->ThumbX[1], X1));
		CheckProfile(C->OrigCursor, C->OrigCamera, TEXT("до «Применить» профиль не тронут"));
		C->BufCursor = WantCur; C->BufCamera = WantCam;
		Shot(TEXT("changed"));
		ClickW(OptBtnApply, TEXT("кнопка «Применить»"));
	});
	C->Steps.Add([this, C, Check, CheckProfile]()
	{
		Check(IsOptionsShown(), TEXT("«Применить»: окно осталось открытым"));
		CheckProfile(C->BufCursor, C->BufCamera, TEXT("«Применить»"));
		const FConfigBranch* Br = GConfig ? GConfig->FindBranch(FName(*GGameUserSettingsIni), GGameUserSettingsIni) : nullptr;
		const FString IniFile = Br ? FPaths::ConvertRelativePathToFull(Br->IniPath) : FString();
		FString Ini;
		const bool bRead = !IniFile.IsEmpty() && FFileHelper::LoadFileToString(Ini, *IniFile);
		const FString W1 = FString::Printf(TEXT("CursorSpeedPercent=%d"), C->BufCursor), W2 = FString::Printf(TEXT("CameraTurnPercent=%d"), C->BufCamera);
		Check(bRead && Ini.Contains(W1) && Ini.Contains(W2), FString::Printf(TEXT("«Применить»: в %s строки «%s» и «%s» %s"), *IniFile, *W1, *W2,
			(bRead && Ini.Contains(W1) && Ini.Contains(W2)) ? TEXT("есть") : TEXT("НЕТ")));
		const int32 Lvl = FAsdaCursorSpeed::ReadWindowsLevel();
		Check(Lvl == C->WinLevel, FString::Printf(TEXT("скорость указателя Windows не тронута прогоном без окна: %d (была %d); %s"), Lvl, C->WinLevel,
			*FAsdaCursorSpeed::Describe()));
	});

	// ── D. «Отмена» ──
	C->Steps.Add([this, ClickW, Ids]()
	{
		if (const FAsdaOptSlider* S = FindSlider(Ids[1])) { ClickW(S->BtnRight, TEXT("стрелка «+» камеры (перед «Отмена»)")); }
	});
	C->Steps.Add([this, C, Check, ClickW]()
	{
		Check(State.CameraTurn != C->BufCamera || C->BufCamera == UAsdaGameUserSettings::kMouseSensMax,
			FString::Printf(TEXT("перед «Отмена» буфер камеры сменён: %d"), State.CameraTurn));
		ClickW(OptBtnCancel, TEXT("кнопка «Отмена»"));
	});
	C->Steps.Add([this, C, Check, CheckProfile]()
	{
		Check(!IsOptionsShown(), TEXT("«Отмена»: окно закрыто"));
		Check(State.CameraTurn == C->BufCamera, FString::Printf(TEXT("«Отмена»: буфер камеры %d (ждали %d — выбор отброшен)"), State.CameraTurn, C->BufCamera));
		CheckProfile(C->BufCursor, C->BufCamera, TEXT("«Отмена»: профиль не изменился"));
	});

	// ── E. Таблица уровней Windows и возврат ──
	C->Steps.Add([C, Check]()
	{
		struct FCase { int32 Base, Pct, Want; };
		const FCase Cases[] = { { 10, 100, 10 }, { 10, 50, 6 }, { 10, 180, 13 }, { 10, 20, 4 }, { 6, 100, 6 }, { 14, 50, 10 }, { 20, 180, 20 }, { 1, 20, 1 }, { 10, 360, 20 }, { 10, 10, 3 } };
		for (const FCase& K : Cases)
		{
			const int32 Got = FAsdaCursorSpeed::LevelForPercent(K.Base, K.Pct);
			Check(Got == K.Want, FString::Printf(TEXT("уровень Windows %d × %d %% → %d (ждали %d)"), K.Base, K.Pct, Got, K.Want));
		}
		if (UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get())
		{
			P->CursorSpeedPercent = C->OrigCursor;
			P->CameraTurnPercent = C->OrigCamera;
			P->SaveSettings();
		}
		FAsdaCursorSpeed::Refresh();
		const UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
		Check(P && P->CursorSpeedPercent == C->OrigCursor && P->CameraTurnPercent == C->OrigCamera && FAsdaCursorSpeed::ReadWindowsLevel() == C->WinLevel,
			FString::Printf(TEXT("исходные значения возвращены: мышь %d, камера %d, уровень Windows %d"), C->OrigCursor, C->OrigCamera, C->WinLevel));
	});

	TWeakObjectPtr<UAsdaSystemOptionSubsystem> WeakThis(this);
	TWeakObjectPtr<UWorld> WW(W);
	const bool bQuit = KitArgs.bQuit;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, WW, C, bQuit](float) -> bool
	{
		if (!WeakThis.IsValid() || !WW.IsValid())
		{
			UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-МЫШЬ ИТОГ: ok=%d fail=%d (ПРЕРВАН: мир или подсистема ушли)"), C->Ok, C->Fail + 1);
			return false;
		}
		if (C->Next < C->Steps.Num()) { C->Steps[C->Next++](); return true; }
		// ★ 26.10.2026: песочница раскладки снята, файл владельца сверен байт в байт.
		const bool bOwnerKept = C->KeySandbox.End();
		if (bOwnerKept) { ++C->Ok; } else { ++C->Fail; }
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-МЫШЬ [%s] %s файл раскладки владельца system/Shortcutkey.ini не тронут (песочница)"), *C->Lang,
			bOwnerKept ? TEXT("✔") : TEXT("✘ ПРОВАЛ"));
		AsdaSelfTestKit::Finish(WW.Get(), kCmd, bQuit, LogAsdaSysOpt, kPrefix, C->Ok, C->Fail,
			*FString::Printf(TEXT(" [язык %s]"), *C->Lang), /*bWarning*/ true);
		return false;
	}), 0.3f);
	UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-МЫШЬ [%s]: запуск, шагов %d, профиль мышь %d / камера %d, уровень Windows %d, кадры: %s"), *C->Lang,
		C->Steps.Num(), C->OrigCursor, C->OrigCamera, C->WinLevel, C->ShotDir.IsEmpty() ? TEXT("нет") : *C->ShotDir);
#else
	(void)Args;
#endif // ASDA_DEV_CONSOLE
}

// ═══════════════════ ★ 25.10.2026 САМОТЕСТ СТРОКИ «ПРИСЕД» `asda.SysOpt.SelfTestCrouch [quit] [shot=<папка>]` ═══════════════════
//   [ПОРТ] Проверка ПУТЁМ ИГРОКА (закон `ui-check-game-path-geometry`) строки I4 «Присед» (Нажатие/Зажатие) страницы «Разное»:
//   окно открывается штатно, вкладка, под-страница, тело списка, пункты, «Применить»/«Отмена»/«ОК» — настоящими кликами Slate
//   (`UAsdaWindowSubsystem::SimulateClickAt`). Геометрия — живая (`GetCachedGeometry` после отрисовки).
//   Офлайн (`-AsdaNoNet`): «ОК» шлёт 4070 — старт через `RefuseStart`. Проверки:
//     A. подпись = loc 990364 языка сессии, пунктов 2 = loc 990365/990366 в порядке значений 0/1, выбран пункт поля профиля;
//     B. геометрия: узлы строки видимы и отрисованы, строка внутри страницы, НЕ пересекается с видимыми строками страницы (10 флажков
//        + полосы I2/I3), подпись и выбранный пункт влезают, раскрытый список — внутри страницы, пункты влезают в строки списка;
//     C. «Зажатие» → «Применить» → поле профиля и `GetCrouchMode` сразу (персонаж: Ctrl вниз — присед без отпускания), строка
//        `CrouchMode=1` в GameUserSettings.ini; «Нажатие» → «Применить» → 0;
//     D. «Отмена» откатывает выбор; повторное открытие показывает сохранённое на той же странице; «ОК» применяет и закрывает;
//     E. исходный режим профиля возвращается.
//   Итог — `САМОТЕСТ-ПРИСЕД-ОКНО ИТОГ: ok=N fail=M` (ловит `asda.SelfTest.Batch`), при `quit` — выход.
void UAsdaSystemOptionSubsystem::RunCrouchModeSelfTest(const TArray<FString>& Args)
{
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping тела нет
	static const TCHAR* const kCmd = TEXT("asda.SysOpt.SelfTestCrouch");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-ПРИСЕД-ОКНО");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (AsdaSelfTestKit::RefuseStart(this, kCmd, KitArgs)) { return; }
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!W || !WM || !UAsdaGameUserSettings::Get())
	{
		AsdaSelfTestKit::Refused(this, kCmd, KitArgs.bQuit, TEXT("нет мира, подсистемы окон или профиля игрока"));
		return;
	}

	// Состояние прогона (живёт в шагах планировщика).
	struct FCtx
	{
		int32 Ok = 0;                          // пройдено
		int32 Fail = 0;                        // провалено
		int32 OrigMode = 0;                    // режим профиля до прогона (вернуть в конце)
		int32 Applied = 0;                     // режим, последним применённый в профиль
		FString ShotDir;                       // папка кадров (пусто — без кадров)
		FString Lang;                          // язык сессии (метка кадров и журнала)
		TArray<TFunction<void()>> Steps;       // шаги — по одному за тик планировщика
		int32 Next = 0;                        // следующий шаг
		FAsdaKeybindTestSandbox KeySandbox;    // ★ 26.10.2026: «Применить»/«ОК» пишут раскладку только в песочницу
	};
	TSharedRef<FCtx> C = MakeShared<FCtx>();
	{
		FString Why;
		if (!C->KeySandbox.Begin(GetKeybinds(), TEXT("crouch"), Why)) { AsdaSelfTestKit::Refused(this, kCmd, KitArgs.bQuit, *Why); return; }
	}
	C->OrigMode = UAsdaGameUserSettings::GetCrouchMode();
	C->Applied = C->OrigMode;
	C->Lang = FAsdaLang::Code(FAsdaLang::Current());
	for (const FString& P : KitArgs.Pos) { if (P.StartsWith(TEXT("shot="))) { C->ShotDir = P.Mid(5); } }

	const FString CmbName = SysOptExtra::NodeName(TEXT("I4"), TEXT("CMB"));   // список (движковый узел; класс списка его гасит)
	const FString TxtName = SysOptExtra::NodeName(TEXT("I4"), TEXT("TXT"));   // подпись строки
	const FString RowName = SysOptExtra::NodeName(TEXT("I4"), TEXT("grp"));   // корень строки
	const FString BodyName = TEXT("Bk_ComboBox_9622_0");                        // тело списка (план 0; шаг asda_ui_option_crouch_row)
	const FString ArrowName = TEXT("Bk_ComboBox_9622_1");                       // стрелка (план 1)
	const FString ValueName = TEXT("Bk_ComboBox_9622_txt");                     // подпись выбранного пункта
	const FString PageName = TEXT("SYSTEM_Op_INT_3_grp");                       // под-страница 70350 «Разное»

	auto Check = [C](bool bPass, const FString& What)
	{
		if (bPass) { ++C->Ok; } else { ++C->Fail; }
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ПРИСЕД-ОКНО [%s] %s %s"), *C->Lang, bPass ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
	};
	auto Node = [this](const FString& N) -> UWidget* { return OptWidget ? OptWidget->GetWidgetFromName(FName(*N)) : nullptr; };
	auto Rect = [](const UWidget* Wd) -> FBox2D
	{
		FVector2D Mn, Mx;
		return (Wd && AsdaUi::WidgetAbsRect(Wd, Mn, Mx)) ? FBox2D(Mn, Mx) : FBox2D(ForceInit);
	};
	// Видимость по цепочке родителей (свёрнутый/скрытый предок прячет узел).
	auto Shown = [](const UWidget* Wd) -> bool
	{
		for (const UWidget* P = Wd; P; P = P->GetParent())
		{
			const ESlateVisibility V = P->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
		}
		return Wd != nullptr;
	};
	auto Overlap = [](const FBox2D& A, const FBox2D& B) -> bool
	{
		return A.bIsValid && B.bIsValid && A.Min.X < B.Max.X - 0.5f && B.Min.X < A.Max.X - 0.5f && A.Min.Y < B.Max.Y - 0.5f && B.Min.Y < A.Max.Y - 0.5f;
	};
	// A целиком внутри Outer (допуск полпикселя).
	auto Inside = [](const FBox2D& A, const FBox2D& Outer) -> bool
	{
		return A.bIsValid && Outer.bIsValid && A.Min.X >= Outer.Min.X - 0.5f && A.Min.Y >= Outer.Min.Y - 0.5f
			&& A.Max.X <= Outer.Max.X + 0.5f && A.Max.Y <= Outer.Max.Y + 0.5f;
	};
	auto Desc = [Rect](const UWidget* Wd) -> FString
	{
		if (!Wd) { return TEXT("НЕТ УЗЛА"); }
		const FBox2D R = Rect(Wd);
		FString S = FString::Printf(TEXT("%s: экран (%.1f,%.1f)-(%.1f,%.1f) %.1f×%.1f вид=%s"), *Wd->GetName(), R.Min.X, R.Min.Y, R.Max.X, R.Max.Y,
			R.Max.X - R.Min.X, R.Max.Y - R.Min.Y, *StaticEnum<ESlateVisibility>()->GetNameStringByValue((int64)Wd->GetVisibility()));
		if (const UTextBlock* T = Cast<UTextBlock>(Wd))
		{
			const FVector2D Ds = T->GetDesiredSize();
			const FVector2D Lc = T->GetCachedGeometry().GetLocalSize();
			S += FString::Printf(TEXT(" текст«%s» желаемо %.1f×%.1f отведено %.1f×%.1f перенос=%d"), *T->GetText().ToString(), Ds.X, Ds.Y,
				Lc.X, Lc.Y, T->GetAutoWrapText() ? 1 : 0);
		}
		return S;
	};
	auto Combo = [this]() -> UAsdaJuiComboBox*
	{
		for (const FAsdaOptExtraCombo& X : ExtraCombos) { if (X.Key == SysOptExtra::I4_Crouch) { return X.Combo; } }
		return nullptr;
	};
	auto ClickW = [WM, Rect, Check](const UWidget* Wd, const TCHAR* What)
	{
		const FBox2D R = Rect(Wd);
		if (!R.bIsValid) { Check(false, FString::Printf(TEXT("клик по %s: нет живой геометрии"), What)); return; }
		WM->SimulateClickAt(R.GetCenter(), /*HoldFrames*/ 3);
	};
	auto Shot = [C](const TCHAR* Name)
	{
		if (C->ShotDir.IsEmpty()) { return; }
		const FString F = FPaths::Combine(C->ShotDir, FString::Printf(TEXT("sysopt_crouch_%s_%s"), *C->Lang, Name));
		FScreenshotRequest::RequestScreenshot(F, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ПРИСЕД-ОКНО [%s] кадр: %s.png"), *C->Lang, *F);
	};
	// Подпись выбранного пункта влезает до стрелки (в экранных точках: желаемая ширина × масштаб строки).
	auto ValueFits = [Node, Rect, ValueName, ArrowName](FString& Out) -> bool
	{
		const UTextBlock* V = Cast<UTextBlock>(Node(ValueName));
		const FBox2D RV = Rect(V), RA = Rect(Node(ArrowName));
		if (!V || !RV.bIsValid || !RA.bIsValid) { Out = TEXT("нет подписи/стрелки"); return false; }
		const float Scale = (RV.Max.X - RV.Min.X) / FMath::Max(1.f, (float)V->GetCachedGeometry().GetLocalSize().X);
		const float Right = RV.Min.X + V->GetDesiredSize().X * Scale;
		Out = FString::Printf(TEXT("«%s»: правый край текста %.1f, стрелка с %.1f (масштаб %.3f)"), *V->GetText().ToString(), Right, RA.Min.X, Scale);
		return Right <= RA.Min.X + 0.5f;
	};
	// Режим в профиле, у читателя и в GameUserSettings.ini на диске; персонаж (если есть) — Ctrl вниз уже присед только в «Зажатии».
	auto CheckApplied = [this, C, Check](int32 Mode, const TCHAR* Via)
	{
		const UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
		const int32 Got = P ? P->CrouchMode : -1;
		Check(Got == Mode && UAsdaGameUserSettings::GetCrouchMode() == Mode,
			FString::Printf(TEXT("%s: профиль CrouchMode=%d, GetCrouchMode=%d (ждали %d) — сразу, без перезапуска"), Via, Got,
				UAsdaGameUserSettings::GetCrouchMode(), Mode));
		const FConfigBranch* Br = GConfig ? GConfig->FindBranch(FName(*GGameUserSettingsIni), GGameUserSettingsIni) : nullptr;
		const FString IniFile = Br ? FPaths::ConvertRelativePathToFull(Br->IniPath) : FString();
		FString Ini;
		const bool bRead = !IniFile.IsEmpty() && FFileHelper::LoadFileToString(Ini, *IniFile);
		const FString Want = FString::Printf(TEXT("CrouchMode=%d"), Mode);
		const bool bHas = bRead && Ini.Contains(Want);
		Check(bHas, FString::Printf(TEXT("%s: в %s строка «%s» %s"), Via, IniFile.IsEmpty() ? TEXT("<нет пути ветки>") : *IniFile, *Want,
			bHas ? TEXT("есть") : TEXT("НЕТ")));
		C->Applied = Mode;
		// Персонаж читает режим той же функцией каждый кадр: нажатие Ctrl (без отпускания) приседает только в «Зажатии».
		APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
		if (AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr)
		{
			Me->SetCrouch(false, TEXT("самотест окна"));
			Me->TickCrouchInput(true, false, true);
			const bool bOn = Me->IsCrouchOn();
			Me->TickCrouchInput(true, true, true);   // сочетание — чтобы отпускание в «Нажатии» ничего не переключило
			Me->TickCrouchInput(false, false, true);
			Me->SetCrouch(false, TEXT("самотест окна"));
			Check(bOn == (Mode == UAsdaGameUserSettings::kCrouchHold),
				FString::Printf(TEXT("%s: персонаж — Ctrl вниз → присед %d (ждали %d)"), Via, bOn ? 1 : 0, Mode == UAsdaGameUserSettings::kCrouchHold ? 1 : 0));
		}
		else
		{
			UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ПРИСЕД-ОКНО [%s] %s: своего персонажа нет — проверка персонажа пропущена"), *C->Lang, Via);
		}
	};
	// Выбор пункта списка путём игрока: клик по телу (раскрыть) → клик по строке пункта (следующим шагом).
	auto PickSteps = [C, Node, ClickW, Check, Combo, CmbName, BodyName](int32 Idx)
	{
		C->Steps.Add([Node, ClickW, BodyName]() { ClickW(Node(BodyName), TEXT("тело списка «Присед»")); });
		C->Steps.Add([Node, ClickW, Check, Combo, CmbName, Idx]()
		{
			UAsdaJuiComboBox* Cb = Combo();
			Check(Cb && Cb->IsOpen(), FString::Printf(TEXT("клик по телу — список раскрыт (для пункта %d)"), Idx));
			ClickW(Node(FString::Printf(TEXT("%s_Row%d"), *CmbName, Idx)), TEXT("строка раскрытого списка"));
		});
	};

	// ── A. Окно, вкладка и страница — путём игрока ──
	C->Steps.Add([this, C, Check, Node, Combo, TxtName]()
	{
		if (IsOptionsShown()) { CloseOptions(/*bApply*/ false); }
		ShowOptions();
		Check(IsOptionsShown() && OptWidget != nullptr, TEXT("окно 69500 открыто"));
		UAsdaJuiComboBox* Cb = Combo();
		Check(Cb != nullptr, TEXT("список «Присед» (SYSTEM_OX_I4_CMB) подключён"));
		const UTextBlock* T = Cast<UTextBlock>(Node(TxtName));
		const FString WantLabel = LocUi(SysOptLoc::kCrouchMode);
		Check(T && !WantLabel.IsEmpty() && T->GetText().ToString() == WantLabel, FString::Printf(TEXT("подпись строки «%s» (loc 990364 «%s»)"),
			T ? *T->GetText().ToString() : TEXT("—"), *WantLabel));
		if (Cb)
		{
			const int32 L[2] = { SysOptLoc::kCrouchToggle, SysOptLoc::kCrouchHold };
			bool bItems = (Cb->NumItems() == 2);
			FString Got;
			for (int32 i = 0; i < Cb->NumItems(); ++i)
			{
				Got += FString::Printf(TEXT("«%s» "), *Cb->GetItemLabel(i));
				if (i < 2) { bItems &= (!LocUi(L[i]).IsEmpty() && Cb->GetItemLabel(i) == LocUi(L[i])); }
			}
			Check(bItems, FString::Printf(TEXT("пунктов %d: %s(ждали loc 990365/990366 в порядке 0/1)"), Cb->NumItems(), *Got));
			Check(Cb->GetSelectedIndex() == C->OrigMode, FString::Printf(TEXT("при открытии выбран пункт %d = режим профиля %d"), Cb->GetSelectedIndex(), C->OrigMode));
		}
	});
	C->Steps.Add([]() {});   // кадр на раскладку только что показанного окна (до него у кнопок нет геометрии)
	C->Steps.Add([this, ClickW]() { ClickW(OptTabBtn[1], TEXT("вкладка «Настройки системы»")); });
	C->Steps.Add([this, Check, ClickW]()
	{
		Check(CurrentTab == 1, FString::Printf(TEXT("клик по вкладке «Настройки системы» → вкладка %d (ждали 1)"), CurrentTab));
		ClickW(OptTabBtn[0], TEXT("вкладка «Настройки игры»"));
	});
	C->Steps.Add([this, Check, ClickW]()
	{
		Check(CurrentTab == 0, FString::Printf(TEXT("клик по вкладке «Настройки игры» → вкладка %d (ждали 0)"), CurrentTab));
		if (CurrentTab != 0) { SelectTab(0); }
		ClickW(OptListBtn[PageSlot(0, 2)], TEXT("ячейка «Разное»"));
	});
	C->Steps.Add([this, Check]()
	{
		Check(CurrentPage[0] == 2, FString::Printf(TEXT("клик по ячейке → под-страница %d (ждали 2 «Разное»)"), CurrentPage[0]));
		if (CurrentPage[0] != 2) { SelectSubPage(0, 2); }
	});
	C->Steps.Add([]() {});   // кадр на раскладку

	// ── B. Геометрия на экране ──
	C->Steps.Add([this, Check, Node, Rect, Shown, Overlap, Inside, Desc, ValueFits, Shot, RowName, PageName, CmbName, TxtName, BodyName, ArrowName, ValueName]()
	{
		UWidget* Page = Node(PageName);
		UWidget* Row = Node(RowName);
		const FBox2D RP = Rect(Page), RR = Rect(Row);
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ПРИСЕД-ОКНО ГЕОМЕТРИЯ окно %s"), *Desc(OptWidget ? OptWidget->GetRootWidget() : nullptr));
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ПРИСЕД-ОКНО ГЕОМЕТРИЯ страница %s"), *Desc(Page));
		const FString RowNodes[6] = { RowName, BodyName, ArrowName, CmbName, ValueName, TxtName };
		for (const FString& N : RowNodes)
		{
			UWidget* Wd = Node(N);
			UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ПРИСЕД-ОКНО ГЕОМЕТРИЯ строка  %s"), *Desc(Wd));
			// Движковый `UComboBoxString` класс списка гасит сам (рисует планы) — для него видимость не требуется.
			if (N != CmbName) { Check(Wd && Shown(Wd) && Rect(Wd).bIsValid, FString::Printf(TEXT("%s видим и отрисован"), *N)); }
		}
		Check(Inside(RR, RP), FString::Printf(TEXT("строка внутри страницы: (%.1f,%.1f)-(%.1f,%.1f) в (%.1f,%.1f)-(%.1f,%.1f)"), RR.Min.X, RR.Min.Y,
			RR.Max.X, RR.Max.Y, RP.Min.X, RP.Min.Y, RP.Max.X, RP.Max.Y));
		// Соседи — видимые строки той же страницы (канвасы-дети: 10 флажков и полосы I2/I3).
		int32 CheckRows = 0, Bars = 0;
		if (UPanelWidget* PP = Cast<UPanelWidget>(Page))
		{
			for (int32 i = 0; i < PP->GetChildrenCount(); ++i)
			{
				UWidget* K = PP->GetChildAt(i);
				if (!K || K == Row || !Cast<UCanvasPanel>(K) || !Shown(K)) { continue; }
				if (K->GetName().StartsWith(TEXT("Box_Check_grp"))) { ++CheckRows; }
				if (K->GetName() == SysOptExtra::NodeName(TEXT("I2"), TEXT("grp")) || K->GetName() == SysOptExtra::NodeName(TEXT("I3"), TEXT("grp"))) { ++Bars; }
				UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ПРИСЕД-ОКНО ГЕОМЕТРИЯ сосед  %s (ребёнок %d)"), *Desc(K), i);
				Check(!Overlap(RR, Rect(K)), FString::Printf(TEXT("строка не пересекается с соседом %s"), *K->GetName()));
			}
		}
		Check(CheckRows == 10 && Bars == 2, FString::Printf(TEXT("видимых соседей на «Разном»: флажков %d, полос %d (ждали 10 и 2)"), CheckRows, Bars));
		const UTextBlock* T = Cast<UTextBlock>(Node(TxtName));
		Check(T && T->GetDesiredSize().X <= T->GetCachedGeometry().GetLocalSize().X + 0.5f && !T->GetAutoWrapText(),
			FString::Printf(TEXT("подпись строки влезает без переноса: желаемо %.1f, отведено %.1f"), T ? T->GetDesiredSize().X : -1.f,
				T ? T->GetCachedGeometry().GetLocalSize().X : -1.f));
		FString Why;
		const bool bFits = ValueFits(Why);
		Check(bFits, FString::Printf(TEXT("подпись выбранного пункта влезает до стрелки: %s"), *Why));
		Shot(TEXT("closed"));
	});

	// ── C. «Зажатие» → «Применить», «Нажатие» → «Применить» ──
	for (const int32 Mode : { (int32)UAsdaGameUserSettings::kCrouchHold, (int32)UAsdaGameUserSettings::kCrouchToggle })
	{
		C->Steps.Add([Node, ClickW, BodyName]() { ClickW(Node(BodyName), TEXT("тело списка «Присед»")); });
		C->Steps.Add([Check, Combo, Node, Rect, Inside, Desc, Shot, CmbName, PageName, Mode]()
		{
			UAsdaJuiComboBox* Cb = Combo();
			Check(Cb && Cb->IsOpen(), FString::Printf(TEXT("клик по телу — список раскрыт (для пункта %d)"), Mode));
			if (Mode != UAsdaGameUserSettings::kCrouchHold) { return; }
			const UWidget* ListBg = Node(FString::Printf(TEXT("%s_ListBg"), *CmbName));   // подложка всего списка
			const FBox2D RB = Rect(ListBg), RP = Rect(Node(PageName));
			UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ПРИСЕД-ОКНО ГЕОМЕТРИЯ список %s"), *Desc(ListBg));
			Check(Inside(RB, RP), FString::Printf(TEXT("раскрытый список внутри страницы: (%.1f,%.1f)-(%.1f,%.1f), страница до (%.1f,%.1f)"),
				RB.Min.X, RB.Min.Y, RB.Max.X, RB.Max.Y, RP.Max.X, RP.Max.Y));
			for (int32 i = 0; i < 2; ++i)
			{
				const UTextBlock* RowT = Cast<UTextBlock>(Node(FString::Printf(TEXT("%s_Row%d"), *CmbName, i)));
				UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ПРИСЕД-ОКНО ГЕОМЕТРИЯ пункт %s"), *Desc(RowT));
				const FBox2D RT = Rect(RowT);
				const float Scale = RowT ? (RT.Max.X - RT.Min.X) / FMath::Max(1.f, (float)RowT->GetCachedGeometry().GetLocalSize().X) : 1.f;
				const float Right = RowT ? RT.Min.X + RowT->GetDesiredSize().X * Scale : -1.f;
				Check(RowT && RT.bIsValid && RB.bIsValid && Right <= RB.Max.X + 0.5f,
					FString::Printf(TEXT("пункт %d списка влезает в список: правый край текста %.1f, подложка до %.1f"), i, Right, RB.Max.X));
			}
			Shot(TEXT("open"));
		});
		C->Steps.Add([Node, ClickW, CmbName, Mode]() { ClickW(Node(FString::Printf(TEXT("%s_Row%d"), *CmbName, Mode)), TEXT("строка раскрытого списка")); });
		C->Steps.Add([this, C, Check, Combo, ValueFits, ClickW, Mode]()
		{
			UAsdaJuiComboBox* Cb = Combo();
			const UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
			Check(State.CrouchMode == Mode && Cb && Cb->GetSelectedIndex() == Mode && !Cb->IsOpen(),
				FString::Printf(TEXT("клик по пункту %d: буфер окна %d, выбран пункт %d, список закрыт"), Mode, State.CrouchMode, Cb ? Cb->GetSelectedIndex() : -1));
			Check(P && P->CrouchMode == C->Applied, FString::Printf(TEXT("до «Применить» профиль не тронут: %d (ждали %d)"), P ? P->CrouchMode : -1, C->Applied));
			FString Why;
			const bool bFits = ValueFits(Why);
			Check(bFits, FString::Printf(TEXT("подпись пункта %d влезает до стрелки: %s"), Mode, *Why));
			ClickW(OptBtnApply, TEXT("кнопка «Применить»"));
		});
		C->Steps.Add([this, Check, CheckApplied, Mode]()
		{
			Check(IsOptionsShown(), TEXT("«Применить»: окно осталось открытым"));
			CheckApplied(Mode, TEXT("«Применить»"));
		});
	}

	// ── D. «Отмена», повторное открытие, «ОК» ──
	PickSteps(UAsdaGameUserSettings::kCrouchHold);
	C->Steps.Add([this, Check, ClickW]()
	{
		Check(State.CrouchMode == UAsdaGameUserSettings::kCrouchHold, FString::Printf(TEXT("перед «Отмена» в буфере 1 (есть %d)"), State.CrouchMode));
		ClickW(OptBtnCancel, TEXT("кнопка «Отмена»"));
	});
	C->Steps.Add([this, C, Check]()
	{
		const UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
		Check(!IsOptionsShown(), TEXT("«Отмена»: окно закрыто"));
		Check(P && P->CrouchMode == C->Applied && UAsdaGameUserSettings::GetCrouchMode() == C->Applied && State.CrouchMode == C->Applied,
			FString::Printf(TEXT("«Отмена»: профиль %d, чтение %d, буфер %d (ждали %d — выбор отброшен)"), P ? P->CrouchMode : -1,
				UAsdaGameUserSettings::GetCrouchMode(), State.CrouchMode, C->Applied));
		ShowOptions();   // повторное открытие — та же функция, что кнопка «Настройки» ESC-меню
	});
	C->Steps.Add([this, C, Check, Combo, Node, Shown, Rect, Shot, RowName, ValueName]()
	{
		UAsdaJuiComboBox* Cb = Combo();
		const UTextBlock* V = Cast<UTextBlock>(Node(ValueName));
		Check(Cb && Cb->GetSelectedIndex() == C->Applied && V && V->GetText().ToString() == Cb->GetItemLabel(C->Applied),
			FString::Printf(TEXT("повторное открытие: выбран пункт %d «%s» (ждали %d «%s»)"), Cb ? Cb->GetSelectedIndex() : -1,
				V ? *V->GetText().ToString() : TEXT("—"), C->Applied, Cb ? *Cb->GetItemLabel(C->Applied) : TEXT("—")));
		Check(CurrentTab == 0 && CurrentPage[0] == 2 && Shown(Node(RowName)) && Rect(Node(RowName)).bIsValid,
			FString::Printf(TEXT("повторное открытие: вкладка %d, страница %d, строка видна"), CurrentTab, CurrentPage[0]));
		Shot(TEXT("reopen"));
	});
	PickSteps(UAsdaGameUserSettings::kCrouchHold);
	C->Steps.Add([this, ClickW]() { ClickW(OptBtnOk, TEXT("кнопка «ОК»")); });
	C->Steps.Add([this, Check, CheckApplied]()
	{
		Check(!IsOptionsShown(), TEXT("«ОК»: окно закрыто"));
		CheckApplied(UAsdaGameUserSettings::kCrouchHold, TEXT("«ОК»"));
		ShowOptions();
	});
	C->Steps.Add([this, Check, Combo, Shot]()
	{
		UAsdaJuiComboBox* Cb = Combo();
		Check(Cb && Cb->GetSelectedIndex() == UAsdaGameUserSettings::kCrouchHold,
			FString::Printf(TEXT("после «ОК» окно показывает пункт %d (ждали 1 «Зажатие»)"), Cb ? Cb->GetSelectedIndex() : -1));
		Shot(TEXT("after_ok"));
	});
	C->Steps.Add([this]() { CloseOptions(/*bApply*/ false); });

	// ── E. Возврат исходного режима ──
	C->Steps.Add([C, Check]()
	{
		if (UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get())
		{
			P->CrouchMode = C->OrigMode;
			P->SaveSettings();
		}
		Check(UAsdaGameUserSettings::GetCrouchMode() == C->OrigMode, FString::Printf(TEXT("исходный режим %d возвращён"), C->OrigMode));
	});

	TWeakObjectPtr<UAsdaSystemOptionSubsystem> WeakThis(this);
	TWeakObjectPtr<UWorld> WW(W);
	const bool bQuit = KitArgs.bQuit;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, WW, C, bQuit](float) -> bool
	{
		if (!WeakThis.IsValid() || !WW.IsValid())
		{
			UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ПРИСЕД-ОКНО ИТОГ: ok=%d fail=%d (ПРЕРВАН: мир или подсистема ушли)"), C->Ok, C->Fail + 1);
			return false;
		}
		if (C->Next < C->Steps.Num()) { C->Steps[C->Next++](); return true; }
		// ★ 26.10.2026: песочница раскладки снята, файл владельца сверен байт в байт.
		const bool bOwnerKept = C->KeySandbox.End();
		if (bOwnerKept) { ++C->Ok; } else { ++C->Fail; }
		UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ПРИСЕД-ОКНО [%s] %s файл раскладки владельца system/Shortcutkey.ini не тронут (песочница)"), *C->Lang,
			bOwnerKept ? TEXT("✔") : TEXT("✘ ПРОВАЛ"));
		AsdaSelfTestKit::Finish(WW.Get(), kCmd, bQuit, LogAsdaSysOpt, kPrefix, C->Ok, C->Fail,
			*FString::Printf(TEXT(" [язык %s]"), *C->Lang), /*bWarning*/ true);
		return false;
	}), 0.3f);
	UE_LOG(LogAsdaSysOpt, Warning, TEXT("САМОТЕСТ-ПРИСЕД-ОКНО [%s]: запуск, шагов %d, режим профиля %d, кадры: %s"), *C->Lang, C->Steps.Num(), C->OrigMode,
		C->ShotDir.IsEmpty() ? TEXT("нет") : *C->ShotDir);
#else
	(void)Args;
#endif // ASDA_DEV_CONSOLE
}
