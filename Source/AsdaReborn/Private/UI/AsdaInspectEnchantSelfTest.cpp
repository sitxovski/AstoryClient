// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПАЧКИ 17, БАГ 6 (08.11.2026): «+N%» заточки на надетом в окне сведений о персонаже 4200. [ИСПРАВЛЕНИЕ]
//
// ЧТО ПРОВЕРЯЕТ (путь игрока: функция 205 «Информация о персонаже» вкладки «Функции» окна C = `PlayEmotion(205)`):
//   N1. О СЕБЕ: надето (контейнер 3, штатная точка `ApplyExternalRecord`) оружие +86%, кираса +24%, кольцо +7%, ботинки без заточки,
//       колчан ×250 → в окне 4200 ячейки 9/1/5 = «+86%»/«+24%»/«+7%», 3 — пусто, 10 — «250». Тот же текст, что у надетого в сумке
//       (общая точка `AsdaEquipCellText::Build`).
//   N2. О СОСЕДЕ: 5478 → ответ 5479+7006 (11 записей по 57 б: те же предметы) → те же надписи.
//   N3. Контроль `asda.OtherChara.EnchantText 0` → как оригинал `uiRefreshItemOptionSlots@0xDA77E0`: только «250» в слоте 10.
//   Кадры — `Reports/batch17/inspect_upgrade/<язык>_<кадр>.png`.
// Консоль: `asda.OtherChara.EnchantTextSelfTest [quit]`. Только офлайн. Строки — «САМОТЕСТ-П17», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaCharaInfoSubsystem.h"
#include "UI/AsdaOtherCharaSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaEquipCellText.h"
#include "UI/AsdaUiHelpers.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaP17InspectTest, Log, All);

namespace AsdaP17InspectLocal
{
	static const TCHAR* kWho = TEXT("asda.OtherChara.EnchantTextSelfTest");
	constexpr int32 kPeer = 612;          // userNum соседа
	constexpr int32 kPeerCid = 990612;    // его charId
	struct FWear { int32 Slot; int32 Amount; int32 Percent; const TCHAR* Want; };
	static const FWear kWear[] = {
		{ 9,  0,   86, TEXT("+86%") },
		{ 1,  0,   24, TEXT("+24%") },
		{ 5,  0,   7,  TEXT("+7%") },
		{ 3,  0,   0,  TEXT("") },
		{ 10, 250, 0,  TEXT("250") },
	};
	static TArray<uint8> Body4005Peer(const FVector2D& G)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)kPeer);
		B[6] = 1;
		AsdaBytes::WrI32(B.GetData() + 11, kPeerCid);
		const char* Name = "P17Peer";
		FMemory::Memcpy(B.GetData() + 15, Name, FCStringAnsi::Strlen(Name));
		B[37] = 1; B[38] = 1; B[40] = 20;
		const float X = (float)G.X, Z = (float)G.Y;
		FMemory::Memcpy(B.GetData() + 56, &X, 4);
		FMemory::Memcpy(B.GetData() + 60, &Z, 4);
		return B;
	}
}

struct FAsdaP17InspectSelfTest
{
	int32 Ok = 0, Fail = 0;
	TArray<TFunction<void(UWorld*)>> Steps;
	bool bAbort = false;
	FString Lang, ShotDir;
	uint32 CapGen = 0;
	TUniquePtr<FAsdaNetSelfScope> SelfScope;
	bool bWasInGame = false;
	TArray<TArray<uint8>> Saved3;           // надетое до теста
	TMap<int32, int32> CodeBySlot;          // слот → код предмета этого типа
	TArray<uint8> Gear627;                  // те же 11 записей для ответа 7006 о соседе

