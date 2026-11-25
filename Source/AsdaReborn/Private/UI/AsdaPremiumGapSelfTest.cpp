// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПАЧКИ 19, БАГ 4 (08.11.2026): прозрачная щель в окне премиум-магазина 6000.
//
// ЧТО ДЕЛАЕТ (путь игрока `-game -AsdaNoNet -RenderOffscreen`): для каждого разрешения из меню настроек (`GetAvailableResolutions`)
//   и стандартного набора ставит экран (`r.SetRes`), открывает окно 6000 и для каждой видимой вкладки живой полосы 58200 (0 «Горячее»,
//   2 «Инструмент», 3 «Снаряжение» с под-полосой и без, 5 «Питомец», 6 «Элитный набор») снимает кадр и пишет в журнал живую геометрию:
//   рамку окна, шапку, полосы, верх витрины 6083. Щель ищет `Tools/asda_premium_gap_scan.py` по кадрам: за окном в мире самотеста небо,
//   и голубой пиксель внутри прямоугольника окна = прозрачная полоса.
//   Кадры — `Reports/batch19/premium_gap/<метка>/<язык>_<WxH>_c<вкладка>.png`, метка — аргумент `tag=` (до/после правки).
//
// Консоль: `asda.Shop.PremiumGapSelfTest [tag=before] [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-ЩЕЛЬ», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Resources/AsdaVideoSettingsSubsystem.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPremGapTest, Log, All);

namespace AsdaPremGapLocal
{
	static const TCHAR* kWho = TEXT("asda.Shop.PremiumGapSelfTest");
}

struct FAsdaPremGapTest
{
	int32 Ok = 0;
	int32 Fail = 0;
	TArray<TFunction<float(UWorld*)>> Steps;
	FString Lang;
	FString ShotDir;

	void Expect(bool b, const FString& Name, const FString& Detail = FString())
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaPremGapTest, Log, TEXT("САМОТЕСТ-ЩЕЛЬ %s: %s%s%s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name,
			Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
	}
	static UAsdaPremiumShopSubsystem* P(UWorld* W) { UGameInstance* G = W ? W->GetGameInstance() : nullptr; return G ? G->GetSubsystem<UAsdaPremiumShopSubsystem>() : nullptr; }

	// Абсолютный прямоугольник узла окна по имени (пусто — нет/скрыт).
	static FString RectOf(UAsdaPremiumShopSubsystem* S, const TCHAR* Name)
	{
		UWidget* N = S ? S->DebugNode(Name) : nullptr;
		FVector2D Mn, Mx;
		if (!N || !AsdaUi::IsShown(N) || !AsdaUi::WidgetAbsRect(N, Mn, Mx)) { return TEXT("-"); }
		return FString::Printf(TEXT("%.0f,%.0f,%.0f,%.0f"), Mn.X, Mn.Y, Mx.X, Mx.Y);
	}

