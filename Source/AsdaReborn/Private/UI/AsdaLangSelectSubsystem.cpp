// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна первого выбора языка — назначение и правила в шапке `Public/UI/AsdaLangSelectSubsystem.h`.
#include "UI/AsdaLangSelectSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton

#include "UI/AsdaWindowSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"
#include "AsdaOutgoingGate.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Misc/App.h"
#include "UnrealClient.h"
#include "Internationalization/StringTable.h"   // UStringTable — самотест подписей (этап 4)
#include "Internationalization/StringTableCore.h"   // FScreenshotRequest — снимок окна самотестом
#include "Internationalization/Internationalization.h" // культура UE — самотест языка на лету
#include "Internationalization/Culture.h"
#include "Kismet/GameplayStatics.h"                    // OpenLevel — пересоздание экрана логина после выбора языка
#include "Resources/AsdaTextureSubsystem.h"            // отпустить атласы с языковым вариантом перед перезагрузкой уровня
#include "Resources/AsdaBenchSubsystem.h"              // ★ 12.10.2026: замер графики первого запуска — сразу после выбора языка
#include "Items/AsdaItemTableSubsystem.h"              // самотест: таблица skill_info (кэш навыков пересобран на новом языке)
#include "Items/AsdaItemDbRows.h"                      // FAsdaSkillInfoRow
#include "UI/AsdaSkillDatabase.h"                      // самотест: имена навыков в кэше = язык сессии
#include "AsdaBytes.h"                                 // код навыка из записи skill_info
#include "HAL/FileManager.h"                           // самотест: перечень пакетов Content/L10N/<культура>
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Containers/Ticker.h"
#include "Resources/AsdaGameUserSettings.h"            // самотест PIE: очистка выбора языка в профиле редактора
#include "Misc/FileHelper.h"                          // самотест PIE: байты файла профиля до/после (возврат профиля)
#include "Misc/ConfigCacheIni.h"                       // FConfigCacheIni::GetDestIniFilename — путь файла профиля
#if WITH_EDITOR
#include "Editor.h"                                    // самотест PIE: GEditor / FEditorDelegates / RequestPlaySession
#include "PlayInEditorDataTypes.h"
#include "Internationalization/TextLocalizationManager.h"   // ★ 10.10.2026 (en_completeness): самотест — предпросмотр языка игры в PIE
#include "Blueprint/WidgetTree.h"                           // ★ 10.10.2026 (en_completeness): самотест — надписи WBP выбора персонажа
#include "World/AsdaEntityText.h"                           // ★ 10.10.2026 (en_completeness): самотест — имя NPC в PIE
#include "Engine/Texture2D.h"                               // ★ 10.10.2026 (en_terms_fix, МЕЛК-3): пары картинок L10N после PIE
#include "TextureResource.h"                                // ★ 10.10.2026 (en_terms_fix, МЕЛК-3): GPU-ресурс RU до/после возврата
#include "RenderingThread.h"                                // ★ 10.10.2026 (en_terms_fix, МЕЛК-3): FlushRenderingCommands перед снимком ресурсов
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaLangSelect, Log, All);

const TCHAR* const UAsdaLangSelectSubsystem::kWidgetPath = TEXT("/Game/AsdaAssets/UI/CommonWin/WBP_LANG_SELECT");
const FName UAsdaLangSelectSubsystem::kWindowId(TEXT("LANG_SELECT"));
const TCHAR* const UAsdaLangSelectSubsystem::kTextNode = TEXT("MESSAGE2_TXT00");
const TCHAR* const UAsdaLangSelectSubsystem::kBtnEn = TEXT("MESSAGE2_BUT00");
const TCHAR* const UAsdaLangSelectSubsystem::kBtnRu = TEXT("MESSAGE2_BUT01");
const TCHAR* const UAsdaLangSelectSubsystem::kBtnAr = TEXT("MESSAGE2_BUT02");

// ★ 11.10.2026 (варианты окна языка, owner_1011/lang_window): путь WBP ВМЕСТО рабочего — ТОЛЬКО на время самотеста
//   `asda.Lang.SelfTestWindow … path=<путь>` (снимки вариантов на выбор владельцу). Пусто — рабочий `kWidgetPath`.
//   Рабочий код его не ставит; самотест сбрасывает сразу после открытия окна.
static FString GLangSelTestWidgetPath;

// Loc-строки окна (наш диапазон 9903xx, `RawAssets/Text/GameText.csv` через ведомость `gametext_overrides.json`).
namespace LangSelLoc
{
	static constexpr int32 kQuestion = 990350;   // «Choose your language / Выберите язык» (двуязычно: язык ещё не выбран)
	static constexpr int32 kEnglish  = 990351;   // «English»
	static constexpr int32 kRussian  = 990352;   // «Русский»
	static constexpr int32 kArabic   = 990353;   // «العربية»
}

void UAsdaLangSelectSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★ Этап 4: подписи окон (ссылки на ST_GameText) — на язык сессии ДО создания первых окон.
	FAsdaLang::ApplyCaptionTable();
	// ★ 10.10.2026 (en_completeness): вбитые в WBP надписи без id — полиглот-данные UE (в PIE — с предпросмотром языка);
	//   картинки с надписями в PIE — копия L10N-вариантов в видеопамять RU-текстур (вне редактора подменяет сам движок).
	FAsdaLang::ApplyUiLiterals();
	FAsdaLang::ApplyPieTextures();
}

bool UAsdaLangSelectSubsystem::ShowFirstRunIfNeeded()
{
	// ★ 10.10.2026 (решение владельца): и в PIE — если в профиле редактора язык не выбран.
	if (!NeedsFirstRun()) { return false; }
	UE_LOG(LogAsdaLangSelect, Log, TEXT("[язык] язык не выбран — окно первого выбора (%s); экран логина — после выбора"), *FAsdaLang::Describe());
	bLoginDeferred = ShowWindow(/*bNoRestart*/ false) != nullptr;
	return bLoginDeferred;
}

UUserWidget* UAsdaLangSelectSubsystem::ShowWindow(bool bNoRestart)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return nullptr; }
	bSuppressRestart = bNoRestart;
	// z 30 — над окнами логина (диалоги логина MESSAGE2 идут на 25), модальное.
	UUserWidget* W = WM->OpenWindowByPath(kWindowId, GLangSelTestWidgetPath.IsEmpty() ? FString(kWidgetPath) : GLangSelTestWidgetPath,
		30, ESlateVisibility::Visible, /*bModal*/ true);
	if (!W)
	{
		UE_LOG(LogAsdaLangSelect, Warning, TEXT("[язык] окно %s не загрузилось — не прогнан шаг ассета asda_ui_lang_select_window?"), kWidgetPath);
		return nullptr;
	}
	auto SetText = [W](const TCHAR* Node, int32 LocId)
	{
		if (UTextBlock* T = Cast<UTextBlock>(W->GetWidgetFromName(FName(Node))))
		{
			const FString S = FAsdaLoginData::Loc(LocId);
			if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
		}
	};
	SetText(kTextNode, LangSelLoc::kQuestion);
	SetText(*(FString(kBtnEn) + TEXT("_Text")), LangSelLoc::kEnglish);
	SetText(*(FString(kBtnRu) + TEXT("_Text")), LangSelLoc::kRussian);
	SetText(*(FString(kBtnAr) + TEXT("_Text")), LangSelLoc::kArabic);

	Relays.Reset();
	auto Bind = [this, W](const TCHAR* Node, EAsdaLang L)
	{
		UButton* B = Cast<UButton>(W->GetWidgetFromName(FName(Node)));
		if (!B) { UE_LOG(LogAsdaLangSelect, Warning, TEXT("[язык] нет кнопки %s"), Node); return; }
		B->OnClicked.Clear();
		// Неактивный язык (арабский): кнопка выключена В АССЕТЕ; обработчик не вешаем, даже если её включат.
		if (!FAsdaLang::IsSelectable(L)) { return; }
		AsdaUi::BindButton(this, B, (int32)L, [this](int32 Idx) { Pick((EAsdaLang)Idx); }, Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(kBtnEn, EAsdaLang::En);
	Bind(kBtnRu, EAsdaLang::Ru);
	Bind(kBtnAr, EAsdaLang::Ar);
	return W;
}

void UAsdaLangSelectSubsystem::Pick(EAsdaLang L)
{
	if (!FAsdaLang::IsSelectable(L)) { return; }
	FAsdaLang::SetSaved(L, /*bSave*/ true, TEXT("первый выбор — применён сразу, без перезапуска"));
	bPicked = true;
	LastPick = L;
	bLastWouldRestart = (L != FAsdaLang::Current());
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->CloseWindow(kWindowId);
	}
	Relays.Reset();
	UE_LOG(LogAsdaLangSelect, Log, TEXT("[язык] выбран «%s»; %s"), FAsdaLang::Code(L),
		bLastWouldRestart ? (bSuppressRestart ? TEXT("применение подавлено (самотест)") : TEXT("применение на лету, экран логина пересоздаётся")) : TEXT("совпал с языком сессии — применять нечего"));
	// ★ 10.10.2026 (требование владельца): первый выбор — сразу, без перезапуска процесса. Экран логина был отложен до
	//   выбора — уровень перезагружается и при совпавшем языке.
	if (!bSuppressRestart && (bLastWouldRestart || bLoginDeferred))
	{
		ApplyLiveAndReloadLogin(L);
	}
}

