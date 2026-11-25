// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPackageIconsSelfTest.cpp — САМОТЕСТ «ИКОНКИ СОСТАВА В ПАНЕЛИ 26500» (04.11.2026, баг владельца: «коробка зелья и, может,
//   другие премиум-наборы — при просмотре из сумки окно открывается, а у содержимого нет иконок»; Reports/levelup_potion).
//
//   Что проверяет. Для КАЖДОЙ коробки item_list (подтип 200/201 со строкой packagetable) панель 26500 заполняется тем же
//   `UAsdaPackagePanelSubsystem::Populate`, что двойной щелчок в сумке (режим 0 «превью», сеть не трогается), и у каждой
//   занятой ячейки состава проверяется САМ ВИДЖЕТ иконки:
//     • виджет найден и это `UImage`;
//     • запись иконки есть (`GetItemIcon`) — иначе «нет записи иконки»;
//     • кисть = текстура атласа, `DrawAs = Image`, UV-область с ненулевой площадью внутри [0,1];
//     • сам виджет и вся цепочка родителей до корня окна видимы (не Collapsed/Hidden).
//   Итог — сводка по причинам и список коробок с пустыми ячейками (файл `Saved/Logs/AsdaPackageIcons.tsv`: коробка, блок,
//   ячейка, код позиции, причина). Аргумент `shot` — после прогона кадры PNG панели для 76405 и 79935…79939
//   (`Saved/Screenshots/asda_pkgicons_<код>.png`) и проверка геометрии иконок на живом кадре. `code=<N>` — только эта коробка.
//   Итог `САМОТЕСТ-ИКОНКИ-26500 ИТОГ: ok=N fail=M`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaPackagePanelSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaShopDblClick.h"
#include "UI/AsdaUiHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPkgIconsTest, Log, All);

namespace
{
	const TCHAR* const kCmd = TEXT("asda.SelfTest.PackageIcons");   // имя команды
	const TCHAR* const kPrefix = TEXT("САМОТЕСТ-ИКОНКИ-26500");      // префикс строк и итога

	// Коробки, для которых снимается кадр (жалоба владельца + коробки волны levelup_potion).
	const int32 kShotBoxes[] = { 76405, 79935, 79936, 79937, 79938, 79939 };