	void Build(const TArray<FIntPoint>& Res)
	{
		struct FCase { int32 Cat; int32 EqSub2; const TCHAR* Tag; };
		static const FCase kCases[] = { {0, -1, TEXT("c0")}, {2, -1, TEXT("c2")}, {3, -1, TEXT("c3")}, {3, 0, TEXT("c3s2")}, {5, -1, TEXT("c5")}, {6, -1, TEXT("c6")} };
		for (const FIntPoint& R : Res)
		{
			Steps.Add([R](UWorld* W) -> float
			{
				if (GEngine) { GEngine->Exec(W, *FString::Printf(TEXT("r.SetRes %dx%dw"), R.X, R.Y)); }
				return 1.2f;
			});
			Steps.Add([](UWorld* W) -> float
			{
				if (UAsdaPremiumShopSubsystem* S = P(W)) { if (S->IsOpen()) { S->CloseWindow(); } S->OpenWindow(false); }
				return 1.0f;
			});
			for (const FCase& C : kCases)
			{
				Steps.Add([C](UWorld* W) -> float
				{
					UAsdaPremiumShopSubsystem* S = P(W);
					if (!S) { return 0.1f; }
					S->SetCategory(C.Cat);
					if (C.EqSub2 >= 0) { S->SetEquipSub2(C.EqSub2); }
					return 0.8f;
				});
				Steps.Add([this, R, C](UWorld* W) -> float
				{
					UAsdaPremiumShopSubsystem* S = P(W);
					const bool bOpen = S && S->IsOpen() && S->GetCategory() == C.Cat;
					Expect(bOpen, FString::Printf(TEXT("%dx%d %s: окно открыто на вкладке"), R.X, R.Y, C.Tag));
					// Геометрия: рамка окна (средний план 9-slice), шапка, подложка, полосы, витрина.
					UE_LOG(LogAsdaPremGapTest, Log, TEXT("САМОТЕСТ-ЩЕЛЬ ГЕОМ %dx%d %s win=%s base=%s bodybg=%s bar1=%s bar2=%s bar3=%s bar4=%s list=%s"),
						R.X, R.Y, C.Tag, *RectOf(S, TEXT("SHELLITEM_STORE_WIN_0_4")), *RectOf(S, TEXT("ShellItem_Base_grp")),
						*RectOf(S, TEXT("ShellItem_BodyBg_grp")), *RectOf(S, TEXT("SHELLITEM_STORE_MENU_NEW1_grp")),
						*RectOf(S, TEXT("SHELLITEM_STORE_MENU_NEW2_grp")), *RectOf(S, TEXT("SHELLITEM_STORE_MENU_NEW3_grp")),
						*RectOf(S, TEXT("SHELLITEM_STORE_MENU_NEW4_grp")), *RectOf(S, TEXT("SHELLITEM_ITEMLIST_grp")));
					const FString File = FPaths::Combine(ShotDir, FString::Printf(TEXT("%s_%dx%d_%s.png"), *Lang, R.X, R.Y, C.Tag));
					FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
					UE_LOG(LogAsdaPremGapTest, Log, TEXT("САМОТЕСТ-ЩЕЛЬ кадр: %s"), *File);
					return 0.6f;
				});
			}
		}
		Steps.Add([](UWorld* W) -> float
		{
			if (UAsdaPremiumShopSubsystem* S = P(W)) { S->CloseWindow(); }
			if (GEngine) { GEngine->Exec(W, TEXT("r.SetRes 1024x768w")); }
			return 1.0f;
		});
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaPremGapSelfTest(
		TEXT("asda.Shop.PremiumGapSelfTest"),
		TEXT("asda.Shop.PremiumGapSelfTest [tag=before] [delay=N] [quit] — ОФЛАЙН-самотест пачки 19 (баг 4): окно 6000 на всех ")
		TEXT("разрешениях меню и вкладках, кадры и живая геометрия для поиска прозрачной щели."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TCHAR* Who = AsdaPremGapLocal::kWho;
			AsdaCVarSave::ArmWorldGuard(W, Who);
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(W, Who, KitArgs)) { return; }
			float Delay = 0.f;
			FString Tag = TEXT("before");
			for (const FString& A : Args)
			{
				if (A.StartsWith(TEXT("tag="))) { Tag = A.Mid(4); }
			}
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit, Who, Tag](float) -> bool
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(W2, Who, bQuit)) { return false; }
				TSharedRef<FAsdaPremGapTest> T = MakeShared<FAsdaPremGapTest>();
				T->Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
				T->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch19/premium_gap"), Tag));
				// Разрешения: меню настроек (адаптер) + стандартный набор; без повторов.
				TArray<FIntPoint> Res = { {1024, 768}, {1280, 720}, {1280, 1024}, {1366, 768}, {1600, 900}, {1920, 1080}, {1680, 1050}, {1920, 1200}, {2560, 1440}, {2560, 1600}, {3840, 2160} };
				if (UAsdaVideoSettingsSubsystem* Vid = W2->GetGameInstance() ? W2->GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr)
				{
					for (const FIntPoint& R : Vid->GetAvailableResolutions()) { Res.AddUnique(R); }
				}
				FString L;
				for (const FIntPoint& R : Res) { L += FString::Printf(TEXT(" %dx%d"), R.X, R.Y); }
				UE_LOG(LogAsdaPremGapTest, Log, TEXT("САМОТЕСТ-ЩЕЛЬ разрешения (%d):%s"), Res.Num(), *L);
				T->Build(Res);
				TSharedRef<int32> Idx = MakeShared<int32>(0);
				TSharedRef<double> Next = MakeShared<double>(0.0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, Next, bQuit, Who](float) -> bool
				{
					UWorld* W3 = WW.Get();
					if (!W3) { AsdaCVarSave::RestoreAll(Who); return false; }
					const double Now = FPlatformTime::Seconds();
					if (Now < *Next) { return true; }
					if (*Idx < T->Steps.Num()) { *Next = Now + T->Steps[(*Idx)++](W3); return true; }
					AsdaCVarSave::RestoreAll(Who);
					UE_LOG(LogAsdaPremGapTest, Log, TEXT("САМОТЕСТ-ЩЕЛЬ ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaPremGapTest, Log, TEXT("САМОТЕСТ-ЩЕЛЬ: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