bool UAsdaLangSelectSubsystem::ApplyLiveAndReloadLogin(EAsdaLang L)
{
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (!W) { return false; }
	bLoginDeferred = false;
	if (FAsdaLang::ApplyLive(L))
	{
		++LiveApplyCount;
		// Атласы с языковым вариантом — из кэша текстур (иначе корень кэша держал бы прежний вариант до выхода).
		if (UAsdaTextureSubsystem* TexMgr = AsdaTexMgr()) { TexMgr->ReleaseLocalizedVariants(); }
	}
	// Уровень логина — заново (как `DoReturnToCharSelect`): движок снимает виджеты, `LoadMap` собирает мусор, пакеты
	//   прежней культуры выгружаются, новый `StartPlay` строит экран заново. Соединение с логин-сервером живёт на
	//   GameInstance и не рвётся (ранний коннект повторно не идёт: `IsConnected`). Заставку второй раз не показываем.
	const FString Map = UWorld::RemovePIEPrefix(W->GetOutermost()->GetName());
	bSkipStartLogoOnce = true;
	LiveReloadedWorld = Map;
	LiveOldWorld = W;
	// ★★★ 12.10.2026 (автоподбор графики): первый запуск продолжается замером кадра на карте замера — язык уже применён
	//   на лету, экран логина строится после возврата (`UAsdaBenchSubsystem::Finish` → уровень логина). Замер не нужен
	//   или не запустился — прежний путь: перезагрузка уровня логина.
	if (UAsdaBenchSubsystem* Bench = GI->GetSubsystem<UAsdaBenchSubsystem>())
	{
		if (Bench->ShouldRunFirstRun() && Bench->StartFirstRun(W))
		{
			UE_LOG(LogAsdaLangSelect, Log, TEXT("[язык] на лету «%s»: далее — замер графики первого запуска (карта замера), экран логина после него"), FAsdaLang::Code(L));
			return true;
		}
	}
	UE_LOG(LogAsdaLangSelect, Log, TEXT("[язык] на лету «%s»: перезагрузка уровня %s"), FAsdaLang::Code(L), *Map);
	UGameplayStatics::OpenLevel(W, FName(*Map));
	return true;
}

bool UAsdaLangSelectSubsystem::ConsumeSkipStartLogo()
{
	const bool b = bSkipStartLogoOnce;
	bSkipStartLogoOnce = false;
	return b;
}

bool UAsdaLangSelectSubsystem::IsOpen() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && WM->FindWindow(kWindowId) != nullptr;
}

