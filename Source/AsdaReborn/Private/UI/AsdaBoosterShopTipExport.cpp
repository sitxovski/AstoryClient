// Copyright (c) 2026 @sitxovski (Discord)
// AsdaBoosterShopTipExport.cpp — дев-команда [ПОРТ] 05.11.2026 для редактора витрины бустер-шопа (`Tools/BoosterShopEditor`):
//   выгрузка НАСТОЯЩИХ игровых подсказок (тот же построитель и та же плашка `ITEM_TIPWIN` 5000, что видит игрок) в JSON,
//   чтобы редактор показывал бустер и призы «как в игре», а не своей копией текста.
//
// `asda.PremiumMall.ExportTips codes=<файл> out=<файл.json> [quit]` (только офлайн, -game -AsdaNoNet):
//   файл кодов — строки «shop <код>» (карточка магазина бустеров: `ShowShopSlotTooltip(код, 1, покупка)` — ровно то, что
//   зовёт наведение на карточку `AsdaShopUi::TickCardTooltip`, владелец BoosterShop) и «prev <код>» (ячейка окна «Вид»
//   15600: `ShowPreviewSlotTooltip` — наведение на приз, `TickPrevTooltip`). После каждого показа читаются узлы плашки
//   `ITEM_TIPTITLE` / `ITEM_TIPCONTENS` (текст с разметкой rich-text `<a c="…">…</>`, как его рисует UMG) и цвет заголовка.
//   Итог `[подсказки витрины] ИТОГ: ok=N fail=M` (fail — у кода не показалась подсказка).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CONSOLE — дев-команда вне Shipping
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "Text/AsdaLang.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTipExport, Log, All);

#if ASDA_DEV_CONSOLE   // дев-команда, в Shipping не регистрируется
namespace AsdaTipExport
{
	// Текст узла плашки (UTextBlock — как есть, URichTextBlock — разметка rich-text) и цвет текстового узла «#RRGGBB».
	static FString NodeText(UUserWidget* W, const TCHAR* Name, FString* OutColor)
	{
		UWidget* N = W ? AsdaUi::FindNodeByName(W, Name, nullptr) : nullptr;
		if (const UTextBlock* Tb = Cast<UTextBlock>(N))
		{
			if (OutColor) { *OutColor = TEXT("#") + Tb->GetColorAndOpacity().GetSpecifiedColor().ToFColor(true).ToHex().Left(6); }
			return Tb->GetText().ToString();
		}
		if (const URichTextBlock* Rb = Cast<URichTextBlock>(N)) { return Rb->GetText().ToString(); }
		return FString();
	}

	// Значение аргумента `key=` или пусто.
	static FString Arg(const TArray<FString>& A, const TCHAR* Key)
	{
		const FString K = FString(Key) + TEXT("=");
		for (const FString& S : A) { if (S.StartsWith(K)) { return S.Mid(K.Len()); } }
		return FString();
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaBoosterShopExportTips(
	TEXT("asda.PremiumMall.ExportTips"),
	TEXT("asda.PremiumMall.ExportTips codes=<файл> out=<файл.json> [quit] — [ПОРТ] 05.11.2026: настоящие подсказки карточек "
	     "магазина бустеров (shop) и ячеек окна «Вид» (prev) для редактора витрины. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaTipExport;
		static const TCHAR* const kCmd = TEXT("asda.PremiumMall.ExportTips");
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (!W || AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W->GetGameInstance();
		UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
		const FString CodesPath = Arg(A, TEXT("codes")), OutPath = Arg(A, TEXT("out"));
		TArray<FString> Lines;
		if (!Tip || OutPath.IsEmpty() || !FFileHelper::LoadFileToStringArray(Lines, *CodesPath))
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистемы подсказок или аргументов codes=/out="));
			return;
		}
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Items = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("lang"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"));
		int32 Ok = 0, Fail = 0;
		for (const FString& L : Lines)
		{
			TArray<FString> P;
			L.TrimStartAndEnd().ParseIntoArrayWS(P);
			if (P.Num() != 2) { continue; }
			const int32 Code = FCString::Atoi(*P[1]);
			Tip->HideTooltip();
			if (P[0] == TEXT("shop")) { Tip->ShowShopSlotTooltip(Code, 1, /*bSellMode*/ false, EAsdaTipOwner::BoosterShop); }
			else { Tip->ShowPreviewSlotTooltip(Code, EAsdaTipOwner::Item); }
			UAsdaTipPanel* Pn = Tip->GetMainPanel();
			UUserWidget* Wd = Pn ? Pn->GetWidget() : nullptr;
			FString TitleColor;
			const FString Title = NodeText(Wd, TEXT("ITEM_TIPTITLE"), &TitleColor);
			const FString Body = NodeText(Wd, TEXT("ITEM_TIPCONTENS"), nullptr);
			if (!Tip->IsTooltipShown() || (Title.IsEmpty() && Body.IsEmpty()))
			{
				++Fail;
				UE_LOG(LogAsdaTipExport, Warning, TEXT("[подсказки витрины] %s %d: подсказка не показалась"), *P[0], Code);
				continue;
			}
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("title"), Title);
			O->SetStringField(TEXT("title_color"), TitleColor);
			O->SetStringField(TEXT("body"), Body);
			Items->SetObjectField(P[0] + TEXT(":") + P[1], O);
			++Ok;
		}
		Tip->HideTooltip();
		Root->SetObjectField(TEXT("items"), Items);
		FString Text;
		const TSharedRef<TJsonWriter<>> Wr = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Root, Wr);
		const bool bSaved = FFileHelper::SaveStringToFile(Text, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogAsdaTipExport, Log, TEXT("[подсказки витрины] записано %s: %d подсказок (%s)"), *OutPath, Ok, bSaved ? TEXT("OK") : TEXT("ОШИБКА ЗАПИСИ"));
		AsdaSelfTestKit::Finish(W, kCmd, Args.bQuit, LogAsdaTipExport, TEXT("[подсказки витрины]"), Ok, Fail + (bSaved ? 0 : 1));
	}));
#endif // ASDA_DEV_CONSOLE