	// Причина «нет иконки» у одной ячейки ("" — иконка на месте).
	FString IconProblem(const UAsdaItemTableSubsystem* IT, int32 Code, const UWidget* W, const UWidget* Root)
	{
		const UImage* Img = Cast<UImage>(W);
		if (!Img) { return TEXT("нет виджета иконки"); }
		if (!IT->GetItemIcon(Code, true).IsValid()) { return TEXT("нет записи иконки (GetItemIcon)"); }
		const FSlateBrush& B = Img->GetBrush();
		if (!Cast<UTexture2D>(B.GetResourceObject())) { return TEXT("кисть без текстуры атласа"); }
		if (B.DrawAs == ESlateBrushDrawType::NoDrawType) { return TEXT("кисть NoDraw"); }
		const FBox2D UV = B.GetUVRegion();
		if (UV.GetArea() <= 0.f || UV.Min.X < 0.f || UV.Min.Y < 0.f || UV.Max.X > 1.0001f || UV.Max.Y > 1.0001f)
		{
			return FString::Printf(TEXT("UV вне атласа (%.3f,%.3f)-(%.3f,%.3f)"), UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y);
		}
		for (const UWidget* Up = Img; Up; Up = Up->GetParent())
		{
			const ESlateVisibility V = Up->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden)
			{
				return FString::Printf(TEXT("скрыт узел %s"), *Up->GetName());
			}
			if (Up == Root) { break; }
		}
		return FString();
	}

	struct FRunState
	{
		TWeakObjectPtr<UWorld> World;
		bool bQuit = false;
		int32 Ok = 0;
		int32 Fail = 0;
		int32 ShotIdx = 0;
		FString OutDir;     // `out=<папка>` — куда копировать TSV и кадры (песочница пакетного прогона удаляется)
		FString PendingShot; // имя последнего запрошенного кадра (копируется на следующем шаге)
	};

	void Check(FRunState& S, bool bOk, const FString& What)
	{
		if (bOk) { ++S.Ok; } else { ++S.Fail; }
		UE_LOG(LogAsdaPkgIconsTest, Display, TEXT("%s %s %s"), kPrefix, bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	// Обход занятых ячеек панели: (блок, верхняя?, индекс, код, виджет иконки).
	template <typename FFn>
	void ForEachFilledCell(const UAsdaPackagePanelSubsystem* Pk, FFn&& Fn)
	{
		for (int32 r = 0; r < UAsdaPackagePanelSubsystem::kBlocks; ++r)
		{
			for (int32 k = 0; k < UAsdaPackagePanelSubsystem::kBigCells; ++k)
			{
				const int32 C = Pk->DebugCellCode(r, true, k);
				if (C > 0) { Fn(r, true, k, C, Pk->DebugCellIcon(r, true, k)); }
			}
			for (int32 k = 0; k < UAsdaPackagePanelSubsystem::kSmallCells; ++k)
			{
				const int32 C = Pk->DebugCellCode(r, false, k);
				if (C > 0) { Fn(r, false, k, C, Pk->DebugCellIcon(r, false, k)); }
			}
		}
	}

	// Кадр одной коробки: заполнить, через 0,6 с — снимок и проверка геометрии иконок, затем следующая.
	// Скопировать готовый кадр в `out=` (движок пишет его в ScreenShotDir в конце кадра — копия на следующем шаге).
	void CopyPendingShot(FRunState& S)
	{
		if (S.OutDir.IsEmpty() || S.PendingShot.IsEmpty()) { return; }
		TArray<FString> Found;
		IFileManager::Get().FindFilesRecursive(Found, *FPaths::ScreenShotDir(), *(S.PendingShot + TEXT(".png")), true, false);
		for (const FString& F : Found)
		{
			IFileManager::Get().Copy(*FPaths::Combine(S.OutDir, FPaths::GetCleanFilename(F)), *F);
		}
		Check(S, Found.Num() > 0, FString::Printf(TEXT("кадр %s.png записан (%d)"), *S.PendingShot, Found.Num()));
		S.PendingShot.Reset();
	}

	void ShotNext(TSharedRef<FRunState> S)
	{
		CopyPendingShot(*S);
		UWorld* W = S->World.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaPackagePanelSubsystem* Pk = GI ? GI->GetSubsystem<UAsdaPackagePanelSubsystem>() : nullptr;
		if (!W || !Pk || S->ShotIdx >= UE_ARRAY_COUNT(kShotBoxes))
		{
			if (Pk) { Pk->ClosePanel(); }
			AsdaSelfTestKit::Finish(W, kCmd, S->bQuit, LogAsdaPkgIconsTest, kPrefix, S->Ok, S->Fail);
			return;
		}
		const int32 Box = kShotBoxes[S->ShotIdx++];
		// ★ Путь игрока: коробка в ячейку 0 сумки → окно инвентаря, вкладка 1 → двойной щелчок по иконке ячейки
		//   (тот же `OpenPackageItem`, что у игрока: панель в режиме 1 «вскрытие»).
		Pk->ClosePanel();
		UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
		if (Inv)
		{
			Inv->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(Box, 1, 0, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
			Inv->SetTab(1);
		}
		FTimerHandle H0;
		W->GetTimerManager().SetTimer(H0, [S, Box]()
		{
			UWorld* W1 = S->World.Get();
			UGameInstance* GI1 = W1 ? W1->GetGameInstance() : nullptr;
			UAsdaInventorySubsystem* Inv1 = GI1 ? GI1->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			FVector2D Mn, Mx;
			const UWidget* Cell = Inv1 ? Inv1->DebugGetGridIcon(0) : nullptr;
			const bool bGeo = Cell && AsdaUi::WidgetAbsRect(Cell, Mn, Mx) && (Mx - Mn).X >= 2.0;
			if (bGeo && FSlateApplication::IsInitialized())
			{
				const FVector2D P = (Mn + Mx) * 0.5f;
				FSlateApplication& App = FSlateApplication::Get();
				App.SetCursorPos(P);
				App.ProcessMouseMoveEvent(FPointerEvent(0, P, P, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
			}
			// Вход двойного щелчка окна сумки (`UseFromBagWindow` → `UseByDoubleClick` → `OpenPackageItem`, режим 1) — тот же,
			//   что зовёт обработчик мыши ячейки (симклик мыши в пакетном прогоне до ячейки не доходит).
			if (Inv1) { Inv1->UseFromBagWindow(1, 0); }
			Check(*S, bGeo, FString::Printf(TEXT("кадр %d: коробка в сумке на экране, двойной щелчок (UseFromBagWindow 1:0)"), Box));
		}, 0.6f, false);
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, [S, Box]()
		{
			UWorld* W2 = S->World.Get();
			UGameInstance* GI2 = W2 ? W2->GetGameInstance() : nullptr;
			UAsdaPackagePanelSubsystem* Pk2 = GI2 ? GI2->GetSubsystem<UAsdaPackagePanelSubsystem>() : nullptr;
			Check(*S, Pk2 && Pk2->IsPanelOpen() && Pk2->GetStampMode() == 1,
				FString::Printf(TEXT("кадр %d: двойной щелчок в сумке открыл 26500 (режим %d)"), Box, Pk2 ? Pk2->GetStampMode() : -1));
			if (Pk2)
			{
				int32 Drawn = 0, Total = 0;
				FString Bad;
				ForEachFilledCell(Pk2, [&](int32 r, bool bBig, int32 k, int32 Code, UWidget* Icon)
				{
					++Total;
					const FVector2D Sz = Icon ? Icon->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
					if (Icon && Icon->IsVisible() && Sz.X >= 8.f && Sz.Y >= 8.f) { ++Drawn; }
					else { Bad += FString::Printf(TEXT(" [%d.%s%d] %d %.0fx%.0f"), r, bBig ? TEXT("") : TEXT("."), k, Code, Sz.X, Sz.Y); }
				});
				Check(*S, Total > 0 && Drawn == Total, FString::Printf(TEXT("кадр %d: на экране иконок %d из %d%s"), Box, Drawn, Total, *Bad));
			}
			S->PendingShot = FString::Printf(TEXT("asda_pkgicons_bag_%d"), Box);
			FScreenshotRequest::RequestScreenshot(S->PendingShot, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
			FTimerHandle H2;
			W2->GetTimerManager().SetTimer(H2, [S]() { ShotNext(S); }, 0.5f, false);
		}, 1.6f, false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GPackageIconsSelfTest(
	kCmd,
	TEXT("asda.SelfTest.PackageIcons [code=<N>] [shot] [out=<папка>] [quit] — иконки состава в панели 26500 у ВСЕХ коробок item_list "
	     "(подтип 200/201 со строкой packagetable): виджет, запись иконки, текстура, UV, видимость; список пустых ячеек в "
	     "Saved/Logs/AsdaPackageIcons.tsv; shot — кадры PNG 76405, 79935…79939 и геометрия иконок"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 60.f)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		UAsdaPackagePanelSubsystem* Pk = GI ? GI->GetSubsystem<UAsdaPackagePanelSubsystem>() : nullptr;
		if (!IT || !Pk)
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет мира или подсистем таблиц / панели 26500"));
			return;
		}
		if (Pk->IsPanelOpen())
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("панель 26500 открыта игроком"));
			return;
		}
		int32 Only = 0;
		bool bShot = false;
		FString S0Out;
		for (const FString& P : Args.Pos)
		{
			if (P.StartsWith(TEXT("code="))) { Only = FCString::Atoi(*P.Mid(5)); }
			if (P.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { bShot = true; }
			if (P.StartsWith(TEXT("out="))) { S0Out = P.Mid(4); }
		}
		TSharedRef<FRunState> S = MakeShared<FRunState>();
		S->World = W;
		S->bQuit = Args.bQuit;
		S->OutDir = S0Out;
		if (!S->OutDir.IsEmpty()) { IFileManager::Get().MakeDirectory(*S->OutDir, true); }

		// ── Все коробки ──
		TArray<FString> Tsv;
		Tsv.Add(TEXT("box\tbox_name\tblock\tcell\tcontent_code\tcontent_name\treason"));
		TMap<FString, int32> ByReason;
		int32 Boxes = 0, Cells = 0, BadCells = 0, BadBoxes = 0, Unopened = 0;
		for (const FAsdaItemRecord& Rec : IT->GetAllItems())
		{
			const int32 Box = Rec.ItemCode;
			if (Only > 0 && Box != Only) { continue; }
			if (!IT->IsPackageItem(Box)) { continue; }
			++Boxes;
			if (!Pk->Populate(Box, /*StampMode*/0, -1, -1))
			{
				++Unopened;
				Tsv.Add(FString::Printf(TEXT("%d\t%s\t-\t-\t-\t-\tпанель не открылась"), Box, *IT->GetItemName(Box)));
				continue;
			}
			const UWidget* Root = Pk->DebugRootWidget();
			bool bBoxBad = false;
			ForEachFilledCell(Pk, [&](int32 r, bool bBig, int32 k, int32 Code, UWidget* Icon)
			{
				++Cells;
				const FString Why = IconProblem(IT, Code, Icon, Root);
				if (Why.IsEmpty()) { return; }
				++BadCells;
				bBoxBad = true;
				ByReason.FindOrAdd(Why.Left(Why.Find(TEXT(" (")) > 0 ? Why.Find(TEXT(" (")) : Why.Len()))++;
				Tsv.Add(FString::Printf(TEXT("%d\t%s\t%d\t%s%d\t%d\t%s\t%s"), Box, *IT->GetItemName(Box), r, bBig ? TEXT("big") : TEXT("small"),
					k, Code, *IT->GetItemName(Code), *Why));
			});
			if (bBoxBad) { ++BadBoxes; }
			Pk->ClosePanel();
		}
		const FString TsvPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("AsdaPackageIcons.tsv"));
		FFileHelper::SaveStringArrayToFile(Tsv, *TsvPath, FFileHelper::EEncodingOptions::ForceUTF8);
		if (!S->OutDir.IsEmpty()) { IFileManager::Get().Copy(*FPaths::Combine(S->OutDir, TEXT("AsdaPackageIcons.tsv")), *TsvPath); }
		FString Reasons;
		for (const TPair<FString, int32>& P : ByReason) { Reasons += FString::Printf(TEXT(" «%s» %d;"), *P.Key, P.Value); }
		Check(*S, Boxes > 0 && Unopened == 0,
			FString::Printf(TEXT("коробок %d, панель не открылась у %d"), Boxes, Unopened));
		Check(*S, BadCells == 0,
			FString::Printf(TEXT("ячеек состава %d, без иконки %d (коробок %d):%s список %s"), Cells, BadCells, BadBoxes,
				*Reasons, *TsvPath));
		for (const int32 Box : kShotBoxes)
		{
			if (Only > 0 && Box != Only) { continue; }
			if (!IT->IsPackageItem(Box)) { Check(*S, false, FString::Printf(TEXT("коробка %d не найдена в item_list/packagetable"), Box)); continue; }
			Pk->Populate(Box, 0, -1, -1);
			int32 N = 0, Bad = 0;
			FString List;
			ForEachFilledCell(Pk, [&](int32 r, bool bBig, int32 k, int32 Code, UWidget* Icon)
			{
				++N;
				const FString Why = IconProblem(IT, Code, Icon, Pk->DebugRootWidget());
				if (!Why.IsEmpty()) { ++Bad; }
				List += FString::Printf(TEXT(" %d«%s»%s"), Code, *IT->GetItemName(Code), Why.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("[%s]"), *Why));
			});
			Pk->ClosePanel();
			Check(*S, N > 0 && Bad == 0, FString::Printf(TEXT("коробка %d «%s»: ячеек %d, без иконки %d:%s"), Box, *IT->GetItemName(Box), N, Bad, *List));
		}
		if (!bShot)
		{
			AsdaSelfTestKit::Finish(W, kCmd, Args.bQuit, LogAsdaPkgIconsTest, kPrefix, S->Ok, S->Fail);
			return;
		}
		ShotNext(S);
	}));
#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