// ── Самотест окна (путь игрока: -game, геометрия ПОСЛЕ отрисовки). `asda.Lang.SelfTestWindow [ru|en] [shot] [path=<WBP>] [quit]`:
//    открыть окно, через 1.5 с снять геометрию трёх кнопок (ненулевые, СТОЛБЦОМ сверху вниз English / Русский /
//    العربية — разметка V3 от 11.10.2026, «العربية» выключена, подписи из loc),
//    нажать выбранную кнопку делегатом OnClicked (перезапуск подавлен) и проверить профиль и закрытие окна.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdLangWindowSelfTest(
	TEXT("asda.Lang.SelfTestWindow"),
	TEXT("[ПОРТ] Самотест окна первого выбора языка: геометрия трёх кнопок, неактивный арабский, запись профиля. [ru|en] [shot] [path=<WBP варианта>] [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaLangSelectSubsystem* LS = GI ? GI->GetSubsystem<UAsdaLangSelectSubsystem>() : nullptr;
		if (!LS) { return; }
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Lang.SelfTestWindow"), KitArgs)) { return; }
		bool bQuit = false, bShot = false;
		EAsdaLang PickL = EAsdaLang::Ru;
		FString VariantPath;   // ★ 11.10.2026: `path=<WBP>` — показать вариант разметки вместо рабочего ассета (только самотест)
		for (const FString& A : Args)
		{
			if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
			if (A.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { bShot = true; }
			if (A.StartsWith(TEXT("path="), ESearchCase::IgnoreCase)) { VariantPath = A.Mid(5).TrimQuotes(); }
			EAsdaLang L;
			if (FAsdaLang::ParseCode(A, L)) { PickL = L; }
		}
		GLangSelTestWidgetPath = VariantPath;
		UUserWidget* Win = LS->ShowWindow(/*bNoRestart*/ true);
		GLangSelTestWidgetPath.Reset();   // подмена — только на это открытие
		// Имя снимка: у варианта — с хвостом имени ассета (asda_lang_window_ru_WBP_LANG_SELECT_V1).
		const FString ShotTail = VariantPath.IsEmpty() ? FString() : TEXT("_") + FPackageName::GetShortName(VariantPath);
		struct FStat { int32 Ok = 0, Fail = 0; };
		TSharedRef<FStat> St = MakeShared<FStat>();
		auto Check = [St](bool b, const FString& What)
		{
			if (b) { ++St->Ok; } else { ++St->Fail; }
			UE_LOG(LogAsdaLangSelect, Warning, TEXT("САМОТЕСТ-ЯЗЫК-ОКНО %s %s"), b ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
		};
		Check(Win != nullptr, TEXT("окно WBP_LANG_SELECT открыто"));
		TWeakObjectPtr<UUserWidget> WeakWin = Win;
		FTimerHandle H;
		// Снимок экрана с окном (путь игрока, UI в кадре) — за 0.5 с до проверок и клика.
		if (bShot)
		{
			FTimerHandle HS;
			W->GetTimerManager().SetTimer(HS, FTimerDelegate::CreateWeakLambda(LS, [ShotTail]()
			{
				FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("asda_lang_window_%s%s"), FAsdaLang::Code(FAsdaLang::Current()), *ShotTail),
					/*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
			}), 1.0f, false);
		}
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(LS, [LS, WeakWin, Check, St, PickL, bQuit, W]()
		{
			if (AsdaSelfTestKit::RefuseDeferred(LS, TEXT("asda.Lang.SelfTestWindow"), bQuit)) { return; }   // ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте
			UUserWidget* Win2 = WeakWin.Get();
			float PrevY = -1.f;   // ★ 11.10.2026 (V3): кнопки столбцом — порядок сверху вниз (прежде ряд — слева направо)
			const TCHAR* Btns[] = { UAsdaLangSelectSubsystem::kBtnEn, UAsdaLangSelectSubsystem::kBtnRu, UAsdaLangSelectSubsystem::kBtnAr };
			const int32 Locs[] = { 990351, 990352, 990353 };
			for (int32 i = 0; i < 3; ++i)
			{
				UButton* B = Win2 ? Cast<UButton>(Win2->GetWidgetFromName(FName(Btns[i]))) : nullptr;
				const FGeometry G = B ? B->GetCachedGeometry() : FGeometry();
				const FVector2D P = G.GetAbsolutePosition(), S = G.GetAbsoluteSize();
				Check(B && S.X > 1.f && S.Y > 1.f && P.Y > PrevY,
					FString::Printf(TEXT("кнопка %s: экран (%.0f,%.0f) %.0f×%.0f, сверху вниз"), Btns[i], P.X, P.Y, S.X, S.Y));
				PrevY = P.Y;
				UTextBlock* T = Win2 ? Cast<UTextBlock>(Win2->GetWidgetFromName(FName(FString(Btns[i]) + TEXT("_Text")))) : nullptr;
				const FString Want = FAsdaLoginData::Loc(Locs[i]);
				Check(T && T->GetText().ToString() == Want && !Want.IsEmpty(),
					FString::Printf(TEXT("подпись %s = loc %d «%s»"), Btns[i], Locs[i], T ? *T->GetText().ToString() : TEXT("—")));
				if (i == 2) { Check(B && !B->GetIsEnabled(), TEXT("«العربية» неактивна (ассет)")); }
				else        { Check(B && B->GetIsEnabled(), FString::Printf(TEXT("%s активна"), Btns[i])); }
			}
			UButton* Target = Win2 ? Cast<UButton>(Win2->GetWidgetFromName(FName(PickL == EAsdaLang::En ? UAsdaLangSelectSubsystem::kBtnEn : UAsdaLangSelectSubsystem::kBtnRu))) : nullptr;
			if (Target) { Target->OnClicked.Broadcast(); }
			EAsdaLang Saved;
			Check(LS->bPicked && LS->LastPick == PickL && FAsdaLang::GetSaved(Saved) && Saved == PickL,
				FString::Printf(TEXT("клик «%s» → профиль «%s»"), FAsdaLang::Code(PickL), FAsdaLang::GetSaved(Saved) ? FAsdaLang::Code(Saved) : TEXT("пусто")));
			Check(!LS->IsOpen(), TEXT("окно закрыто после выбора"));
			Check(LS->bLastWouldRestart == (PickL != FAsdaLang::Current()),
				FString::Printf(TEXT("перезапуск %s (сессия «%s»)"), LS->bLastWouldRestart ? TEXT("нужен") : TEXT("не нужен"), FAsdaLang::Code(FAsdaLang::Current())));
			UE_LOG(LogAsdaLangSelect, Warning, TEXT("САМОТЕСТ-ЯЗЫК-ОКНО ИТОГ: ok=%d fail=%d"), St->Ok, St->Fail);
			AsdaSelfTestKit::Quit(W, bQuit);
		}), 1.5f, false);
	}));
#endif // ASDA_DEV_CONSOLE

// ── ★ Этап 4: самотест подписей окон, поставленных ссылкой на ST_GameText. `asda.Lang.SelfTestCaptions [shot] [quit]`:
//    1) каждая запись таблицы подписей = `FAsdaLoginData::Loc(id)` на языке сессии (после `ApplyCaptionTable`);
//    2) окно `Item_Initializ_Win` (20200), открытое как у игрока: 8 подписей из плана шага = Loc(50031…50038);
//    3) снимок окна (shot) и закрытие.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdLangCaptionsSelfTest(
	TEXT("asda.Lang.SelfTestCaptions"),
	TEXT("[ПОРТ] Самотест подписей окон через ST_GameText: таблица = язык сессии, подписи окна 20200 = GameText. [shot] [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM) { return; }
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Lang.SelfTestCaptions"), KitArgs)) { return; }
		const bool bShot = Args.ContainsByPredicate([](const FString& A) { return A.Equals(TEXT("shot"), ESearchCase::IgnoreCase); });
		const bool bQuit = Args.ContainsByPredicate([](const FString& A) { return A.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
		struct FStat { int32 Ok = 0, Fail = 0; };
		TSharedRef<FStat> St = MakeShared<FStat>();
		auto Check = [St](bool b, const FString& What)
		{
			if (b) { ++St->Ok; } else { ++St->Fail; }
			UE_LOG(LogAsdaLangSelect, Warning, TEXT("САМОТЕСТ-ПОДПИСИ %s %s"), b ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
		};
		// 1) таблица
		UStringTable* Tbl = LoadObject<UStringTable>(nullptr, FAsdaLang::kCaptionTablePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		Check(Tbl != nullptr, FString::Printf(TEXT("таблица %s загружена"), FAsdaLang::kCaptionTablePath));
		if (Tbl)
		{
			int32 N = 0, Bad = 0;
			FString FirstBad;
			Tbl->GetStringTable()->EnumerateSourceStrings([&](const FString& Key, const FString& Src)
			{
				++N;
				const FString Want = FAsdaLoginData::Loc(FCString::Atoi(*Key));
				if (!Want.IsEmpty() && Src != Want) { if (!Bad++) { FirstBad = FString::Printf(TEXT("%s: «%s» ≠ «%s»"), *Key, *Src, *Want); } }
				return true;
			});
			Check(N > 0 && Bad == 0, FString::Printf(TEXT("записей %d, не на языке сессии «%s» — %d %s"), N, FAsdaLang::Code(FAsdaLang::Current()), Bad, *FirstBad));
		}
		// 2) окно 20200
		UUserWidget* Win = WM->OpenWindowByPath(FName(TEXT("Item_Initializ_Win")), TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Item_Initializ_Win"), 10, ESlateVisibility::SelfHitTestInvisible);
		Check(Win != nullptr, TEXT("окно Item_Initializ_Win открыто"));
		TWeakObjectPtr<UUserWidget> WeakWin = Win;
		if (bShot)
		{
			FTimerHandle HS;
			W->GetTimerManager().SetTimer(HS, FTimerDelegate::CreateWeakLambda(WM, []()
			{
				FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("asda_captions_20200_%s"), FAsdaLang::Code(FAsdaLang::Current())), true, false);
			}), 1.0f, false);
		}
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(WM, [WM, WeakWin, Check, St, bQuit, W]()
		{
			if (AsdaSelfTestKit::RefuseDeferred(WM, TEXT("asda.Lang.SelfTestCaptions"), bQuit)) { return; }   // ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте
			static const TCHAR* const Nodes[] = {
				TEXT("Item_Initializ_Grade"), TEXT("Item_Initializ_Power"), TEXT("Item_Initializ_Chance"), TEXT("Item_Initializ_TargetTitle_Text"),
				TEXT("Item_Initializ_Btn_Text"), TEXT("Item_Initializ_IniFeeTxt"), TEXT("Item_Initializ_GoldTxt"), TEXT("Item_Initializ_Cancel_Text") };
			for (int32 i = 0; i < 8; ++i)
			{
				UUserWidget* Win2 = WeakWin.Get();
				const UTextBlock* T = Win2 ? Cast<UTextBlock>(Win2->GetWidgetFromName(FName(Nodes[i]))) : nullptr;
				const FString Have = T ? T->GetText().ToString() : TEXT("—");
				const FString Want = FAsdaLoginData::Loc(50031 + i);
				Check(T && Have == Want, FString::Printf(TEXT("%s = loc %d «%s» (на экране «%s»)"), Nodes[i], 50031 + i, *Want, *Have));
			}
			WM->CloseWindow(FName(TEXT("Item_Initializ_Win")));
			UE_LOG(LogAsdaLangSelect, Warning, TEXT("САМОТЕСТ-ПОДПИСИ ИТОГ: ok=%d fail=%d"), St->Ok, St->Fail);
			AsdaSelfTestKit::Quit(W, bQuit);
		}), 1.8f, false);
	}));
#endif // ASDA_DEV_CONSOLE

// ── ★ 10.10.2026: ЯЗЫК НА ЛЕТУ — проверки путём игрока ──────────────────────────────────────────────────────────────
namespace AsdaLangLive
{
	// Первый игровой мир процесса (после перезагрузки уровня мир консольной последовательности уже уничтожен).
	static UWorld* GameWorld(UWorld* Hint)
	{
		if (Hint && Hint->IsGameWorld()) { return Hint; }
		if (!GEngine) { return nullptr; }
		for (const FWorldContext& C : GEngine->GetWorldContexts())
		{
			if (C.World() && C.World()->IsGameWorld()) { return C.World(); }
		}
		return nullptr;
	}

	// Состояние пакетов с языковым вариантом культуры Code: сколько исходных пакетов загружено, из них — из варианта
	//   `/Game/L10N/<Code>/…` (OkLoc) и из исходного пути (Src). Список — по файлам `Content/L10N/<Code>` (прогон -game
	//   из редактора; в упакованной игре файлов на диске нет — тогда 0 пакетов). FirstSrc — первый «не тот» для журнала.
	struct FL10NState
	{
		int32 Variants = 0;   // пакетов-вариантов культуры на диске
		int32 Loaded = 0;     // из них исходный пакет сейчас загружен
		int32 OkLoc = 0;      // загружен из варианта этой культуры
		int32 Src = 0;        // загружен из исходного пути
		FString FirstSrc;     // первый загруженный из исходного пути (журнал)
	};
	static FL10NState Scan(const FString& Code)
	{
		FL10NState S;
		const FString Dir = FPaths::ProjectContentDir() / TEXT("L10N") / Code;
		const FString Prefix = FString::Printf(TEXT("/Game/L10N/%s/"), *Code);
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Dir, TEXT("*.uasset"), true, false);
		for (const FString& F : Files)
		{
			FString Pkg;
			if (!FPackageName::TryConvertFilenameToLongPackageName(F, Pkg) || !Pkg.StartsWith(Prefix)) { continue; }
			++S.Variants;
			const FString SrcName = TEXT("/Game/") + Pkg.Mid(Prefix.Len());
			const UPackage* P = FindPackage(nullptr, *SrcName);
			if (!P) { continue; }
			++S.Loaded;
			const FString From = P->GetLoadedPath().GetPackageName();
			if (From.StartsWith(Prefix)) { ++S.OkLoc; }
			else { ++S.Src; if (S.FirstSrc.IsEmpty()) { S.FirstSrc = SrcName + TEXT(" <- ") + From; } }
		}
		return S;
	}
}

// `asda.Lang.L10NState [культура]` — какие пакеты с языковым вариантом загружены и откуда (журнал).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdLangL10NState(
	TEXT("asda.Lang.L10NState"),
	TEXT("[ПОРТ] Пакеты с языковым вариантом /Game/L10N/<культура>: загружено, из варианта, из исходного пути. [культура=en]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		const FString Code = Args.Num() > 0 ? Args[0] : FString(TEXT("en"));
		const AsdaLangLive::FL10NState S = AsdaLangLive::Scan(Code);
		UE_LOG(LogAsdaLangSelect, Display, TEXT("[язык] L10N/%s: вариантов %d, загружено %d, из варианта %d, из исходного пути %d %s"),
			*Code, S.Variants, S.Loaded, S.OkLoc, S.Src, *S.FirstSrc);
	}));
#endif // ASDA_DEV_CONSOLE

// `asda.Lang.SelfTestLive <ru|en> [quit]` — проверка ПОСЛЕ выбора языка в окне первого запуска (клик делает
//   `asda.UI.SimClickWidget LANG_SELECT MESSAGE2_BUT00|01` — настоящий Slate): язык сессии и профиль = выбор, культура UE
//   (вне редактора), уровень логина перезагружен (текущий мир — новый), окно выбора закрыто, таблица подписей и кэш
//   навыков — на языке сессии, атласы с языковым вариантом загружены из варианта нужной культуры, а не из исходного пути.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdLangLiveSelfTest(
	TEXT("asda.Lang.SelfTestLive"),
	TEXT("[ПОРТ] Самотест применения языка на лету после окна первого выбора. <ru|en> [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* Hint)
	{
		UWorld* W = AsdaLangLive::GameWorld(Hint);
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaLangSelectSubsystem* LS = GI ? GI->GetSubsystem<UAsdaLangSelectSubsystem>() : nullptr;
		if (!LS) { UE_LOG(LogAsdaLangSelect, Warning, TEXT("САМОТЕСТ-ЯЗЫК-ЖИВОЙ ИТОГ: ok=0 fail=1 (нет мира/подсистемы)")); return; }
		EAsdaLang Want = EAsdaLang::En;
		bool bQuit = false;
		for (const FString& A : Args)
		{
			if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
			EAsdaLang L;
			if (FAsdaLang::ParseCode(A, L)) { Want = L; }
		}
		int32 Ok = 0, Fail = 0;
		auto Check = [&Ok, &Fail](bool b, const FString& What)
		{
			if (b) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaLangSelect, Warning, TEXT("САМОТЕСТ-ЯЗЫК-ЖИВОЙ %s %s"), b ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
		};
		const FString C = FAsdaLang::Code(Want);
		EAsdaLang Saved = EAsdaLang::Ru;
		const bool bSaved = FAsdaLang::GetSaved(Saved);
		Check(FAsdaLang::Current() == Want, FString::Printf(TEXT("язык сессии «%s» (ждали «%s»)"), FAsdaLang::Code(FAsdaLang::Current()), *C));
		Check(bSaved && Saved == Want, FString::Printf(TEXT("профиль «%s»"), bSaved ? FAsdaLang::Code(Saved) : TEXT("пусто")));
		// Выбор совпал с языком сессии (язык системы) — применять нечего (0), иначе ровно одно применение на лету.
		const int32 WantApply = LS->bLastWouldRestart ? 1 : 0;
		Check(LS->LiveApplyCount == WantApply, FString::Printf(TEXT("применений на лету %d (ждали %d; тот же процесс, без перезапуска)"), LS->LiveApplyCount, WantApply));
		Check(!LS->LiveOldWorld.IsValid() && W->GetOutermost()->GetName().EndsWith(FPackageName::GetShortName(LS->LiveReloadedWorld)),
			FString::Printf(TEXT("уровень %s перезагружен: прежний мир уничтожен=%d, текущий %s"), *LS->LiveReloadedWorld,
				!LS->LiveOldWorld.IsValid() ? 1 : 0, *W->GetOutermost()->GetName()));
		Check(!LS->IsOpen(), TEXT("окно выбора языка закрыто и после перезагрузки не показано"));
		if (!GIsEditor)
		{
			const FString Cul = FInternationalization::Get().GetCurrentCulture()->GetName();
			Check(Cul.StartsWith(C), FString::Printf(TEXT("культура UE «%s»"), *Cul));
		}
		// Таблица подписей = Loc(id) языка сессии.
		if (UStringTable* Tbl = LoadObject<UStringTable>(nullptr, FAsdaLang::kCaptionTablePath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			int32 N = 0, Bad = 0;
			FString FirstBad;
			Tbl->GetStringTable()->EnumerateSourceStrings([&](const FString& Key, const FString& Src)
			{
				++N;
				const FString WantS = FAsdaLoginData::Loc(FCString::Atoi(*Key));
				if (!WantS.IsEmpty() && Src != WantS) { if (!Bad++) { FirstBad = FString::Printf(TEXT("%s: «%s» ≠ «%s»"), *Key, *Src, *WantS); } }
				return true;
			});
			Check(N > 0 && Bad == 0, FString::Printf(TEXT("подписи окон: записей %d, не на языке сессии %d %s"), N, Bad, *FirstBad));
		}
		else { Check(false, TEXT("таблица подписей не загрузилась")); }
		// Кэш навыков (собран в `Initialize` ДО выбора) пересобран: имя и описание = оверлей `skilltooltip` языка сессии
		//   (тот же источник, что второй проход `UAsdaSkillDatabase::Load`; при повторе id побеждает последняя строка).
		{
			UAsdaSkillDatabase* Db = GI->GetSubsystem<UAsdaSkillDatabase>();
			UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
			UDataTable* Dt = IT ? IT->GetDataTable(FName(TEXT("skilltooltip"))) : nullptr;
			TMap<int32, TPair<FString, FString>> WantSk;   // id → (имя, описание), ожидаемые на языке сессии
			int32 Diff = 0;
			if (Db && Dt)
			{
				for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
				{
					const FAsdaSkilltooltipRow* R = reinterpret_cast<const FAsdaSkilltooltipRow*>(It.Value);
					if (!R || !Db->Get(R->Id)) { continue; }
					TPair<FString, FString>& P = WantSk.FindOrAdd(R->Id);
					if (!R->Name.IsEmpty()) { P.Key = FAsdaLang::ResolveRow(Dt, It.Key, TEXT("Name"), R->Name); if (P.Key != R->Name) { ++Diff; } }
					P.Value = FAsdaLang::ResolveRow(Dt, It.Key, TEXT("Text"), R->Text);
				}
			}
			int32 Bad = 0;
			FString FirstBad;
			for (const TPair<int32, TPair<FString, FString>>& It : WantSk)
			{
				const FAsdaSkillDef* D = Db->Get(It.Key);
				const bool bNameOk = It.Value.Key.IsEmpty() || D->Name == It.Value.Key;
				if (!bNameOk || D->Description != It.Value.Value)
				{
					if (!Bad++) { FirstBad = FString::Printf(TEXT("%d: «%s» ≠ «%s»"), It.Key, *D->Name, *It.Value.Key); }
				}
			}
			Check(WantSk.Num() > 0 && Bad == 0, FString::Printf(TEXT("кэш навыков: сверено %d (имя перевода отличается от RU у %d), расхождений %d %s"), WantSk.Num(), Diff, Bad, *FirstBad));
		}
		// Атласы с языковым вариантом: загружены из варианта нужной культуры (в редакторе движок их не подменяет).
		if (!GIsEditor)
		{
			static const TCHAR* const kCul[] = { TEXT("en"), TEXT("ar") };
			for (const TCHAR* Cul : kCul)
			{
				const AsdaLangLive::FL10NState S = AsdaLangLive::Scan(Cul);
				if (S.Variants == 0) { continue; }
				if (C == Cul)
				{
					Check(S.Src == 0 && S.Loaded > 0, FString::Printf(TEXT("L10N/%s: вариантов %d, загружено %d — из варианта %d, из исходного пути %d %s"),
						Cul, S.Variants, S.Loaded, S.OkLoc, S.Src, *S.FirstSrc));
				}
				else
				{
					Check(S.OkLoc == 0, FString::Printf(TEXT("L10N/%s (не язык сессии): из варианта %d — должно быть 0; загружено %d"), Cul, S.OkLoc, S.Loaded));
				}
			}
		}
		UE_LOG(LogAsdaLangSelect, Warning, TEXT("САМОТЕСТ-ЯЗЫК-ЖИВОЙ ИТОГ: ok=%d fail=%d"), Ok, Fail);
		AsdaSelfTestKit::Quit(W, bQuit);
	}));
#endif // ASDA_DEV_CONSOLE

#if WITH_EDITOR
// ── ★ 10.10.2026: самотест ветки PIE в НАСТОЯЩЕМ редакторе (`asda.Lang.SelfTestPie [quit]`, запуск редактора с
//    -RenderOffscreen и своим -saveddirsuffix — профиль редактора временный). Сценарий владельца целиком:
//    1) профиль en → старт PIE → язык сеанса en, подписи окон en (в памяти);
//    2) «Настройки → Русский → Перезапустить» = профиль ru + `FAsdaLang::RestartGame` → PIE останавливается и стартует
//       заново сам, язык нового сеанса ru (прежде в редакторе согласие молча ничего не делало);
//    3) профиль без языка → перезапуск → на логине окно первого выбора; клик (делегат OnClicked) по другому языку →
//       язык применён на лету, уровень логина перезагружен, окно закрыто, подписи на новом языке;
//    4) остановка PIE → язык редактора прежний, таблица подписей = строки ассета (ассет не изменён).
namespace AsdaLangPieTest
{
	// Состояние сценария (живёт в тикере).
	struct FRun
	{
		int32 Step = 0;                         // шаг сценария
		double StepT = 0.0;                     // момент входа в шаг
		int32 PieStarts = 0;                    // сколько раз начинался PIE (PreBeginPIE)
		int32 PieStartsAtStep = 0;              // PieStarts на входе в шаг
		int32 Ok = 0, Fail = 0;                 // итог
		bool bQuit = false;                     // выйти из редактора в конце
		EAsdaLang EditorLang = EAsdaLang::Ru;   // язык редактора до теста
		EAsdaLang PickL = EAsdaLang::En;        // язык клика в окне первого выбора
		TMap<FString, FString> Orig;            // строки таблицы подписей в ассете (до PIE)
		FDelegateHandle HPre;                   // подписка PreBeginPIE
		// ★ 10.10.2026 (ревизия СРЕДН-1): профиль редактора до теста — возвращается в `Finish` на всех путях.
		bool bProfileSaved = false;             // профиль запомнен (тест начал его менять) — возвращать в `Finish`
		FString ProfileLang;                    // исходное поле `Language` профиля (пустое = язык не выбран)
		FString ProfilePath;                    // файл профиля редактора на диске (Saved*/Config/<платформа>/GameUserSettings.ini)
		bool bProfileFileExisted = false;       // файл профиля существовал до теста
		TArray<uint8> ProfileBytes;             // байты файла профиля до теста — сверка «побайтно как до»
	};

	static void Check(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok; } else { ++R.Fail; }
		UE_LOG(LogAsdaLangSelect, Warning, TEXT("САМОТЕСТ-ЯЗЫК-PIE %s %s"), b ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
	}

	// Таблица подписей: (записей, не равных Loc языка сессии, равных строке ассета).
	static void CaptionStats(const FRun& R, int32& N, int32& NotLoc, int32& SameAsAsset)
	{
		N = NotLoc = SameAsAsset = 0;
		const UStringTable* T = FindObject<UStringTable>(nullptr, FAsdaLang::kCaptionTablePath);
		if (!T) { return; }
		T->GetStringTable()->EnumerateSourceStrings([&](const FString& Key, const FString& Src)
		{
			++N;
			const FString W = FAsdaLoginData::Loc(FCString::Atoi(*Key));
			if (!W.IsEmpty() && W != Src) { ++NotLoc; }
			if (const FString* O = R.Orig.Find(Key)) { if (*O == Src) { ++SameAsAsset; } }
			return true;
		});
	}

	// Игровой мир PIE и его подсистема выбора языка.
	static UAsdaLangSelectSubsystem* PieLangSel(UWorld*& OutW)
	{
		OutW = GEditor ? GEditor->PlayWorld.Get() : nullptr;
		UGameInstance* GI = OutW ? OutW->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaLangSelectSubsystem>() : nullptr;
	}

	// Сеанс PIE поднят и прожил не меньше Sec секунд с входа в шаг.
	static bool PieReady(const FRun& R, double Sec)
	{
		UWorld* W = nullptr;
		return GEditor && GEditor->IsPlayingSessionInEditor() && PieLangSel(W) && W->HasBegunPlay()
			&& FPlatformTime::Seconds() - R.StepT > Sec;
	}

	static void Next(FRun& R) { ++R.Step; R.StepT = FPlatformTime::Seconds(); R.PieStartsAtStep = R.PieStarts; }

	// ★ 10.10.2026 (ревизия СРЕДН-1): вернуть поле `Language` профиля и сохранить профиль (`SaveSettings`), затем сверить
	//   файл профиля с байтами до теста. Если движок записал файл иначе (порядок/форма ключей, файла не было) — вернуть
	//   исходные байты (удалить файл, если его не было): профиль владельца после самотеста обязан быть таким же.
	static void RestoreProfile(FRun& R)
	{
		if (!R.bProfileSaved) { return; }
		R.bProfileSaved = false;
		UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
		if (P)
		{
			P->Language = R.ProfileLang;
			P->SaveSettings();
		}
		TArray<uint8> Now;
		const bool bNow = FFileHelper::LoadFileToArray(Now, *R.ProfilePath, FILEREAD_Silent);
		bool bSame = (bNow == R.bProfileFileExisted) && (!bNow || Now == R.ProfileBytes);
		const bool bByEngine = bSame;
		if (!bSame)
		{
			// Игра в PIE пишет в тот же профиль и другие поля (разрешение окна PIE, замер производительности, группы
			//   качества, служебные разделы) — они живут в памяти конфига и записались бы снова при выходе редактора.
			//   Вернуть файл и перечитать профиль С ДИСКА (кэш конфига + объект настроек), чтобы память = файл до теста.
			const bool bWrote = R.bProfileFileExisted ? FFileHelper::SaveArrayToFile(R.ProfileBytes, *R.ProfilePath)
				: IFileManager::Get().Delete(*R.ProfilePath, false, true, true);
			if (bWrote && P) { P->LoadSettings(/*bForceReload*/ true); }
			Now.Reset();
			const bool bNow2 = FFileHelper::LoadFileToArray(Now, *R.ProfilePath, FILEREAD_Silent);
			bSame = bWrote && (bNow2 == R.bProfileFileExisted) && (!bNow2 || Now == R.ProfileBytes) && (!P || P->Language == R.ProfileLang);
		}
		Check(R, P && bSame, FString::Printf(TEXT("профиль редактора возвращён: Language=«%s», файл %s %s"), *R.ProfileLang, *R.ProfilePath,
			!bSame ? TEXT("НЕ совпал с исходным") : bByEngine ? TEXT("побайтно как до теста (SaveSettings)")
				: TEXT("побайтно как до теста (SaveSettings записал и поля, изменённые игрой в PIE, — возвращены исходные байты, профиль перечитан с диска)")));
	}

	static void Finish(TSharedRef<FRun> R)
	{
		RestoreProfile(*R);
		FEditorDelegates::PreBeginPIE.Remove(R->HPre);
		UE_LOG(LogAsdaLangSelect, Warning, TEXT("САМОТЕСТ-ЯЗЫК-PIE ИТОГ: ok=%d fail=%d"), R->Ok, R->Fail);
		if (R->bQuit) { FPlatformMisc::RequestExit(false, TEXT("asda.Lang.SelfTestPie")); }
	}
}

static FAutoConsoleCommandWithWorldAndArgs GCmdLangPieSelfTest(
	TEXT("asda.Lang.SelfTestPie"),
	TEXT("[ПОРТ] Самотест языка в PIE (только редактор вне PIE): язык из профиля на старте PIE, перезапуск сеанса PIE, окно первого выбора и язык на лету, возврат подписей после PIE. [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace AsdaLangPieTest;
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->bQuit = Args.ContainsByPredicate([](const FString& A) { return A.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
		if (!GIsEditor || !GEditor || GEditor->IsPlaySessionInProgress() || !UAsdaGameUserSettings::Get())
		{
			Check(*R, false, TEXT("нужен редактор вне PIE с профилем игры"));
			Finish(R);
			return;
		}
		R->EditorLang = FAsdaLang::Current();
		if (const UStringTable* T = LoadObject<UStringTable>(nullptr, FAsdaLang::kCaptionTablePath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			T->GetStringTable()->EnumerateSourceStrings([&R](const FString& K, const FString& S) { R->Orig.Add(K, S); return true; });
		}
		Check(*R, R->Orig.Num() > 0, FString::Printf(TEXT("таблица подписей ассета: %d записей; язык редактора «%s»"), R->Orig.Num(), FAsdaLang::Code(R->EditorLang)));
		R->HPre = FEditorDelegates::PreBeginPIE.AddLambda([R](bool) { ++R->PieStarts; });
		// Профиль редактора до первой правки — вернётся в `Finish` (успех, таймаут, отказ).
		R->ProfileLang = UAsdaGameUserSettings::Get()->Language;
		// GGameUserSettingsIni в UE5 — ключ кэша конфигов, не путь: файл на диске — как в `UGameUserSettings` (GetDestIniFilename).
		R->ProfilePath = FPaths::ConvertRelativePathToFull(FConfigCacheIni::GetDestIniFilename(TEXT("GameUserSettings"), nullptr, *FPaths::GeneratedConfigDir()));
		R->bProfileFileExisted = FFileHelper::LoadFileToArray(R->ProfileBytes, *R->ProfilePath, FILEREAD_Silent);
		R->bProfileSaved = true;
		UE_LOG(LogAsdaLangSelect, Log, TEXT("САМОТЕСТ-ЯЗЫК-PIE профиль до теста: Language=«%s», файл %s (%s, %d б)"), *R->ProfileLang, *R->ProfilePath,
			R->bProfileFileExisted ? TEXT("есть") : TEXT("нет"), R->ProfileBytes.Num());
		FAsdaLang::SetSaved(EAsdaLang::En, true);
		GEditor->RequestPlaySession(FRequestPlaySessionParams());
		Next(*R);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([R](float) -> bool
		{
			using namespace AsdaLangPieTest;
			const double Age = FPlatformTime::Seconds() - R->StepT;
			if (Age > 240.0) { Check(*R, false, FString::Printf(TEXT("шаг %d не завершился за 240 с"), R->Step)); if (GEditor) { GEditor->RequestEndPlayMap(); } Finish(R); return false; }
			UWorld* W = nullptr;
			UAsdaLangSelectSubsystem* LS = PieLangSel(W);
			int32 N = 0, NotLoc = 0, Same = 0;
			switch (R->Step)
			{
			case 1:   // PIE с профилем en
				if (!PieReady(*R, 6.0)) { return true; }
				CaptionStats(*R, N, NotLoc, Same);
				Check(*R, FAsdaLang::IsPieSession() && FAsdaLang::Current() == EAsdaLang::En, FString::Printf(TEXT("старт PIE: язык сеанса «%s» из профиля (ждали en)"), FAsdaLang::Code(FAsdaLang::Current())));
				Check(*R, N > 0 && NotLoc == 0 && Same < N, FString::Printf(TEXT("подписи окон в PIE на en: записей %d, не на языке сеанса %d, совпали с RU-ассетом %d"), N, NotLoc, Same));
				// «Настройки → Русский → ОК → Перезапустить»: профиль ru + перезапуск.
				FAsdaLang::SetSaved(EAsdaLang::Ru, true);
				Check(*R, FAsdaLang::RestartGame(), TEXT("RestartGame в PIE вернул true (перезапуск сеанса заказан)"));
				Next(*R);
				return true;
			case 2:   // новый сеанс PIE — ru
				if (R->PieStarts <= R->PieStartsAtStep || !PieReady(*R, 6.0)) { return true; }
				CaptionStats(*R, N, NotLoc, Same);
				Check(*R, FAsdaLang::Current() == EAsdaLang::Ru, FString::Printf(TEXT("PIE перезапущен сам (стартов %d); язык нового сеанса «%s» (ждали ru)"), R->PieStarts, FAsdaLang::Code(FAsdaLang::Current())));
				Check(*R, N > 0 && NotLoc == 0, FString::Printf(TEXT("подписи окон на ru: записей %d, не на языке сеанса %d"), N, NotLoc));
				// Профиль без языка → перезапуск → окно первого выбора.
				UAsdaGameUserSettings::Get()->Language.Empty();
				UAsdaGameUserSettings::Get()->SaveSettings();
				Check(*R, FAsdaLang::RestartGame(), TEXT("второй перезапуск сеанса заказан (профиль без языка)"));
				Next(*R);
				return true;
			case 3:   // окно первого выбора в PIE → клик
			{
				if (R->PieStarts <= R->PieStartsAtStep || !PieReady(*R, 6.0)) { return true; }
				Check(*R, LS && LS->IsOpen(), FString::Printf(TEXT("язык не выбран → в PIE окно первого выбора открыто (сеанс «%s», язык системы)"), FAsdaLang::Code(FAsdaLang::Current())));
				R->PickL = (FAsdaLang::Current() == EAsdaLang::Ru) ? EAsdaLang::En : EAsdaLang::Ru;
				UUserWidget* Win = nullptr;
				if (W && W->GetGameInstance()) { if (UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { Win = WM->FindWindow(UAsdaLangSelectSubsystem::kWindowId); } }
				UButton* B = Win ? Cast<UButton>(Win->GetWidgetFromName(FName(R->PickL == EAsdaLang::En ? UAsdaLangSelectSubsystem::kBtnEn : UAsdaLangSelectSubsystem::kBtnRu))) : nullptr;
				Check(*R, B != nullptr, FString::Printf(TEXT("кнопка «%s» найдена"), FAsdaLang::Code(R->PickL)));
				if (B) { B->OnClicked.Broadcast(); }
				Next(*R);
				return true;
			}
			case 4:   // язык на лету: уровень логина перезагружен
				if (!LS || LS->LiveOldWorld.IsValid() || !PieReady(*R, 5.0)) { return true; }
				CaptionStats(*R, N, NotLoc, Same);
				Check(*R, FAsdaLang::Current() == R->PickL && LS->LiveApplyCount == 1 && R->PieStarts == R->PieStartsAtStep,
					FString::Printf(TEXT("язык на лету «%s» без перезапуска PIE (применений %d, стартов PIE %d)"), FAsdaLang::Code(FAsdaLang::Current()), LS->LiveApplyCount, R->PieStarts));
				Check(*R, !LS->IsOpen(), TEXT("после перезагрузки уровня логина окно выбора не показано"));
				Check(*R, N > 0 && NotLoc == 0, FString::Printf(TEXT("подписи окон на «%s»: записей %d, не на языке сеанса %d"), FAsdaLang::Code(R->PickL), N, NotLoc));
				GEditor->RequestEndPlayMap();
				Next(*R);
				return true;
			case 5:   // PIE остановлен: ассет и язык редактора — как были
				if (GEditor->IsPlaySessionInProgress() || Age < 2.0) { return true; }
				CaptionStats(*R, N, NotLoc, Same);
				Check(*R, !FAsdaLang::IsPieSession() && FAsdaLang::Current() == R->EditorLang, FString::Printf(TEXT("после PIE язык редактора «%s» (был «%s»)"), FAsdaLang::Code(FAsdaLang::Current()), FAsdaLang::Code(R->EditorLang)));
				Check(*R, N == R->Orig.Num() && Same == N, FString::Printf(TEXT("таблица подписей = строки ассета: %d из %d"), Same, N));
				{
					const UStringTable* T = FindObject<UStringTable>(nullptr, FAsdaLang::kCaptionTablePath);
					Check(*R, T && !T->GetOutermost()->IsDirty(), TEXT("пакет таблицы подписей не помечен изменённым"));
				}
				Finish(R);
				return false;
			default:
				Finish(R);
				return false;
			}
		}));
	}));

// ── ★ 10.10.2026 (en_completeness): самотест КАРТИНОК и НАДПИСЕЙ в настоящем PIE (`asda.Lang.SelfTestPieAssets [quit]`,
//    редактор с -RenderOffscreen и своим -saveddirsuffix). Профиль en → PIE: (1) пары L10N-текстур скопированы в
//    видеопамять RU-текстур, отказов нет; (2) предпросмотр языка игры включён, вбитые надписи WBP выбора персонажа
//    показывают перевод (сверка с оверлеем UiLiteral по ключу текста); (3) имя NPC из тика ВНЕ мира PIE — английское;
//    (4) снимок окон «Рюкзак», «Профиль», выбор персонажа (`asda.UI.Shot pieassets_en`); остановка PIE → RU-ресурсы
//    восстановлены, предпросмотр выключен, надпись снова русская; профиль редактора возвращён побайтно.
//    ★ 10.10.2026 (en_terms_fix, МЕЛК-3): после PIE ещё — пакеты всех пар (RU и L10N) и WBP_CharSelect не помечены
//    изменёнными, GPU-ресурс каждой RU-текстуры пересоздан (указатель ресурса/RHI не тот, что был в PIE).
namespace AsdaLangPieAssetsTest
{
	using AsdaLangPieTest::FRun;
	using AsdaLangPieTest::Check;

	// Ключ строки оверлея UiLiteral для текста (как у генератора: «ns|key», пустое пространство имён — просто ключ).
	static FString RowKeyOf(const FText& T)
	{
		const TOptional<FString> Ns = FTextInspector::GetNamespace(T);
		const TOptional<FString> Key = FTextInspector::GetKey(T);
		if (!Key.IsSet() || Key->IsEmpty()) { return FString(); }
		return (Ns.IsSet() && !Ns->IsEmpty()) ? *Ns + TEXT("|") + *Key : *Key;
	}

	// Перевод оверлея без экранирования генератора (`\n` двумя символами → перевод строки).
	static FString OverlayText(const FString& Row)
	{
		FString S;
		if (!FAsdaLang::FindOverlay(EAsdaLang::En, TEXT("UiLiteral"), Row, S)) { return FString(); }
		return S.Replace(TEXT("\\n"), TEXT("\n")).Replace(TEXT("\\r"), TEXT("\r")).Replace(TEXT("\\t"), TEXT("\t")).Replace(TEXT("\\\\"), TEXT("\\"));
	}

	// Надписи виджета W с переводом в оверлее: (всего, показан перевод, показан исходник). Образец — в OutSample.
	static void LiteralStats(UUserWidget* W, int32& N, int32& Translated, int32& Source, FString& OutSample)
	{
		N = Translated = Source = 0;
		if (!W || !W->WidgetTree) { return; }
		W->WidgetTree->ForEachWidget([&](UWidget* Wd)
		{
			const UTextBlock* T = Cast<UTextBlock>(Wd);
			if (!T) { return; }
			const FText Tx = T->GetText();
			const FString Row = RowKeyOf(Tx);
			const FString En = Row.IsEmpty() ? FString() : OverlayText(Row);
			if (En.IsEmpty()) { return; }
			++N;
			const FString Shown = Tx.ToString();
			if (Shown == En) { ++Translated; }
			else if (const FString* Src = FTextInspector::GetSourceString(Tx)) { if (Shown == *Src) { ++Source; } }
			if (OutSample.IsEmpty()) { OutSample = FString::Printf(TEXT("%s: «%s»"), *Wd->GetName(), *Shown); }
		});
	}

	// ★ 10.10.2026 (en_terms_fix, МЕЛК-3): снимок пары картинок в PIE — текстуры и GPU-ресурс RU-текстуры, в который
	//   скопирован L10N-вариант. После PIE ресурс обязан быть пересоздан (`UpdateResource`), пакеты — не изменены.
	struct FPieTexSnap
	{
		TWeakObjectPtr<UTexture2D> Ru;         // RU-текстура (/Game/X)
		TWeakObjectPtr<UTexture2D> Loc;        // её L10N-вариант (/Game/L10N/<lang>/X)
		const void* Res = nullptr;             // FTextureResource RU-текстуры в PIE (с английскими пикселями)
		const void* Rhi = nullptr;             // её RHI-текстура в PIE
		// ★ 10.10.2026 (terms_minors, МЕЛК-2 ревизии): копия в ресурс была (иначе пара отклонена — sRGB/формат — и её
		//   ресурс после PIE не пересоздаётся по замыслу; такая пара в проверку пересоздания не входит).
		bool bCopied = false;
		// ★ 10.10.2026 (terms_minors): удерживаемая ссылка на RHI-текстуру PIE — пока она жива, память RHI не
		//   освобождается, и новый ресурс не может получить тот же адрес (сравнение указателей без ложного «прежний»).
		FTextureRHIRef RhiHold;
	};

	// GPU-ресурс текстуры и его RHI (сверка «пересоздан ли» после PIE; только сравнение указателей).
	static void ResOf(const UTexture2D* T, const void*& OutRes, const void*& OutRhi)
	{
		const FTextureResource* Res = T ? T->GetResource() : nullptr;
		OutRes = Res;
		OutRhi = Res ? Res->TextureRHI.GetReference() : nullptr;
	}

	// Итог, возврат профиля редактора и выход (по ключу quit).
	static void FinishAssets(TSharedRef<FRun> R)
	{
		AsdaLangPieTest::RestoreProfile(*R);
		FEditorDelegates::PreBeginPIE.Remove(R->HPre);
		UE_LOG(LogAsdaLangSelect, Warning, TEXT("САМОТЕСТ-PIE-АССЕТЫ ИТОГ: ok=%d fail=%d"), R->Ok, R->Fail);
		if (R->bQuit) { FPlatformMisc::RequestExit(false, TEXT("asda.Lang.SelfTestPieAssets")); }
	}
}

static FAutoConsoleCommandWithWorldAndArgs GCmdLangPieAssetsSelfTest(
	TEXT("asda.Lang.SelfTestPieAssets"),
	TEXT("[ПОРТ] Самотест картинок L10N и вбитых надписей WBP в PIE на английском (только редактор вне PIE). [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace AsdaLangPieAssetsTest;
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->bQuit = Args.ContainsByPredicate([](const FString& A) { return A.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
		if (!GIsEditor || !GEditor || GEditor->IsPlaySessionInProgress() || !UAsdaGameUserSettings::Get())
		{
			Check(*R, false, TEXT("нужен редактор вне PIE с профилем игры"));
			FinishAssets(R);
			return;
		}
		R->EditorLang = FAsdaLang::Current();
		R->HPre = FEditorDelegates::PreBeginPIE.AddLambda([R](bool) { ++R->PieStarts; });
		// Профиль редактора до первой правки — вернётся в `Finish` (как в asda.Lang.SelfTestPie).
		R->ProfileLang = UAsdaGameUserSettings::Get()->Language;
		R->ProfilePath = FPaths::ConvertRelativePathToFull(FConfigCacheIni::GetDestIniFilename(TEXT("GameUserSettings"), nullptr, *FPaths::GeneratedConfigDir()));
		R->bProfileFileExisted = FFileHelper::LoadFileToArray(R->ProfileBytes, *R->ProfilePath, FILEREAD_Silent);
		R->bProfileSaved = true;
		FAsdaLang::SetSaved(EAsdaLang::En, true);
		GEditor->RequestPlaySession(FRequestPlaySessionParams());
		AsdaLangPieTest::Next(*R);
		// Надпись-образец для проверки «после PIE снова русская»: пространство имён, ключ и исходник.
		TSharedRef<FString> ProbeNs = MakeShared<FString>(), ProbeKey = MakeShared<FString>(), ProbeSrc = MakeShared<FString>();
		// ★ 10.10.2026 (en_terms_fix, МЕЛК-3): пары картинок и их GPU-ресурсы в PIE — для проверки после остановки PIE.
		TSharedRef<TArray<FPieTexSnap>> Snaps = MakeShared<TArray<FPieTexSnap>>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([R, ProbeNs, ProbeKey, ProbeSrc, Snaps](float) -> bool
		{
			using namespace AsdaLangPieAssetsTest;
			const double Age = FPlatformTime::Seconds() - R->StepT;
			if (Age > 240.0) { Check(*R, false, FString::Printf(TEXT("шаг %d не завершился за 240 с"), R->Step)); if (GEditor) { GEditor->RequestEndPlayMap(); } FinishAssets(R); return false; }
			UWorld* W = nullptr;
			AsdaLangPieTest::PieLangSel(W);
			UAsdaWindowSubsystem* WM = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			switch (R->Step)
			{
			case 1:   // PIE поднят (12 с — копия текстур с повторами и асинхронный предпросмотр)
			{
				if (!AsdaLangPieTest::PieReady(*R, 12.0) || !WM) { return true; }
				int32 Pairs = 0, Copied = 0, Rejected = 0;
				FAsdaLang::PieTextureStats(Pairs, Copied, Rejected);
				// Текстуры редактор может ещё собирать (асинхронно) — ждать, пока каждая пара скопирована или отклонена (до 150 с).
				if ((Pairs == 0 || Copied + Rejected < Pairs) && Age < 150.0) { return true; }
				Check(*R, FAsdaLang::IsPieSession() && FAsdaLang::Current() == EAsdaLang::En, FString::Printf(TEXT("PIE: язык сеанса «%s» (ждали en)"), FAsdaLang::Code(FAsdaLang::Current())));
				// ★ 10.10.2026 (terms_minors, МЕЛК-2 ревизии): законно отклонённая пара (разный sRGB/формат — свойство ассета,
				//   предупреждение пишет `PieTextureStats`) провалом не считается — выводится отдельной строкой ниже.
				Check(*R, Pairs > 0 && Copied > 0 && Copied + Rejected == Pairs, FString::Printf(TEXT("картинки L10N в PIE: пар %d, скопировано %d, отказов %d"), Pairs, Copied, Rejected));
				{
					// ★ 10.10.2026 (en_terms_fix, МЕЛК-3): запомнить пары и GPU-ресурсы RU-текстур с английскими пикселями.
					TArray<TWeakObjectPtr<UTexture2D>> Ru, Loc;
					TArray<bool> CopiedFlags;   // ★ terms_minors: копия была (по индексу пары)
					FAsdaLang::PieTexturePairs(Ru, Loc, CopiedFlags);
					FlushRenderingCommands();   // ресурсы стабильны: все заказанные копии выполнены
					int32 NRej = 0;             // отклонённых пар (не провал)
					for (int32 i = 0; i < Ru.Num(); ++i)
					{
						FPieTexSnap S;
						S.Ru = Ru[i];
						S.Loc = Loc.IsValidIndex(i) ? Loc[i] : nullptr;
						S.bCopied = CopiedFlags.IsValidIndex(i) && CopiedFlags[i];
						ResOf(S.Ru.Get(), S.Res, S.Rhi);
						if (const FTextureResource* TR = S.Ru.IsValid() ? S.Ru->GetResource() : nullptr) { S.RhiHold = TR->TextureRHI; }
						if (!S.bCopied)
						{
							++NRej;
							UE_LOG(LogAsdaLangSelect, Warning, TEXT("САМОТЕСТ-PIE-АССЕТЫ: пара отклонена (не провал, пересоздание не проверяется): %s"),
								S.Ru.IsValid() ? *S.Ru->GetPathName() : TEXT("?"));
						}
						Snaps->Add(S);
					}
					UE_LOG(LogAsdaLangSelect, Warning, TEXT("САМОТЕСТ-PIE-АССЕТЫ: пар %d, со скопированной картинкой %d, отклонённых %d (отдельно, не провал)"),
						Ru.Num(), Ru.Num() - NRej, NRej);
				}
				Check(*R, FTextLocalizationManager::Get().IsGameLocalizationPreviewEnabled(), TEXT("предпросмотр языка игры включён на время PIE"));
				// Окна как на снимках владельца: «Рюкзак», «Профиль», выбор персонажа (монолит — поверх).
				WM->OpenWindowByPath(FName(TEXT("INVEN_WIN")), TEXT("/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_INVEN_WIN"), 40);
				WM->OpenWindowByPath(FName(TEXT("CHARAINFO_WIN")), TEXT("/Game/AsdaAssets/UI/Player_UI/Character/WBP_CHARAINFO_WIN"), 41);
				UUserWidget* Sel = WM->OpenWindowByPath(FName(TEXT("CHARSELECT_WIN")), TEXT("/Game/AsdaAssets/UI/Login/WBP_CharSelect"), 15, ESlateVisibility::SelfHitTestInvisible);
				int32 N = 0, Tr = 0, Src = 0;
				FString Sample;
				LiteralStats(Sel, N, Tr, Src, Sample);
				Check(*R, N > 0 && Tr == N, FString::Printf(TEXT("выбор персонажа в PIE: надписей с переводом %d, показан перевод %d, исходник %d (%s)"), N, Tr, Src, *Sample));
				if (Sel && Sel->WidgetTree)
				{
					Sel->WidgetTree->ForEachWidget([&](UWidget* Wd)
					{
						const UTextBlock* T = Cast<UTextBlock>(Wd);
						if (!T || !ProbeKey->IsEmpty()) { return; }
						const FText Tx = T->GetText();
						const FString Row = RowKeyOf(Tx);
						const FString* S = FTextInspector::GetSourceString(Tx);
						if (Row.IsEmpty() || !S || OverlayText(Row).IsEmpty()) { return; }
						*ProbeNs = FTextInspector::GetNamespace(Tx).Get(FString());
						*ProbeKey = FTextInspector::GetKey(Tx).Get(FString());
						*ProbeSrc = *S;
					});
				}
				// Имя NPC из тика редактора (вне мира PIE — как «удочерение» 4067 из тика сети).
				FString EnName;
				const bool bHas = FAsdaLang::FindOverlay(EAsdaLang::En, TEXT("NpcNames"), TEXT("37.Name"), EnName);
				const FString Nm = AsdaEntityText::NpcName(37);
				Check(*R, bHas && Nm == EnName, FString::Printf(TEXT("имя NPC 37 вне тика мира PIE: «%s» (оверлей «%s»)"), *Nm, *EnName));
				AsdaLangPieTest::Next(*R);
				return true;
			}
			case 2:   // кадр со всеми окнами
				if (Age < 2.0) { return true; }
				if (GEngine && W) { GEngine->Exec(W, TEXT("asda.UI.Shot pieassets_en")); }
				AsdaLangPieTest::Next(*R);
				return true;
			case 3:   // кадр записан → остановить PIE
				if (Age < 3.0) { return true; }
				GEditor->RequestEndPlayMap();
				AsdaLangPieTest::Next(*R);
				return true;
			case 4:   // PIE остановлен: картинки и надписи — назад к ассетам
			{
				if (GEditor->IsPlaySessionInProgress() || Age < 3.0) { return true; }
				int32 Pairs = 0, Copied = 0, Rejected = 0;
				FAsdaLang::PieTextureStats(Pairs, Copied, Rejected);
				Check(*R, Pairs == 0, FString::Printf(TEXT("после PIE подмена текстур снята (пар в работе %d)"), Pairs));
				{
					// ★ 10.10.2026 (en_terms_fix, МЕЛК-3): кроме структуры — сами пакеты и ресурсы. Ни RU-, ни L10N-текстура и
					//   окно выбора персонажа не помечены изменёнными (копия жила только в видеопамяти); GPU-ресурс каждой
					//   RU-текстуры пересоздан из данных ассета (иначе в редакторе остались бы английские пиксели).
					int32 Lost = 0, DirtyRu = 0, DirtyLoc = 0, Stale = 0;
					int32 NCopied = 0;   // ★ terms_minors: пар, у которых копия была (только они проверяются на пересоздание)
					FString FirstBad;
					for (const FPieTexSnap& S : *Snaps)
					{
						const UTexture2D* Ru = S.Ru.Get();
						const UTexture2D* Loc = S.Loc.Get();
						if (!Ru || !Loc) { ++Lost; continue; }
						const bool bDr = Ru->GetOutermost()->IsDirty();
						const bool bDl = Loc->GetOutermost()->IsDirty();
						const void* Res = nullptr;
						const void* Rhi = nullptr;
						ResOf(Ru, Res, Rhi);
						// Ресурс тот же, что был в PIE. ★ terms_minors: только у пар с копией (отклонённую пару `RestorePieTextures`
						//   не пересоздаёт по замыслу); адрес RHI не может быть переиспользован — PIE-ресурс удерживается `RhiHold`.
						const bool bStale = S.bCopied && (Res == S.Res && Rhi == S.Rhi);
						NCopied += S.bCopied ? 1 : 0;
						DirtyRu += bDr ? 1 : 0;
						DirtyLoc += bDl ? 1 : 0;
						Stale += bStale ? 1 : 0;
						if ((bDr || bDl || bStale) && FirstBad.IsEmpty()) { FirstBad = Ru->GetPathName(); }
					}
					const int32 NSnap = Snaps->Num();
					Check(*R, NSnap > 0 && Lost == 0 && DirtyRu == 0 && DirtyLoc == 0,
						FString::Printf(TEXT("после PIE пакеты картинок не помечены изменёнными: пар %d, выгружено %d, изменённых RU %d, L10N %d%s%s"),
							NSnap, Lost, DirtyRu, DirtyLoc, FirstBad.IsEmpty() ? TEXT("") : TEXT("; первая: "), *FirstBad));
					Check(*R, NSnap > 0 && NCopied > 0 && Stale == 0,
						FString::Printf(TEXT("после PIE GPU-ресурсы RU-картинок пересозданы из ассета: %d из %d скопированных (прежний ресурс у %d; отклонённых пар %d — не проверяются)"),
							NCopied - Stale, NCopied, Stale, NSnap - Lost - NCopied));
					// PIE-ресурсы больше не нужны — отпустить удерживаемые RHI (освобождение отложенное, через поток рендера).
					for (FPieTexSnap& S : *Snaps) { S.RhiHold.SafeRelease(); }
					const UPackage* SelPkg = FindObject<UPackage>(nullptr, TEXT("/Game/AsdaAssets/UI/Login/WBP_CharSelect"));
					Check(*R, SelPkg && !SelPkg->IsDirty(), FString::Printf(TEXT("после PIE окно выбора персонажа WBP_CharSelect не помечено изменённым (%s)"),
						!SelPkg ? TEXT("пакет не загружен") : SelPkg->IsDirty() ? TEXT("ИЗМЕНЁН") : TEXT("чистый")));
				}
				Check(*R, !FTextLocalizationManager::Get().IsGameLocalizationPreviewEnabled(), TEXT("после PIE предпросмотр языка игры выключен"));
				if (!ProbeKey->IsEmpty())
				{
					const FString Shown = FText::AsLocalizable_Advanced(*ProbeNs, *ProbeKey, *ProbeSrc).ToString();
					Check(*R, Shown == *ProbeSrc, FString::Printf(TEXT("после PIE надпись «%s» снова как в ассете: «%s»"), **ProbeSrc, *Shown));
				}
				else
				{
					Check(*R, false, TEXT("нет надписи-образца для проверки после PIE"));
				}
				FinishAssets(R);
				return false;
			}
			default:
				FinishAssets(R);
				return false;
			}
		}));
	}));
#endif
