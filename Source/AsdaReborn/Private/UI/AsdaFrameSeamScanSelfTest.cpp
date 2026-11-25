// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ-СКАНЕР ШВОВ СОСТАВНЫХ РАМОК (пачка 19, баг 4, 09.11.2026).
//
// ЧТО ДЕЛАЕТ (путь `-game -AsdaNoNet -RenderOffscreen`): для каждого экрана (меню настроек + 1680×1050…3840×2160) и каждого окна из
//   `Tools/data/seam_scan_windows.txt` (все WBP с составными рамками — аудит `Tools/asda_ui_seam_audit.py`) кладёт под окно сплошную
//   пурпурную подложку, открывает окно как есть (`OpenWindowByPath`, раскладка ассета), снимает кадр с интерфейсом (`FScreenshotRequest` → `OnScreenshotCaptured`, в память) и на
//   каждой границе соседних кусков каждой показанной рамки (`<база>_0…8` / `_0…2` одного родителя) ищет пурпурный пиксель, у которого
//   на 2 px по обе стороны поперёк границы НЕ пурпур — это шов (просвет 1–2 px между кусками). Небо и анимация мира не мешают.
//   Строки «САМОТЕСТ-ШОВ ШОВ <экран> <окно> <рамка> <пара> <пикселей>»; таблица — `Reports/batch19/seam_scan/<tag>_<язык>.tsv`.
//   Самотест ничего не проверяет на ok/fail, кроме «окно открылось»: число швов — результат, а не провал (итог: окон со швом N).
//
// Консоль: `asda.UI.FrameSeamScanSelfTest [tag=before] [only=WBP_a,WBP_b] [res=WxH,WxH] [quit]`. Только офлайн.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaBoosterShopSubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"
#include "UI/AsdaPetEvolutionSubsystem.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Resources/AsdaVideoSettingsSubsystem.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "ImageUtils.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSeamScan, Log, All);

namespace AsdaSeamScanLocal
{
	static const TCHAR* kWho = TEXT("asda.UI.FrameSeamScanSelfTest");
	static bool IsMag(const FColor& C) { return C.R > 230 && C.G < 40 && C.B > 230; }
	static int32 Dist(const FColor& A, const FColor& B) { return FMath::Abs(A.R - B.R) + FMath::Abs(A.G - B.G) + FMath::Abs(A.B - B.B); }
}

struct FAsdaSeamScan
{
	int32 Ok = 0;
	int32 Fail = 0;
	TArray<TFunction<float(UWorld*)>> Steps;
	FString Lang;
	FString OutFile;
	TArray<FString> Lines;                 // строки таблицы
	TSet<FString> WinsWithSeam;            // «экран окно»
	TObjectPtr<UBorder> Backdrop = nullptr;
	TArray<float> Shifts = { 0.f };        // `shift=0.25,0.5`: доли пикселя, на которые сдвигается окно (окно на дробной позиции — как после
	                                       //   центровки кодом или перетаскивания); 0 — раскладка ассета
	bool bShots = false;                   // `shot=1`: сохранять прочитанный кадр каждого окна в PNG (сверка глазами)
	FString ShotDir;

	void Expect(bool b, const FString& Name, const FString& Detail = FString())
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaSeamScan, Log, TEXT("САМОТЕСТ-ШОВ %s: %s%s%s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name, Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
	}
	static UAsdaWindowSubsystem* WM(UWorld* W) { UGameInstance* G = W ? W->GetGameInstance() : nullptr; return G ? G->GetSubsystem<UAsdaWindowSubsystem>() : nullptr; }

	void ShowBackdrop(bool bOn)
	{
		if (!GEngine || !GEngine->GameViewport) { return; }
		if (!Backdrop)
		{
			Backdrop = NewObject<UBorder>(GetTransientPackage());
			Backdrop->SetBrushColor(FLinearColor(1.f, 0.f, 1.f, 1.f));
			Backdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
			Backdrop->AddToRoot();
		}
		GEngine->GameViewport->RemoveViewportWidgetContent(Backdrop->TakeWidget());
		if (bOn) { GEngine->GameViewport->AddViewportWidgetContent(Backdrop->TakeWidget(), 15000); }
	}

