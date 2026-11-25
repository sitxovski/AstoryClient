// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ПРОЦЕНТ СКОРОСТИ В ОПИСАНИЯХ ТРАНСПОРТА» (07.11.2026): `asda.SelfTest.MountTip [quit]`.
//   Обходит все записи special_itemlist с подтипом 15 и берёт описание тем же путём, что окно подсказки
//   (`GetSpecialItemTooltip`, с переводом на язык сессии): в тексте должен стоять процент записи (`ResultModel`, равен столбцу 6
//   `Function_Table` сервера) и ни одного другого процента. Исключения — записи без описания и служебные (см. `kSkip`).
//   Итог: «ОПИСАНИЯ-ТРАНСПОРТА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMountTipTest, Log, All);

namespace AsdaMountTipTest
{
	const TCHAR* const kCmd = TEXT("asda.SelfTest.MountTip");     // имя команды
	const TCHAR* const kPrefix = TEXT("ОПИСАНИЯ-ТРАНСПОРТА");     // префикс строк
	// Без процента намеренно: машина админа, безымянные записи «-», подсказка «-» по решению владельца, тестовая доска.
	const int32 kSkip[] = { 299, 393, 406, 430, 450, 451, 472, 473 };
	constexpr int32 kLastCode = 700;   // верхняя граница обхода кодов

	// Все проценты текста как числа.
	static TArray<int32> Percents(const FString& S)
	{
		TArray<int32> Out;
		for (int32 i = 0; i < S.Len(); ++i)
		{
			if (S[i] != TEXT('%')) { continue; }
			int32 b = i;
			while (b > 0 && FChar::IsDigit(S[b - 1])) { --b; }
			if (b < i) { Out.Add(FCString::Atoi(*S.Mid(b, i - b))); }
		}
		return Out;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMountTip(
	TEXT("asda.SelfTest.MountTip"),
	TEXT("Самотест описаний транспорта: у каждой записи подтипа 15 в тексте на языке сессии стоит её процент скорости. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaMountTipTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!IT) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет таблицы предметов")); return; }
		int32 Ok = 0, Fail = 0, Skipped = 0;
		for (int32 Code = 1; Code <= kLastCode; ++Code)
		{
			FAsdaSpecialItemlistRow Sp;
			if (!IT->GetSpecialItem(Code, Sp) || Sp.SubType != 15) { continue; }
			bool bSkip = false;
			for (const int32 K : kSkip) { bSkip = bSkip || K == Code; }
			if (bSkip) { ++Skipped; continue; }
			FAsdaSpecialitemTooltiplistRow Tip;
			const bool bTip = IT->GetSpecialItemTooltip(Code, Tip);
			const TArray<int32> P = Percents(Tip.Tooltip);
			if (bTip && P.Num() == 1 && P[0] == Sp.ResultModel) { ++Ok; continue; }
			++Fail;
			UE_LOG(LogAsdaMountTipTest, Error, TEXT("%s FAIL код %d: ждём %d %%, в тексте процентов %d: %s"), kPrefix, Code, Sp.ResultModel, P.Num(), *Tip.Tooltip);
		}
		// Образец — видно, что текст на языке сессии.
		FAsdaSpecialitemTooltiplistRow Sample;
		if (IT->GetSpecialItemTooltip(125, Sample)) { UE_LOG(LogAsdaMountTipTest, Log, TEXT("%s образец 125 (ru=%d): %s"), kPrefix, FAsdaLang::IsRu() ? 1 : 0, *Sample.Tooltip); }
		const bool bLang = FAsdaLang::IsRu() ? Sample.Tooltip.Contains(TEXT("скорость")) : Sample.Tooltip.Contains(TEXT("Moving speed"));
		if (bLang) { ++Ok; } else { ++Fail; UE_LOG(LogAsdaMountTipTest, Error, TEXT("%s FAIL образец не на языке сессии"), kPrefix); }
		UE_LOG(LogAsdaMountTipTest, Log, TEXT("%s пропущено намеренно %d"), kPrefix, Skipped);
		AsdaSelfTestKit::Finish(W, kCmd, KA.bQuit, LogAsdaMountTipTest, kPrefix, Ok, Fail);
	}));