	void Expect(bool b, const FString& Name, const FString& Detail = FString())
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaP17InspectTest, Log, TEXT("САМОТЕСТ-П17 %s: %s%s%s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name,
			Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
	}
	template <class T> static T* Sub(UWorld* W) { UGameInstance* GI = W ? W->GetGameInstance() : nullptr; return GI ? GI->GetSubsystem<T>() : nullptr; }
	static AAsdaPlayerController* PCOf(UWorld* W) { return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
	// Живой текст ячейки надетого окна 4200 (`OTHERCHARA_EQUIP%02d`).
	static FString CellText(UWorld* W, int32 Slot)
	{
		const FString Name = FString::Printf(TEXT("OTHERCHARA_EQUIP%02d"), Slot);
		for (TObjectIterator<UTextBlock> It; It; ++It)
		{
			if (It->GetWorld() == W && It->GetName() == Name && It->GetCachedWidget().IsValid() && AsdaUi::IsShown(*It)) { return It->GetText().ToString(); }
		}
		return FString(TEXT("<нет>"));
	}
	// Все пять ячеек набора совпадают с ожидаемым (bLegacy — только слот 10).
	void CheckCells(UWorld* W, const TCHAR* Name, bool bLegacy)
	{
		bool bAll = true; FString Det;
		for (const AsdaP17InspectLocal::FWear& Wr : AsdaP17InspectLocal::kWear)
		{
			const FString Want = (bLegacy && Wr.Slot != 10) ? FString() : FString(Wr.Want);
			const FString Got = CellText(W, Wr.Slot);
			if (Got != Want) { bAll = false; }
			Det += FString::Printf(TEXT("%d:«%s»%s "), Wr.Slot, *Got, Got == Want ? TEXT("") : *FString::Printf(TEXT("(ждали «%s»)"), *Want));
		}
		Expect(bAll, Name, Det);
	}
	void Shot(const TCHAR* Name)
	{
		const FString File = FPaths::Combine(ShotDir, FString::Printf(TEXT("%s_%s.png"), *Lang, Name));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaP17InspectTest, Log, TEXT("САМОТЕСТ-П17 кадр: %s"), *File);
	}
	bool Wear(UWorld* W, int32 Slot, int32 Amount, int32 Percent)
	{
		UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(W);
		const int32* Code = CodeBySlot.Find(Slot);
		if (!Inv || !Code) { return false; }
		TArray<uint8> R; R.SetNumZeroed(FAsdaItemInstance::kRecordSize);
		AsdaBytes::WrI32(R.GetData() + 0, *Code);
		R[4] = 3;
		AsdaBytes::WrI16(R.GetData() + 5, Slot);
		AsdaBytes::WrI32(R.GetData() + 9, Amount);
		R[24] = Percent != 0 ? 3 : 0;
		AsdaBytes::WrI16(R.GetData() + 27, Percent);
		FMemory::Memcpy(Gear627.GetData() + 57 * Slot, R.GetData(), 57);
		return Inv->ApplyExternalRecord(R, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	}

	void Setup(UWorld* W)
	{
		using namespace AsdaP17InspectLocal;
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(W);
		UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(W);
		UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(W);
		if (!Net || !Inv || !IT || !Sub<UAsdaOtherCharaSubsystem>(W) || !Sub<UAsdaCharaInfoSubsystem>(W)) { Expect(false, TEXT("нет подсистем")); bAbort = true; return; }
		Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch17/inspect_upgrade")));
		CapGen = AsdaSelfTestKit::ArmCapture(Net, [](uint16, const uint8*, int32, bool, TCHAR) {}, 0, kWho);
		if (!CapGen) { Expect(false, TEXT("перехват отправок занят")); bAbort = true; return; }
		SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kWho);
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 4321; Net->bSelfUserNumKnown = true; }
		if (Net->SelectedCharId == 0) { Net->SelectedCharId = 990400; }
		bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;
		AsdaCVarSave::SetForTest(TEXT("asda.Inven.EquipCellText"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.OtherChara.EnchantText"), 1);
		for (int32 Code = 1; Code < 200000 && CodeBySlot.Num() < (int32)UE_ARRAY_COUNT(kWear); ++Code)
		{
			const FAsdaItemRecord* Rec = IT->GetItem(Code);
			if (!Rec || IT->IsSpecialItemCode(Code) || !IT->GetItemIcon(Code, true).IsValid()) { continue; }
			for (const FWear& Wr : kWear) { if (Rec->ItemType == Wr.Slot && !CodeBySlot.Contains(Wr.Slot)) { CodeBySlot.Add(Wr.Slot, Code); } }
		}
		Gear627.SetNumZeroed(627);
		for (int32 i = 0; i < Inv->GetContainerSize(3); ++i)
		{
			const FAsdaItemInstance* I = Inv->GetCell(3, i);
			Saved3.Add((I && !I->IsEmpty()) ? I->Raw : TArray<uint8>());
			Inv->ClearCell(3, i);
		}
		FString Det;
		for (const FWear& Wr : kWear) { const bool b = Wear(W, Wr.Slot, Wr.Amount, Wr.Percent); Det += FString::Printf(TEXT("%d:%d%s "), Wr.Slot, CodeBySlot.FindRef(Wr.Slot), b ? TEXT("") : TEXT("(ОТКАЗ)")); }
		Expect(CodeBySlot.Num() == (int32)UE_ARRAY_COUNT(kWear), TEXT("N0 набор надетого собран (слот:код)"), Det);
		APlayerController* PC = W->GetFirstPlayerController();
		AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
		if (!Me) { Expect(false, TEXT("нет своего павна")); bAbort = true; return; }
		Net->DebugFeedPacket(4005, Body4005Peer(Me->GetGamePosXZ() + FVector2D(250.f, 0.f)));
	}

	void BuildSteps()
	{
		using namespace AsdaP17InspectLocal;
		Steps.Add([this](UWorld* W)
		{
			UAsdaCharaInfoSubsystem* C = Sub<UAsdaCharaInfoSubsystem>(W);
			C->ToggleTab(0);
			C->SwitchTabMode(1);
			if (AAsdaPlayerController* PC = PCOf(W)) { PC->SetTarget(PC->GetPawn()); }
			C->PlayEmotion(205);
		});
		Steps.Add([this](UWorld* W)
		{
			UAsdaOtherCharaSubsystem* O = Sub<UAsdaOtherCharaSubsystem>(W);
			Expect(O && O->IsOpen() && O->DebugIsSelfTarget(), TEXT("N1a окно 4200 о себе открыто"));
			CheckCells(W, TEXT("N1 о себе: надписи надетого = как в сумке (+N% / количество слота 10)"), false);
			// Сверка с сумкой: та же общая точка на тех же записях.
			UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(W);
			bool bSame = Inv != nullptr; FString Det;
			for (const FWear& Wr : kWear)
			{
				const FAsdaItemInstance* I = Inv ? Inv->GetCell(3, Wr.Slot) : nullptr;
				uint32 A = 0; const FString Bag = I ? AsdaEquipCellText::Build(Wr.Slot, *I, A) : FString();
				if (Bag != CellText(W, Wr.Slot)) { bSame = false; }
				Det += FString::Printf(TEXT("%d:«%s» "), Wr.Slot, *Bag);
			}
			Expect(bSame, TEXT("N1b текст окна 4200 = текст надетой ячейки сумки (общая точка)"), Det);
			Shot(TEXT("self_enchant"));
		});
		Steps.Add([this](UWorld* W)
		{
			UAsdaOtherCharaSubsystem* O = Sub<UAsdaOtherCharaSubsystem>(W);
			if (O) { O->CloseWindow(); }
			UGameInstance* GI = W->GetGameInstance();
			const UAsdaWorldSubsystem* Ws = GI->GetSubsystem<UAsdaWorldSubsystem>();
			AActor* Peer = Ws ? Ws->GetPlayerActorByCharId(kPeerCid) : nullptr;
			AAsdaPlayerController* PC = PCOf(W);
			if (!Peer || !PC) { Expect(false, TEXT("N2 сосед в мире")); return; }
			PC->SetTarget(Peer);
			Sub<UAsdaCharaInfoSubsystem>(W)->PlayEmotion(205);
			TArray<uint8> A; A.SetNumZeroed(121); FMemory::Memcpy(A.GetData() + 3, "P17Clan", 7); AsdaBytes::WrI32(A.GetData() + 116, 7171);
			for (int32 i = 0; i < 12; ++i) { AsdaBytes::WrI32(A.GetData() + 20 + 4 * i, -1); AsdaBytes::WrI16(A.GetData() + 68 + 2 * i, -1); AsdaBytes::WrI16(A.GetData() + 92 + 2 * i, -1); }
			TArray<uint8> B; B.SetNumZeroed(631); AsdaBytes::WrI32(B.GetData(), 7171); FMemory::Memcpy(B.GetData() + 4, Gear627.GetData(), 627);
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(W);
			Net->DebugFeedPacket(5479, A);
			Net->DebugFeedPacket(7006, B);
		});
		Steps.Add([this](UWorld* W)
		{
			UAsdaOtherCharaSubsystem* O = Sub<UAsdaOtherCharaSubsystem>(W);
			Expect(O && O->IsOpen() && !O->DebugIsSelfTarget() && O->DebugTargetUserNum() == kPeer, TEXT("N2a окно 4200 о соседе открыто (ответ 5479+7006)"));
			CheckCells(W, TEXT("N2 о соседе: «+N%» из записей 7006 (+24 уровень, +27 процент), слот 10 — количество"), false);
			Shot(TEXT("peer_enchant"));
		});
		Steps.Add([this](UWorld* W)   // контроль — отдельным шагом: кадр снимается в конце кадра, после шага
		{
			AsdaCVarSave::SetForTest(TEXT("asda.OtherChara.EnchantText"), 0);
			if (UAsdaOtherCharaSubsystem* O = Sub<UAsdaOtherCharaSubsystem>(W)) { O->DebugRefreshEquip(); }
		});
		Steps.Add([this](UWorld* W)
		{
			CheckCells(W, TEXT("N3 контроль asda.OtherChara.EnchantText 0 → как оригинал: только количество слота 10"), true);
			Shot(TEXT("peer_enchant_key0"));
			AsdaCVarSave::SetForTest(TEXT("asda.OtherChara.EnchantText"), 1);
		});
	}

	void Finish(UWorld* W)
	{
		AsdaCVarSave::RestoreAll(AsdaP17InspectLocal::kWho);
		if (UAsdaOtherCharaSubsystem* O = Sub<UAsdaOtherCharaSubsystem>(W)) { O->CloseWindow(); }
		if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(W))
		{
			for (int32 i = 0; i < Saved3.Num(); ++i)
			{
				if (Saved3[i].Num() >= FAsdaItemInstance::kRecordSize) { Inv->ApplyExternalRecord(Saved3[i], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
				else { Inv->ClearCell(3, i); }
			}
		}
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(W))
		{
			if (CapGen) { AsdaSelfTestKit::DisarmCapture(Net, CapGen); CapGen = 0; }
			Net->bInGameConnected = bWasInGame;
		}
		if (AAsdaPlayerController* PC = PCOf(W)) { PC->SetTarget(nullptr); }
		if (UAsdaCharaInfoSubsystem* C = Sub<UAsdaCharaInfoSubsystem>(W)) { C->CloseWindow(); }
		SelfScope.Reset();
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaP17InspectSelfTest(
		TEXT("asda.OtherChara.EnchantTextSelfTest"),
		TEXT("asda.OtherChara.EnchantTextSelfTest [quit] — ОФЛАЙН-самотест пачки 17 (баг 6): «+N%» заточки на надетом в окне 4200 ")
		TEXT("о себе и о соседе (общая точка с сумкой), контроль ключом asda.OtherChara.EnchantText, кадры RU/EN."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TCHAR* Who = AsdaP17InspectLocal::kWho;
			AsdaCVarSave::ArmWorldGuard(W, Who);
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(W, Who, KitArgs)) { return; }
			const bool bQuit = Args.Contains(TEXT("quit"));
			float Delay = 0.f;
			if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
			{
				GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
				Delay = 4.f;
			}
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit, Who](float) -> bool
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(W2, Who, bQuit)) { return false; }
				TSharedRef<FAsdaP17InspectSelfTest> T = MakeShared<FAsdaP17InspectSelfTest>();
				T->Setup(W2);
				T->BuildSteps();
				TSharedRef<int32> Idx = MakeShared<int32>(0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, bQuit, Who](float) -> bool
				{
					UWorld* W3 = WW.Get();
					if (!W3) { AsdaCVarSave::RestoreAll(Who); return false; }
					if (*Idx < T->Steps.Num() && !T->bAbort) { T->Steps[(*Idx)++](W3); return true; }
					T->Finish(W3);
					UE_LOG(LogAsdaP17InspectTest, Log, TEXT("САМОТЕСТ-П17 ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.6f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaP17InspectTest, Log, TEXT("САМОТЕСТ-П17: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
