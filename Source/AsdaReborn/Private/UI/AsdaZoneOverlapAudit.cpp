// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ЖИВОЙ АУДИТ ПЕРЕКРЫТЫХ КНОПОК — `asda.UI.ZoneOverlapAudit [file=<путь>] [period=0.1]` (06.11.2026, продолжение бага окна создания).
//   Запускается ДО пакета самотестов и работает весь прогон. На каждом шаге смотрит все окна на экране и пишет две записи:
//     COVER — видимая кнопка окна в точке (центр и 4 внутренние) перекрыта другим виджетом ТОГО ЖЕ окна: путь попадания Slate кнопку не содержит;
//     GHOST — зона `UAsdaIconCellGrid` ловит клики, а её ячейка скрыта (сама или через предка).
//   Плюс GRID — какие сетки вообще были на экране (покрытие аудита). Только чтение: ни кликов, ни правок виджетов, ни исходящих.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaUiHelpers.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"                   // IFileManager для дозаписи файла
#include "Misc/FileHelper.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaZoneAudit, Log, All);   // журнал аудита

// Доступ аудита к ячейке сетки (friend в AsdaIconCellGrid.h).
struct FAsdaZoneAuditAccess
{
	static UWidget* Cell(const UAsdaIconCellGrid* G, int32 I) { return G->CellWidget(I); }
	static UUserWidget* Owner(const UAsdaIconCellGrid* G) { return G->Owner; }
};

namespace AsdaZoneAuditLocal
{
	static FString GFile;                 // файл записей
	static TSet<FString> GSeen;           // уже записанные строки
	static FTSTicker::FDelegateHandle GTick;   // шаг аудита

	// Новая запись: в журнал и в файл, повторы отбрасываются.
	static void Note(const FString& S)
	{
		bool bDup = false;
		GSeen.Add(S, &bDup);
		if (bDup) { return; }
		UE_LOG(LogAsdaZoneAudit, Display, TEXT("[аудит зон] %s"), *S);
		if (!GFile.IsEmpty()) { FFileHelper::SaveStringToFile(S + TEXT("\n"), *GFile, FFileHelper::EEncodingOptions::ForceUTF8, &IFileManager::Get(), FILEWRITE_Append); }
	}

	// Показан ли виджет с учётом всей цепочки родителей.
	static bool Shown(const UWidget* W)
	{
		for (; W; W = W->GetParent())
		{
			const ESlateVisibility V = W->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
		}
		return true;
	}

	// Шаг: окна на экране.
	static bool Step(float)
	{
		if (!FSlateApplication::IsInitialized()) { return true; }
		FSlateApplication& App = FSlateApplication::Get();
		TArray<TSharedRef<SWindow>> Wins = App.GetInteractiveTopLevelWindows();
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* Win = *It;
			if (!IsValid(Win) || !Win->WidgetTree || !Win->GetWorld() || !Win->GetWorld()->IsGameWorld() || !Win->IsInViewport() || !Shown(Win)) { continue; }
			TMap<const SWidget*, const UWidget*> Map;
			TArray<UWidget*> All; Win->WidgetTree->GetAllWidgets(All);
			for (UWidget* X : All) { if (X) { if (TSharedPtr<SWidget> S = X->GetCachedWidget()) { Map.Add(S.Get(), X); } } }
			for (UWidget* X : All)
			{
				UButton* B = Cast<UButton>(X);
				if (!B || !B->GetIsEnabled() || !Shown(B)) { continue; }
				TSharedPtr<SWidget> Self = B->GetCachedWidget();
				FVector2D Mn, Mx;
				if (!Self.IsValid() || !AsdaUi::WidgetAbsRect(B, Mn, Mx) || (Mx - Mn).X < 4.0 || (Mx - Mn).Y < 4.0) { continue; }
				const FVector2D D = Mx - Mn;
				const FVector2D Pts[5] = { Mn + D * 0.5, Mn + D * 0.2, Mn + FVector2D(D.X * 0.8, D.Y * 0.2), Mn + FVector2D(D.X * 0.2, D.Y * 0.8), Mn + D * 0.8 };
				for (const FVector2D& P : Pts)
				{
					const FWidgetPath Path = App.LocateWindowUnderMouse(P, Wins, false, 0);
					if (!Path.IsValid()) { continue; }
					bool bHasSelf = false; const UWidget* Top = nullptr;
					for (int32 i = Path.Widgets.Num() - 1; i >= 0; --i)
					{
						const SWidget* S = &Path.Widgets[i].Widget.Get();
						bHasSelf |= (S == Self.Get());
						if (!Top) { if (const UWidget** F = Map.Find(S)) { Top = *F; } }
					}
					// Верхний виджет не из этого окна: окна лежат друг на друге, это не дефект разметки.
					if (bHasSelf || !Top || Top == Win->WidgetTree->RootWidget) { continue; }
					Note(FString::Printf(TEXT("COVER\t%s\t%s\tперекрыта\t%s\t%s"), *Win->GetClass()->GetName(), *B->GetName(), *Top->GetName(), *Top->GetClass()->GetName()));
				}
			}
		}
		for (TObjectIterator<UAsdaIconCellGrid> It; It; ++It)
		{
			const UAsdaIconCellGrid* G = *It;
			UUserWidget* Owner = IsValid(G) ? FAsdaZoneAuditAccess::Owner(G) : nullptr;
			if (!IsValid(Owner) || !Owner->GetWorld() || !Owner->GetWorld()->IsGameWorld() || !Owner->IsInViewport() || !Shown(Owner)) { continue; }
			Note(FString::Printf(TEXT("GRID\t%s\tячеек %d"), *Owner->GetClass()->GetName(), G->GetNumCells()));
			for (int32 i = 0; i < G->GetNumCells(); ++i)
			{
				const UButton* Z = G->GetHitButton(i);
				const UWidget* Cell = FAsdaZoneAuditAccess::Cell(G, i);
				if (Z && Cell && Shown(Z) && !Shown(Cell))
				{
					Note(FString::Printf(TEXT("GHOST\t%s\t%s\tячейка\t%s"), *Owner->GetClass()->GetName(), *Z->GetName(), *Cell->GetName()));
				}
			}
		}
		return true;
	}
}

static FAutoConsoleCommand GAsdaUiZoneOverlapAudit(
	TEXT("asda.UI.ZoneOverlapAudit"),
	TEXT("asda.UI.ZoneOverlapAudit [file=<путь>] [period=0.1] — живой аудит на весь прогон: перекрытые кнопки окон (COVER) и зоны сетки у скрытых ячеек (GHOST). Только чтение."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& A)
	{
		using namespace AsdaZoneAuditLocal;
		float Period = 0.1f;
		for (const FString& P : A)
		{
			if (P.StartsWith(TEXT("file="))) { GFile = P.Mid(5); }
			else if (P.StartsWith(TEXT("period="))) { Period = FCString::Atof(*P.Mid(7)); }
		}
		if (GTick.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(GTick); }
		GTick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Step), FMath::Max(0.02f, Period));
		UE_LOG(LogAsdaZoneAudit, Display, TEXT("[аудит зон] запущен, шаг %.2f с, файл %s"), Period, *GFile);
	}));

#endif // !UE_BUILD_SHIPPING