// ★ Кадры окна подсказки (07.11.2026): `asda.SelfTest.MountTipFrame [codes=137,97] [out=<папка>] [quit]`. Самые длинные описания транспорта в
//   ячейке сумки, наведение настоящим событием мыши по живой геометрии ячейки, снимок экрана — перенос смотрится глазами.
namespace AsdaMountTipFrame
{
	const TCHAR* const kCmd = TEXT("asda.SelfTest.MountTipFrame");   // имя команды
	const TCHAR* const kPrefix = TEXT("КАДРЫ-ОПИСАНИЙ");             // префикс строк

	// Курсор в точку + событие движения.
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UWorld> W; bool bQuit = false; int32 Ok = 0, Fail = 0; FString Out; TArray<int32> Codes; int32 Idx = 0; int32 Phase = 0;
		TArray<uint8> Saved; FTimerHandle H;
	};

	static void Step(TSharedRef<FRun> R)   // ★ 08.11 по значению: `ClearTimer` уничтожает лямбду с последней ссылкой (иначе конец читает освобождённое)
	{
		UWorld* W = R->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaInventorySubsystem* I = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaItemTooltipSubsystem* T = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
		if (!I || !T) { return; }
		if (R->Idx >= R->Codes.Num())
		{
			W->GetTimerManager().ClearTimer(R->H);
			T->HideTooltip();
			I->ApplyExternalRecord(R->Saved.Num() == FAsdaItemRecordWriter::kSize ? R->Saved : FAsdaItemRecordWriter::ClearCell(1, 0), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			AsdaSelfTestKit::Finish(W, kCmd, R->bQuit, LogAsdaMountTipTest, kPrefix, R->Ok, R->Fail);
			return;
		}
		const int32 Code = R->Codes[R->Idx];
		if (R->Phase == 0)
		{
			I->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(Code, 1, 0, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			if (!I->IsWindowOpen()) { I->OpenWindow(); }
			I->SetTab(1);
			Hover(FVector2D(5.f, 5.f));
		}
		else if (R->Phase == 1)
		{
			FVector2D Mn, Mx;
			const UWidget* Icon = I->DebugGetGridIcon(0);
			if (Icon && AsdaUi::WidgetAbsRect(Icon, Mn, Mx)) { Hover((Mn + Mx) * 0.5f); }
		}
		else if (R->Phase == 2)
		{
			const bool bShown = T->IsTooltipShown();
			if (bShown) { ++R->Ok; } else { ++R->Fail; }
			UE_LOG(LogAsdaMountTipTest, Log, TEXT("%s %s код %d: подсказка %s"), kPrefix, bShown ? TEXT("ok  ") : TEXT("FAIL"), Code, bShown ? TEXT("показана") : TEXT("НЕ показана"));
			const FString File = R->Out / FString::Printf(TEXT("%s_%d.png"), FAsdaLang::Code(FAsdaLang::Current()), Code);
			FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		}
		if (++R->Phase > 3) { R->Phase = 0; ++R->Idx; }
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMountTipFrame(
	TEXT("asda.SelfTest.MountTipFrame"),
	TEXT("Кадры окна подсказки на длинных описаниях транспорта: `codes=137,97` `out=<папка>` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaMountTipFrame;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		FString CodesArg = TEXT("137,97"), Out = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MountTipFrames"));
		bool bReady = false;
		for (const FString& A : KA.Pos)
		{
			if (A.StartsWith(TEXT("codes="))) { CodesArg = A.Mid(6); }
			if (A.StartsWith(TEXT("out=")))   { Out = A.Mid(4); }
			if (A == TEXT("selfready"))       { bReady = true; }
		}
		// Мир без своего персонажа — поднять и перезапуститься (как соседние самотесты).
		if (!bReady && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			FString Again = FString::Printf(TEXT("%s selfready codes=%s out=%s"), kCmd, *CodesArg, *Out);
			if (KA.bQuit) { Again += TEXT(" quit"); }
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle Hd;
			W->GetTimerManager().SetTimer(Hd, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaInventorySubsystem* I = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (!I) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет инвентаря")); return; }
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->W = W; R->bQuit = KA.bQuit; R->Out = Out;
		TArray<FString> Parts; CodesArg.ParseIntoArray(Parts, TEXT(","));
		for (const FString& P : Parts) { R->Codes.Add(FCString::Atoi(*P)); }
		IFileManager::Get().MakeDirectory(*Out, /*Tree*/ true);
		if (const FAsdaItemInstance* It = I->GetCell(1, 0)) { if (!It->IsEmpty()) { R->Saved = It->Raw; } }
		W->GetTimerManager().SetTimer(R->H, FTimerDelegate::CreateLambda([R]() { Step(R); }), 0.6f, true);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