	// Снятая геометрия рамок окна (координаты кадра) — ждёт кадр скриншота.
	struct FPendingFrame { FString Key; int32 N = 0; TMap<int32, FBox2D> R; };
	TArray<FPendingFrame> Pending;
	FIntPoint PendingRes = FIntPoint::ZeroValue;
	FString PendingWin;
	bool bWaitShot = false;
	FDelegateHandle ShotHandle;

	// Шаг 1: геометрия показанных рамок окна (в координатах кадра) и заказ кадра с интерфейсом.
	void CaptureWindow(UWorld* W, const FIntPoint& Res, const FString& WinName, UUserWidget* Win)
	{
		Pending.Reset();
		if (!Win || !Win->WidgetTree || !GEngine || !GEngine->GameViewport) { return; }
		TSharedPtr<SViewport> VW = GEngine->GameViewport->GetGameViewportWidget();
		const FVector2D Org = VW.IsValid() ? FVector2D(VW->GetCachedGeometry().GetAbsolutePosition()) : FVector2D::ZeroVector;
		TArray<UWidget*> All;
		Win->WidgetTree->GetAllWidgets(All);
		const FRegexPattern Pat(TEXT("^(.*)_([0-8])$"));
		TMap<FString, TMap<int32, UWidget*>> Groups;
		for (UWidget* X : All)
		{
			if (!Cast<UImage>(X) || !X->GetParent()) { continue; }
			FRegexMatcher M(Pat, X->GetName());
			if (!M.FindNext()) { continue; }
			Groups.FindOrAdd(X->GetParent()->GetName() + TEXT("|") + M.GetCaptureGroup(1)).Add(FCString::Atoi(*M.GetCaptureGroup(2)), X);
		}
		for (const TPair<FString, TMap<int32, UWidget*>>& G : Groups)
		{
			const int32 N = G.Value.Num();
			if (N != 9 && N != 3) { continue; }
			FPendingFrame F;
			F.Key = G.Key;
			F.N = N;
			bool bAll = true;
			for (const TPair<int32, UWidget*>& P : G.Value)
			{
				FVector2D Mn, Mx;
				if (!AsdaUi::IsShown(P.Value) || !AsdaUi::WidgetAbsRect(P.Value, Mn, Mx) || (Mx - Mn).X < 0.5 || (Mx - Mn).Y < 0.5) { bAll = false; break; }
				F.R.Add(P.Key, FBox2D(Mn - Org, Mx - Org));
			}
			if (bAll) { Pending.Add(MoveTemp(F)); }
		}
		PendingRes = Res;
		PendingWin = WinName;
		bWaitShot = true;
		FScreenshotRequest::RequestScreenshot(/*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
	}

	// Шаг 2 (делегат кадра): пиксели кадра с интерфейсом.
	void OnShot(int32 SW, int32 SH, const TArray<FColor>& Px)
	{
		if (!bWaitShot) { return; }
		bWaitShot = false;
		ScanPixels(PendingRes, PendingWin, FIntPoint(SW, SH), Px);
	}

	// Скан кадра по снятой геометрии: все рамки, все границы.
	void ScanPixels(const FIntPoint& Res, const FString& WinName, const FIntPoint& Sz, const TArray<FColor>& Px)
	{
		if (bShots && Px.Num() == Sz.X * Sz.Y)
		{
			TArray<FColor> Op = Px;
			for (FColor& C : Op) { C.A = 255; }
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Sz.X, Sz.Y, TArrayView64<const FColor>(Op.GetData(), Op.Num()), Png);
			FFileHelper::SaveArrayToFile(Png, *FPaths::Combine(ShotDir, FString::Printf(TEXT("%dx%d_%s.png"), Res.X, Res.Y, *WinName)));
		}
		auto At = [&Px, &Sz](int32 X, int32 Y) -> const FColor* { return (X >= 0 && Y >= 0 && X < Sz.X && Y < Sz.Y) ? &Px[Y * Sz.X + X] : nullptr; };
		int32 Frames = 0;
		for (const FPendingFrame& G : Pending)
		{
			const int32 N = G.N;
			const TMap<int32, FBox2D>& R = G.R;
			++Frames;
			TArray<TTuple<int32, int32, bool>> Pairs;   // a, b, по X (вертикальная граница)
			if (N == 9)
			{
				for (int32 r = 0; r < 3; ++r) { Pairs.Emplace(r * 3, r * 3 + 1, true); Pairs.Emplace(r * 3 + 1, r * 3 + 2, true); }
				for (int32 c = 0; c < 3; ++c) { Pairs.Emplace(c, c + 3, false); Pairs.Emplace(c + 3, c + 6, false); }
			}
			else
			{
				const bool bX = FMath::Abs(R[1].Min.X - R[0].Max.X) <= FMath::Abs(R[1].Min.Y - R[0].Max.Y);
				Pairs.Emplace(0, 1, bX); Pairs.Emplace(1, 2, bX);
			}
			for (const TTuple<int32, int32, bool>& Pr : Pairs)
			{
				const FBox2D& A = R[Pr.Get<0>()];
				const FBox2D& B = R[Pr.Get<1>()];
				const bool bX = Pr.Get<2>();
				const float Edge = bX ? A.Max.X : A.Max.Y;
				const float Lo = bX ? FMath::Max(A.Min.Y, B.Min.Y) + 3.f : FMath::Max(A.Min.X, B.Min.X) + 3.f;
				const float Hi = bX ? FMath::Min(A.Max.Y, B.Max.Y) - 3.f : FMath::Min(A.Max.X, B.Max.X) - 3.f;
				if (Hi <= Lo) { continue; }
				int32 Hits = 0;
				for (int32 c = FMath::FloorToInt(Edge) - 1; c <= FMath::CeilToInt(Edge); ++c)
				{
					for (int32 t = FMath::CeilToInt(Lo); t <= FMath::FloorToInt(Hi); ++t)
					{
						const FColor* P0 = bX ? At(c, t) : At(t, c);
						const FColor* Pm = bX ? At(c - 2, t) : At(t, c - 2);
						const FColor* Pp = bX ? At(c + 2, t) : At(t, c + 2);
						if (!P0 || !Pm || !Pp) { continue; }
						// Просвет: пурпур подложки между двумя кусками (по 2 px поперёк границы — не пурпур). Признак «линия по цвету» снят:
						//   на градиентах рамок он давал ложные попадания (сверка кадров 09.11), а настоящий шов — всегда просвет.
						if (AsdaSeamScanLocal::IsMag(*P0) && !AsdaSeamScanLocal::IsMag(*Pm) && !AsdaSeamScanLocal::IsMag(*Pp)) { ++Hits; }
					}
				}
				if (Hits > 0)
				{
					const FString Frame = G.Key;
					const FString Line = FString::Printf(TEXT("%dx%d\t%s\t%s\t%d|%d\t%s\t%.2f\t%d"), Res.X, Res.Y, *WinName, *Frame, Pr.Get<0>(), Pr.Get<1>(),
						bX ? TEXT("x") : TEXT("y"), Edge, Hits);
					Lines.Add(Line);
					WinsWithSeam.Add(FString::Printf(TEXT("%dx%d %s"), Res.X, Res.Y, *WinName));
					UE_LOG(LogAsdaSeamScan, Log, TEXT("САМОТЕСТ-ШОВ ШОВ %s"), *Line.Replace(TEXT("\t"), TEXT(" ")));
				}
			}
		}
		UE_LOG(LogAsdaSeamScan, Log, TEXT("САМОТЕСТ-ШОВ окно %dx%d %s: рамок показано %d"), Res.X, Res.Y, *WinName, Frames);
	}

	// ★ `live=1`: окна, открытые СВОИМИ подсистемами (рамки растягивает код: витрины 6083/6983, панель 15515, модалка 1559, окно 43500).
	struct FLive { FString Name; FName Id; TFunction<void(UWorld*)> Open; TFunction<void(UWorld*)> Close; };
	TArray<FLive> Live;
	bool bLive = false;
	bool bHidden = false;   // `hidden=1`: показать скрытые в ассете составные рамки (панели, которые код показывает в игре) с родителями

	// Показать все куски составных рамок окна и цепочку их родителей (только в самотесте, окно затем закрывается).
	static void ForceShowFrames(UUserWidget* Win)
	{
		if (!Win || !Win->WidgetTree) { return; }
		TArray<UWidget*> All;
		Win->WidgetTree->GetAllWidgets(All);
		const FRegexPattern Pat(TEXT("^(.*)_([0-8])$"));
		TMap<FString, TArray<UWidget*>> Groups;
		for (UWidget* X : All)
		{
			if (!Cast<UImage>(X) || !X->GetParent()) { continue; }
			FRegexMatcher M(Pat, X->GetName());
			if (M.FindNext()) { Groups.FindOrAdd(X->GetParent()->GetName() + TEXT("|") + M.GetCaptureGroup(1)).Add(X); }
		}
		for (const TPair<FString, TArray<UWidget*>>& G : Groups)
		{
			if (G.Value.Num() != 9 && G.Value.Num() != 3) { continue; }
			for (UWidget* X : G.Value)
			{
				for (UWidget* P = X; P; P = P->GetParent())
				{
					const ESlateVisibility V = P->GetVisibility();
					if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { P->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
				}
			}
		}
	}

	template <class TS> static TS* Sub(UWorld* W) { UGameInstance* G = W ? W->GetGameInstance() : nullptr; return G ? G->GetSubsystem<TS>() : nullptr; }

	void BuildLive()
	{
		Live.Add({ TEXT("LIVE_Premium6000"), FName(TEXT("SHELLITEM_STORE_WIN")),
			[](UWorld* W) { if (UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W)) { P->OpenWindow(false); } },
			[](UWorld* W) { if (UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W)) { P->CloseWindow(); } } });
		Live.Add({ TEXT("LIVE_Purchase1559"), FName(TEXT("SHELLITEM_PURCHASE_BOX")),
			[](UWorld* W) { if (UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W)) { P->OpenWindow(false); P->DebugSetGxp(1000000); P->OnCardBuyClicked(0); } },
			[](UWorld* W) { if (UAsdaPurchaseBoxSubsystem* B = Sub<UAsdaPurchaseBoxSubsystem>(W)) { B->Close(); } if (UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W)) { P->CloseWindow(); } } });
		Live.Add({ TEXT("LIVE_Booster15515"), FName(TEXT("BOOSTER_SHOP_WIN")),
			[](UWorld* W) { if (UAsdaBoosterShopSubsystem* B = Sub<UAsdaBoosterShopSubsystem>(W)) { B->OpenWindow(); B->OnCellBuyClicked(0); } },
			[](UWorld* W) { if (UAsdaBoosterShopSubsystem* B = Sub<UAsdaBoosterShopSubsystem>(W)) { B->CloseWindow(); } } });
		Live.Add({ TEXT("LIVE_Equip6800"), FName(TEXT("SHELLITEM_STORE2_WIN")),
			[](UWorld* W) { if (UAsdaEquipShopSubsystem* E = Sub<UAsdaEquipShopSubsystem>(W)) { E->OpenWindow(); } },
			[](UWorld* W) { if (UAsdaEquipShopSubsystem* E = Sub<UAsdaEquipShopSubsystem>(W)) { E->CloseWindow(); } } });
		Live.Add({ TEXT("LIVE_PetEvoWide"), FName(TEXT("Pet_Evolution_Win")),
			[](UWorld* W) { if (UAsdaPetEvolutionSubsystem* E = Sub<UAsdaPetEvolutionSubsystem>(W)) { E->DebugShowPage(true); } },
			[](UWorld* W) { if (UAsdaPetEvolutionSubsystem* E = Sub<UAsdaPetEvolutionSubsystem>(W)) { E->CloseEvolution(); } } });
		Live.Add({ TEXT("LIVE_PetEvoNarrow"), FName(TEXT("Pet_Evolution_Win")),
			[](UWorld* W) { if (UAsdaPetEvolutionSubsystem* E = Sub<UAsdaPetEvolutionSubsystem>(W)) { E->DebugShowPage(false); } },
			[](UWorld* W) { if (UAsdaPetEvolutionSubsystem* E = Sub<UAsdaPetEvolutionSubsystem>(W)) { E->CloseEvolution(); } } });
	}

	void Build(const TArray<FIntPoint>& Res, const TArray<FString>& Paths)
	{
		if (bLive) { BuildLive(); }
		for (const FIntPoint& R : Res)
		{
			Steps.Add([R](UWorld* W) -> float { if (GEngine) { GEngine->Exec(W, *FString::Printf(TEXT("r.SetRes %dx%dw"), R.X, R.Y)); } return 1.5f; });
			Steps.Add([this](UWorld*) -> float { ShowBackdrop(true); return 0.3f; });
			for (const FLive& L : Live)
			for (const float Sh : Shifts)
			{
				const FString Name = L.Name + (Sh != 0.f ? FString::Printf(TEXT("@%.2f"), Sh) : FString());
				Steps.Add([L, Sh](UWorld* W) -> float
				{
					L.Open(W);
					// Окно своей подсистемы стоит на обычном z (≈750) — ниже подложки (15000): поднимаем над ней на время кадра.
					if (UAsdaWindowSubsystem* M = WM(W))
					{
						if (UUserWidget* Win = M->FindWindow(L.Id))
						{
							Win->SetRenderTranslation(FVector2D(Sh, Sh));
							if (M->GetWindowZOrder(L.Id) < 15001) { Win->RemoveFromParent(); Win->AddToViewport(15001); }
						}
					}
					return 0.6f;
				});
				Steps.Add([this, R, L, Name](UWorld* W) -> float
				{
					UAsdaWindowSubsystem* M = WM(W);
					UUserWidget* Win = M ? M->FindWindow(L.Id) : nullptr;
					if (!Win) { UE_LOG(LogAsdaSeamScan, Log, TEXT("САМОТЕСТ-ШОВ окно %dx%d %s: НЕ ОТКРЫЛОСЬ"), R.X, R.Y, *Name); return 0.05f; }
					CaptureWindow(W, R, Name, Win);
					return 0.35f;
				});
				Steps.Add([this, L, R, Name](UWorld* W) -> float
				{
					if (bWaitShot) { UE_LOG(LogAsdaSeamScan, Log, TEXT("САМОТЕСТ-ШОВ окно %dx%d %s: кадр не пришёл"), R.X, R.Y, *Name); bWaitShot = false; }
					if (UAsdaWindowSubsystem* M = WM(W)) { if (UUserWidget* Win = M->FindWindow(L.Id)) { Win->SetRenderTranslation(FVector2D::ZeroVector); } }
					L.Close(W);
					return 0.2f;
				});
			}
			for (const FString& Path : Paths)
			for (const float Sh : Shifts)
			{
				const FString Name = FPaths::GetBaseFilename(Path) + (Sh != 0.f ? FString::Printf(TEXT("@%.2f"), Sh) : FString());
				const FName Id(*(FString(TEXT("SEAMSCAN_")) + Name));
				const bool bHiddenNow = bHidden;
				Steps.Add([Id, Path, Sh, bHiddenNow](UWorld* W) -> float
				{
					if (UAsdaWindowSubsystem* M = WM(W))
					{
						UUserWidget* Win = M->OpenWindowByPath(Id, Path + TEXT(".") + FPaths::GetBaseFilename(Path), 15001, ESlateVisibility::SelfHitTestInvisible, false);
						if (Win && bHiddenNow) { ForceShowFrames(Win); }
						// Только в самотесте: окно на дробной позиции (сдвиг отрисовки корня), как после центровки кодом / перетаскивания.
						if (Win) { Win->SetRenderTranslation(FVector2D(Sh, Sh)); }
					}
					return 0.25f;
				});
				Steps.Add([this, R, Id, Name](UWorld* W) -> float
				{
					UAsdaWindowSubsystem* M = WM(W);
					UUserWidget* Win = M ? M->FindWindow(Id) : nullptr;
					if (!Win) { UE_LOG(LogAsdaSeamScan, Log, TEXT("САМОТЕСТ-ШОВ окно %dx%d %s: НЕ ОТКРЫЛОСЬ"), R.X, R.Y, *Name); return 0.05f; }
					CaptureWindow(W, R, Name, Win);
					return 0.35f;   // кадр снимается в конце следующего кадра движка
				});
				Steps.Add([this, Id, Name, R](UWorld* W) -> float
				{
					if (bWaitShot) { UE_LOG(LogAsdaSeamScan, Log, TEXT("САМОТЕСТ-ШОВ окно %dx%d %s: кадр не пришёл"), R.X, R.Y, *Name); bWaitShot = false; }
					if (UAsdaWindowSubsystem* M = WM(W)) { M->CloseWindow(Id); }
					return 0.05f;
				});
			}
			Steps.Add([this](UWorld*) -> float { ShowBackdrop(false); return 0.2f; });
		}
		Steps.Add([](UWorld* W) -> float { if (GEngine) { GEngine->Exec(W, TEXT("r.SetRes 1024x768w")); } return 1.0f; });
	}

	void Finish()
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(ShotHandle);
		ShowBackdrop(false);
		if (Backdrop) { Backdrop->RemoveFromRoot(); Backdrop = nullptr; }
		FFileHelper::SaveStringToFile(TEXT("экран\tокно\tрамка\tпара\tось\tкрай\tпикселей\n") + FString::Join(Lines, TEXT("\n")), *OutFile,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogAsdaSeamScan, Log, TEXT("САМОТЕСТ-ШОВ таблица: %s (швов %d, окон-экранов со швом %d)"), *OutFile, Lines.Num(), WinsWithSeam.Num());
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaSeamScanSelfTest(
		TEXT("asda.UI.FrameSeamScanSelfTest"),
		TEXT("asda.UI.FrameSeamScanSelfTest [tag=before] [only=WBP_a,WBP_b] [res=WxH,WxH] [quit] — ОФЛАЙН-сканер швов составных рамок окон ")
		TEXT("(пурпурная подложка + кадр скриншота движка) на всех разрешениях меню и 4K; окна — Tools/data/seam_scan_windows.txt."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TCHAR* Who = AsdaSeamScanLocal::kWho;
			AsdaCVarSave::ArmWorldGuard(W, Who);
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(W, Who, KitArgs)) { return; }
			FString Tag = TEXT("before"), Only, ResArg;
			bool bShot = false;
			bool bLiveArg = false;
			bool bHiddenArg = false;
			FString ShiftArg;
			for (const FString& A : Args)
			{
				if (A.StartsWith(TEXT("tag="))) { Tag = A.Mid(4); }
				if (A.StartsWith(TEXT("only="))) { Only = A.Mid(5); }
				if (A.StartsWith(TEXT("res="))) { ResArg = A.Mid(4); }
				if (A == TEXT("shot=1")) { bShot = true; }
				if (A == TEXT("live=1")) { bLiveArg = true; }
				if (A == TEXT("hidden=1")) { bHiddenArg = true; }
				if (A.StartsWith(TEXT("shift="))) { ShiftArg = A.Mid(6); }
			}
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit, Who, Tag, Only, ResArg, bShot, ShiftArg, bLiveArg, bHiddenArg](float) -> bool
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(W2, Who, bQuit)) { return false; }
				TSharedRef<FAsdaSeamScan> T = MakeShared<FAsdaSeamScan>();
				T->Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
				const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch19/seam_scan")));
				T->OutFile = FPaths::Combine(Dir, FString::Printf(TEXT("%s_%s.tsv"), *Tag, *T->Lang));
				T->bShots = bShot;
				T->bLive = bLiveArg;
				T->bHidden = bHiddenArg;
				if (!ShiftArg.IsEmpty())
				{
					TArray<FString> Sp;
					ShiftArg.ParseIntoArray(Sp, TEXT(","));
					T->Shifts.Reset();
					for (const FString& V : Sp) { T->Shifts.Add(FCString::Atof(*V)); }
				}
				T->ShotDir = FPaths::Combine(Dir, Tag + TEXT("_shots"));
				TArray<FString> Paths;
				FString Txt;
				FFileHelper::LoadFileToString(Txt, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/data/seam_scan_windows.txt")));
				Txt.ParseIntoArrayLines(Paths);
				if (!Only.IsEmpty())
				{
					TArray<FString> Want;
					Only.ParseIntoArray(Want, TEXT(","));
					Paths = Paths.FilterByPredicate([&Want](const FString& P) { return Want.Contains(FPaths::GetBaseFilename(P)); });
				}
				TArray<FIntPoint> Res;
				if (!ResArg.IsEmpty())
				{
					TArray<FString> Parts;
					ResArg.ParseIntoArray(Parts, TEXT(","));
					for (const FString& P : Parts) { FString A, B; if (P.Split(TEXT("x"), &A, &B)) { Res.Add(FIntPoint(FCString::Atoi(*A), FCString::Atoi(*B))); } }
				}
				else
				{
					Res = { {1024, 768}, {1280, 720}, {1280, 1024}, {1366, 768}, {1600, 900}, {1920, 1080}, {1680, 1050}, {1920, 1200}, {2560, 1440}, {2560, 1600}, {3840, 2160} };
					if (UAsdaVideoSettingsSubsystem* Vid = W2->GetGameInstance() ? W2->GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr)
					{
						for (const FIntPoint& R : Vid->GetAvailableResolutions()) { Res.AddUnique(R); }
					}
				}
				UE_LOG(LogAsdaSeamScan, Log, TEXT("САМОТЕСТ-ШОВ окон %d, экранов %d"), Paths.Num(), Res.Num());
				T->Build(Res, Paths);
				// Кадр с интерфейсом — делегатом движка (при -RenderOffscreen `ReadPixels` вьюпорта возвращает пустой кадр).
				TWeakPtr<FAsdaSeamScan> WT = T;
				T->ShotHandle = UGameViewportClient::OnScreenshotCaptured().AddLambda([WT](int32 SW, int32 SH, const TArray<FColor>& Px)
				{
					if (TSharedPtr<FAsdaSeamScan> P = WT.Pin()) { P->OnShot(SW, SH, Px); }
				});
				TSharedRef<int32> Idx = MakeShared<int32>(0);
				TSharedRef<double> Next = MakeShared<double>(0.0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, Next, bQuit, Who](float) -> bool
				{
					UWorld* W3 = WW.Get();
					if (!W3) { AsdaCVarSave::RestoreAll(Who); return false; }
					const double Now = FPlatformTime::Seconds();
					if (Now < *Next) { return true; }
					if (*Idx < T->Steps.Num()) { *Next = Now + T->Steps[(*Idx)++](W3); return true; }
					T->Finish();
					AsdaCVarSave::RestoreAll(Who);
					UE_LOG(LogAsdaSeamScan, Log, TEXT("САМОТЕСТ-ШОВ ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.f);
				return false;
			}), 0.f);
			UE_LOG(LogAsdaSeamScan, Log, TEXT("САМОТЕСТ-ШОВ: запуск"));
		}));
}

#endif // !UE_BUILD_SHIPPING
