// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ИКОНОК СОВЕЛА ПОСЛЕ АПСКЕЙЛА (07.11.2026, пачка 13): `asda.SelfTest.SowelIcon [quit]`. [ПОРТ]
//   Иконки совела заменены на 64×64 (исходник 32×32), логический размер 32×32 — строка `DT_AtlasSourceSize`.
//     A. Для ВСЕХ совелов: у каждой текстуры импорт 64×64, логический 32×32, кисть `AsdaUi::ApplyItemIcon` берёт ВСЮ картинку
//        (UV 0…1), а не левый верхний угол. Совелы без ассета (`cb5_Newtype_40_4`) считаются отдельно.
//     B. Окно «Вид» бустера с совелами: ячейки 64 заполнены совелами. Кадр.
//     C. Барабан 10050 того же бустера. Кадр.
//   Кадры: `Reports/batch13/sowel_icons_check/<язык>_*.png`. Офлайн (-AsdaNoNet), карта мира.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaBoosterPanelSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaUiHelpers.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSowelIconTest, Log, All);

namespace AsdaSowelIconTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.SowelIcon");   // имя команды
	static const TCHAR* kTag = TEXT("ИКОНКА-СОВЕЛА");              // префикс строк журнала
	static constexpr int32 kBagCell = 7;                           // ячейка сумки с бустером

	// Контекст прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;              // пройдено / провалено
		TWeakObjectPtr<UWorld> W;            // мир
		bool bQuit = false;                  // выйти после итога
		uint32 CapGen = 0;                   // поколение перехвата отправок
		int32 Booster = 0;                   // предмет-бустер с наибольшим числом совелов
		TArray<uint8> SavedBag;              // ячейка сумки до прогона
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaSowelIconTest, Log,   TEXT("%s ok   %s"), kTag, *What); }
			else   { ++Fail; UE_LOG(LogAsdaSowelIconTest, Error, TEXT("%s FAIL %s"), kTag, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Кадр экрана с интерфейсом.
	static void Shot(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("@@ASTORY_DEV_ROOT_FWD@@/Reports/batch13/sowel_icons_check/%s_%s.png"),
			FAsdaLang::Code(FAsdaLang::Current()), Name), /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}

	// A: все совелы таблицы.
	static void CheckAll(const FCtxRef& C)
	{
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		UAsdaTextureSubsystem* TexMgr = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
		UImage* Probe = NewObject<UImage>(C->GI());
		TSet<FString> Files, NoAsset;
		int32 Stones = 0, NoTex = 0, BadSize = 0, BadUv = 0;
		FString FirstBad;
		for (int32 Code = 7001; Code <= 10925; ++Code)
		{
			if (!IT->IsSowelCode(Code)) { continue; }
			const FAsdaItemIcon Ic = IT->GetItemIcon(Code, true);
			if (!Ic.IsValid()) { continue; }
			++Stones;
			UTexture2D* Tex = TexMgr ? TexMgr->LoadUITexture(FName(*Ic.Atlas)) : nullptr;
			if (!Tex) { ++NoTex; NoAsset.Add(Ic.Atlas); continue; }
			if (Files.Contains(Ic.Atlas)) { continue; }
			Files.Add(Ic.Atlas);
			const FIntPoint Imp = Tex->GetImportedSize();
			const FIntPoint Logical = AsdaUi::LogicalAtlasSize(Tex);
			if (Imp != FIntPoint(64, 64) || Logical != FIntPoint(32, 32))
			{
				++BadSize;
				if (FirstBad.IsEmpty()) { FirstBad = FString::Printf(TEXT("%s %dx%d логич. %dx%d"), *Ic.Atlas, Imp.X, Imp.Y, Logical.X, Logical.Y); }
			}
			AsdaUi::ApplyItemIcon(C->GI(), Probe, Code, FVector2D(64.f, 64.f), true, true);
			const FBox2f Uv = Probe->GetBrush().GetUVRegion();
			if (Uv.Min.X != 0.f || Uv.Min.Y != 0.f || !FMath::IsNearlyEqual(Uv.Max.X, 1.f) || !FMath::IsNearlyEqual(Uv.Max.Y, 1.f))
			{
				++BadUv;
				if (FirstBad.IsEmpty()) { FirstBad = FString::Printf(TEXT("%s UV до (%.3f,%.3f)"), *Ic.Atlas, Uv.Max.X, Uv.Max.Y); }
			}
		}
		C->Expect(Files.Num() == 288 && BadSize == 0, FString::Printf(TEXT("A1 совелов %d, файлов %d: импорт 64×64 и логический 32×32 у всех (плохих %d) %s"), Stones, Files.Num(), BadSize, *FirstBad));
		C->Expect(BadUv == 0, FString::Printf(TEXT("A2 кисть берёт ВСЮ картинку у всех файлов (плохих %d)"), BadUv));
		C->Expect(NoAsset.Num() == 1 && NoTex == 21, FString::Printf(TEXT("A3 без ассета только cb5_Newtype_40_4: совелов %d, имён %d (%s)"), NoTex, NoAsset.Num(), *FString::Join(NoAsset.Array(), TEXT(", "))));
	}

	// Бустер с наибольшим числом совелов среди 35 слотов.
	static bool PickBooster(const FCtxRef& C)
	{
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		int32 Best = 0;
		for (int32 Code = 20551; Code <= 80000; ++Code)
		{
			if (!IT->GetItem(Code) || !IT->IsBoosterItem(Code)) { continue; }
			TArray<int32> S;
			if (!IT->GetBoosterSlotsRaw(Code, S)) { continue; }
			int32 N = 0;
			for (int32 V : S) { N += (V > 0 && IT->IsSowelCode(V)) ? 1 : 0; }
			if (N > Best) { Best = N; C->Booster = Code; }
		}
		C->Expect(Best >= 5, FString::Printf(TEXT("данные: бустер %d, совелов в составе %d, язык %s"), C->Booster, Best, FAsdaLang::Code(FAsdaLang::Current())));
		return Best >= 5;
	}

	// Тело 5469 (как `AsdaBoosterBusySelfTest`): код 0, бустер потрачен из ячейки kBagCell, приз — первый совел состава в ячейку 8.
	static TArray<uint8> BuildResult(const FCtxRef& C)
	{
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		TArray<int32> S;
		IT->GetBoosterSlotsRaw(C->Booster, S);
		int32 Prize = 0;
		for (int32 V : S) { if (V > 0 && IT->IsSowelCode(V)) { Prize = V; break; } }
		TArray<uint8> B; B.SetNumZeroed(68);
		B[0] = 0;   // код успеха 5469
		AsdaBytes::WrI32(&B[2], C->Booster); B[6] = 1; AsdaBytes::WrI16(&B[7], kBagCell);
		const TArray<uint8> R = FAsdaItemRecordWriter::Cell(Prize, 2, 8, 1);
		FMemory::Memcpy(&B[9], R.GetData(), FMath::Min(R.Num(), 57));
		AsdaBytes::WrI16(&B[66], 100);
		return B;
	}

	static void Finish(const FCtxRef& C)
	{
		UAsdaBoosterPanelSubsystem* B = C->Sub<UAsdaBoosterPanelSubsystem>();
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		if (B) { B->ClosePrevWindow(); if (B->IsWindowOpen()) { B->OnCloseButton(); } }
		if (Inv)
		{
			if (C->SavedBag.Num() >= 57) { Inv->ApplyExternalRecord(C->SavedBag, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); } else { Inv->ClearCell(1, kBagCell); }
			if (Inv->IsWindowOpen()) { Inv->CloseWindow(); }
		}
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); C->CapGen = 0; } }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaSowelIconTest, kTag, C->Ok, C->Fail);
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaBoosterPanelSubsystem* B = C->Sub<UAsdaBoosterPanelSubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!Inv || !B || !Net || !C->Sub<UAsdaItemTableSubsystem>()) { C->Expect(false, TEXT("подсистемы есть")); Finish(C); return; }
		CheckAll(C);
		if (!PickBooster(C)) { Finish(C); return; }
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [](uint16, const uint8*, int32, bool, TCHAR) {}, 0, kCmd);
		if (const FAsdaItemInstance* X = Inv->GetCell(1, kBagCell)) { if (!X->IsEmpty()) { C->SavedBag = X->Raw; } }
		Inv->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(C->Booster, 1, kBagCell, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
		if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
		At(C, 0.5f, [C]()
		{
			UAsdaBoosterPanelSubsystem* B2 = C->Sub<UAsdaBoosterPanelSubsystem>();
			C->Expect(B2->OpenWindow(C->Booster, 1, kBagCell), TEXT("B0 окно бустера открыто"));
			C->Expect(B2->OpenPrevWindowFrom(C->Booster, EAsdaPrevSource::Drum, /*bCellsActive*/ false), TEXT("B1 окно «Вид» открыто"));
		});
		At(C, 1.6f, [C]()
		{
			UAsdaBoosterPanelSubsystem* B2 = C->Sub<UAsdaBoosterPanelSubsystem>();
			UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
			int32 N = 0;
			for (int32 i = 0; i < 5; ++i) { N += IT->IsSowelCode(B2->DebugPrevSpecialCode(i)) ? 1 : 0; }
			for (int32 i = 0; i < 15; ++i) { N += IT->IsSowelCode(B2->DebugPrevNormalCode(i)) ? 1 : 0; }
			C->Expect(N >= 5, FString::Printf(TEXT("B2 в ячейках 64 окна «Вид» совелов %d"), N));
			Shot(TEXT("booster_prev"));
		});
		At(C, 2.2f, [C]()
		{
			UAsdaBoosterPanelSubsystem* B2 = C->Sub<UAsdaBoosterPanelSubsystem>();
			B2->ClosePrevWindow();
			B2->OnStartButton();
		});
		// Ответ сервера 5469 штатным приёмом: барабан перебирает иконки 5 с, затем показывает приз (совел).
		At(C, 2.6f, [C]()
		{
			TArray<uint8> Body = BuildResult(C);
			C->Sub<UAsdaNetSubsystem>()->DebugFeedPacket(5469, Body);
		});
		At(C, 3.4f, [C]() { Shot(TEXT("booster_drum_spin")); });
		At(C, 8.4f, [C]() { Shot(TEXT("booster_drum_prize")); });
		At(C, 9.0f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSowelIcon(
	TEXT("asda.SelfTest.SowelIcon"),
	TEXT("Самотест иконок совела после апскейла: размеры и UV всех файлов, окно «Вид» и барабан бустера, кадры. Офлайн, карта мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaSowelIconTest;
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
