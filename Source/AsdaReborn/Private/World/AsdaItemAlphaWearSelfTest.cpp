// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ПРОЗРАЧНАЯ ЧАСТЬ НА НАДЕТОМ КОМПЛЕКТЕ» (31.10.2026, баг 8 bug_chest_invisible): [ПОРТ]
//   `asda.SelfTest.ItemAlphaWear items=<слот>:<код>[:<заточка>],… [sex=f] [shot=<папка>] [quit]`
//   (`sex=f` — свой персонаж на время прогона женский: `SelfSex` 2 и пересборка модели, в конце — как было).
//   Жалоба владельца: после выпуска alpha-5.0.4 у игрока (комплект 74394–74398 +9, оружие 62309 +10, мужчина) грудь
//   стала невидимой. Сценарий путём игрока, офлайн (`-AsdaNoNet`), карта мира: свой персонаж, комплект надевается в
//   ячейки контейнера 3 (`DebugPutItem` — та же общая точка, что ответ 5001 → `ApplySelfEquipPart` → `ApplyItemTexture`).
//     A) по каждой надетой части: компонент найден, мастер слота 0 и режим смешивания — в лог (сверка с таблицей);
//     B) кадры спереди/сзади/сбоку в трёх состояниях (`<папка>/wear_<состояние>_<ракурс>.png`): `fix` — ключи
//        asda.Char.ItemAlpha 1 и asda.Char.ItemAlphaBitSlotOnly 1 (исправление бага 8), `504` — 1/0 (как alpha-5.0.4),
//        `old` — 0 (прежний порт, всё непрозрачное);
//     C) мастер каждого слота: у части с режимом таблицы Blend/Mask — вариант мастера у слота бит-подсеток `_asdabit`
//        (при `504` — у всех слотов), у прочих слотов — M_AsdaCharLit; при `old` — M_AsdaCharLit везде.
//   Итог: «АЛЬФА-КОМПЛЕКТ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Characters/AsdaCharAssembly.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Resources/AsdaMaterialNames.h"
#include "Test/AsdaSelfTestKit.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"   // приближение камеры для кадров
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры

#include "../Characters/AsdaCharAlphaParts.inl"   // таблица прозрачных частей (сверка режима)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaItemAlphaWearTest, Log, All);

namespace AsdaItemAlphaWearTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.ItemAlphaWear");   // имя команды
	static const TCHAR* kKey = TEXT("asda.Char.ItemAlpha");            // ключ отката волны acce_alpha
	static const TCHAR* kKeyBit = TEXT("asda.Char.ItemAlphaBitSlotOnly"); // ключ отката бага 8 (только слот бит-подсеток)
	constexpr int32 kMe = 4321;                                        // свой userNum (4003)

	// Шаг сценария: true — шаг завершён, false — ждёт следующего кадра. Аргумент — секунды от начала шага.
	using FStep = TFunction<bool(float)>;

	// Надеваемая вещь: слот, код, заточка (−1 — не задана).
	struct FWear { int32 Slot = 0; int32 Id = 0; int32 Grade = -1; };

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> W;                     // мир
		bool bQuit = false;                           // `quit` в конце
		FString ShotDir;                              // папка кадров (пусто — без кадров)
		int32 Ok = 0, Fail = 0;                       // счётчики проверок
		TArray<FStep> Steps;                          // сценарий
		int32 Idx = 0;                                // текущий шаг
		double StepT0 = 0.0;                          // начало шага (время мира)
		FDelegateHandle H;                            // подписка на тик мира
		TUniquePtr<FAsdaNetSelfScope> NetScope;       // свой номер — вернуть после прогона
		TUniquePtr<FAsdaCVarScope> CVars;             // ключи — вернуть после прогона
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;       // мир
		TWeakObjectPtr<UAsdaItemTableSubsystem> Items;// таблицы предметов
		TWeakObjectPtr<UAsdaInventorySubsystem> Inv;  // инвентарь (контейнер 3 = надетое)
		TArray<FWear> Wears;                          // комплект
		TMap<int32, int32> Orig;                      // слот → надетое до прогона
		bool bMale = true;                            // пол своего персонажа
		bool bAbort = false;                          // прервать сценарий
		FRotator OrigRot = FRotator::ZeroRotator;     // поворот актёра до кадров
		bool bRotSaved = false;                       // поворот снят
		float OrigArm = -1.f;                         // длина штанги камеры до кадров (−1 — не менялась)
		bool bWantFemale = false;                     // `sex=f`
		uint8 OrigSex = 0;                            // пол до прогона (0 — не менялся)

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaItemAlphaWearTest, Log,   TEXT("АЛЬФА-КОМПЛЕКТ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaItemAlphaWearTest, Error, TEXT("АЛЬФА-КОМПЛЕКТ FAIL %s"), *What); }
		}
	};

	// Режим пары «меш|текстура» по генерируемой таблице (0 — нет, 1 — Blend, 2 — Mask).
	static uint8 TableMode(const FString& Mesh, const FString& Tex)
	{
		const FString M = Mesh.ToLower(), T = Tex.ToLower();
		uint8 Any = 0;   // запись «любая текстура»
		for (const FAsdaCharAlphaPart& P : GAsdaCharAlphaParts)
		{
			if (!M.Equals(P.Mesh)) { continue; }
			if (T.Equals(P.Tex)) { return P.Mode; }
			if (P.Tex[0] == 0) { Any = P.Mode; }
		}
		return Any;
	}

	// Свой павн.
	static AAsdaPlayerCharacter* MeOf(const FState& S)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Компонент надетой части с мешем `Mesh` (скелетная часть или жёсткий меш оружия).
	static UMeshComponent* PartComp(const AActor* A, const FString& Mesh)
	{
		if (!A) { return nullptr; }
		TInlineComponentArray<UMeshComponent*> Comps(A);
		for (UMeshComponent* C : Comps)
		{
			if (!IsValid(C) || !C->IsRegistered() || !C->ComponentHasTag(UAsdaCharAssembly::WornPartTag)) { continue; }
			FString N;
			if (const USkeletalMeshComponent* SK = Cast<USkeletalMeshComponent>(C)) { N = GetNameSafe(SK->GetSkeletalMeshAsset()); }
			else if (const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(C)) { N = GetNameSafe(SM->GetStaticMesh()); }
			if (N.Equals(Mesh, ESearchCase::IgnoreCase)) { return C; }
		}
		return nullptr;
	}

	// Надеть вещь своему: ячейка контейнера 3 (общая точка ответа 5001); заточка оружия — тем же входом модели.
	static void Wear(FState& S, int32 Slot, int32 ItemId, int32 Grade)
	{
		UAsdaInventorySubsystem* Inv = S.Inv.Get();
		const bool bCell = Inv && Inv->DebugPutItem(3, Slot, ItemId, 1);
		if (!bCell || Grade >= 0)
		{
			if (UAsdaWorldSubsystem* WS = S.WS.Get()) { WS->ApplySelfEquipPart(Slot, ItemId, Grade); }
		}
	}

	// Надеть весь комплект.
	static void WearAll(FState& S)
	{
		for (const FWear& Wr : S.Wears) { Wear(S, Wr.Slot, Wr.Id, Wr.Grade); }
	}

	// Описать и проверить все части комплекта при ключах `Key` (ItemAlpha) и `KeyBit` (ItemAlphaBitSlotOnly).
	static void CheckParts(FState& S, int32 Key, int32 KeyBit)
	{
		UAsdaItemTableSubsystem* Items = S.Items.Get();
		static const TCHAR* kWant[] = { TEXT("M_AsdaCharLit"), TEXT("M_AsdaCharLitBlend"), TEXT("M_AsdaCharLitMask") };
		for (const FWear& Wr : S.Wears)
		{
			const bool bSexM = Wr.Slot == 9 ? true : S.bMale;
			const FString Mesh = Items->GetItemMeshName(Wr.Id, bSexM);
			const FString Tex = Items->GetItemTexName(Wr.Id, bSexM);
			if (Mesh.IsEmpty() || !(Wr.Slot <= 4 || Wr.Slot == 8 || Wr.Slot == 9)) { continue; }   // кольца/ожерелье — без модели
			const UMeshComponent* C = PartComp(MeOf(S), Mesh);
			const uint8 Md = Key != 0 ? TableMode(Mesh, Tex) : 0;
			FString Desc;
			bool bAll = C != nullptr && C->GetNumMaterials() > 0;
			const TArray<FName> Slots = C ? C->GetMaterialSlotNames() : TArray<FName>();
			bool bSplit = false;   // у меша есть слот бит-подсеток
			for (const FName& SN : Slots) { bSplit |= SN.ToString().EndsWith(AsdaMat::kBitSlotSuffix); }
			for (int32 i = 0; C && i < C->GetNumMaterials(); ++i)
			{
				const bool bBitSlot = Slots.IsValidIndex(i) && Slots[i].ToString().EndsWith(AsdaMat::kBitSlotSuffix);
				const uint8 Want = (!bSplit || KeyBit == 0 || bBitSlot) ? Md : 0;   // ожидаемый режим слота
				const UMaterialInterface* M = C->GetMaterial(i);
				const UMaterial* B = M ? M->GetMaterial() : nullptr;
				UTexture* T = nullptr;
				if (M) { M->GetTextureParameterValue(FName(AsdaMat::kParamTex), T); }
				Desc += FString::Printf(TEXT(" [%d %s] %s/%s blend=%d tex=%s (ждём %s)"), i, Slots.IsValidIndex(i) ? *Slots[i].ToString() : TEXT("?"),
					*GetNameSafe(M), *GetNameSafe(B), M ? (int32)M->GetBlendMode() : -1, *GetNameSafe(T), kWant[Want]);
				bAll &= B && B->GetName() == kWant[Want] && T && T->GetName().Equals(Tex, ESearchCase::IgnoreCase);
			}
			S.Check(bAll, FString::Printf(TEXT("ключи %d/%d слот %d предмет %d меш %s текстура %s режим таблицы %d, слотов %d:%s"),
				Key, KeyBit, Wr.Slot, Wr.Id, *Mesh, *Tex, (int32)Md, C ? C->GetNumMaterials() : 0, *Desc));
		}
	}

	// Развернуть своего (смещение от «лицом к камере») и снять кадр.
	static void Shot(FState& S, const FString& Name, float YawOff)
	{
		UWorld* W = S.W.Get();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		AAsdaPlayerCharacter* Me = MeOf(S);
		if (PC && Me)
		{
			if (!S.bRotSaved) { S.OrigRot = Me->GetActorRotation(); S.bRotSaved = true; }
			Me->SetActorRotation(FRotator(0.f, PC->GetControlRotation().Yaw + 180.f + YawOff, 0.f));
			if (Me->CameraArm)
			{
				// Крупный план: штанга камеры короче (возвращается в Finish).
				if (S.OrigArm < 0.f) { S.OrigArm = Me->CameraArm->TargetArmLength; }
				Me->CameraArm->TargetArmLength = 260.f;
				Me->CameraArm->bEnableCameraLag = false;
			}
		}
		if (S.ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(S.ShotDir, Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaItemAlphaWearTest, Log, TEXT("АЛЬФА-КОМПЛЕКТ кадр %s"), *Path);
	}

	// Конец прогона: вернуть надетое, поворот, ключи, номер.
	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		for (const TPair<int32, int32>& O : S.Orig) { Wear(S, O.Key, O.Value, -1); }
		if (AAsdaPlayerCharacter* Me = MeOf(S))
		{
			if (S.bRotSaved) { Me->SetActorRotation(S.OrigRot); }
			if (S.OrigArm >= 0.f && Me->CameraArm) { Me->CameraArm->TargetArmLength = S.OrigArm; }
		}
		if (S.OrigSex != 0)
		{
			if (UAsdaWorldSubsystem* WS = S.WS.Get())
			{
				WS->SelfSex = S.OrigSex;
				if (AAsdaPlayerCharacter* Me = MeOf(S)) { Me->InitAppearance(WS->SelfSex, WS->VisualSelfParts()); }
			}
		}
		S.CVars.Reset();
		S.NetScope.Reset();
		if (UAsdaNetSubsystem* N = S.Net.Get())
		{
			if (MeOf(S) && !N->IsSelfUserNumKnown()) { N->SetSelfUserNumForDebug(N->SelfUserNum); }
		}
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaItemAlphaWearTest, TEXT("АЛЬФА-КОМПЛЕКТ"), S.Ok, S.Fail);
	}

	// Тик сценария.
	static void Tick(const TSharedRef<FState>& InSP, UWorld* InW)
	{
		const TSharedRef<FState> SP = InSP;
		FState& S = *SP;
		UWorld* W = S.W.Get();
		if (!W) { FWorldDelegates::OnWorldPostActorTick.Remove(S.H); return; }
		if (InW != W) { return; }
		while (S.Steps.IsValidIndex(S.Idx) && !S.bAbort)
		{
			const float T = (float)(W->GetTimeSeconds() - S.StepT0);
			if (!S.Steps[S.Idx](T)) { return; }
			++S.Idx;
			S.StepT0 = W->GetTimeSeconds();
		}
		Finish(SP);
	}

	// Сценарий.
	static void Build(const TSharedRef<FState>& SP)
	{
		FState* S = &SP.Get();
		auto Do = [](TFunction<void()> Fn) { return FStep([Fn](float) { Fn(); return true; }); };
		auto Wait = [](float Sec) { return FStep([Sec](float T) { return T >= Sec; }); };

		S->Steps.Add(Do([S]()
		{
			GEngine->Exec(S->W.Get(), TEXT("Asda.Load.CurrentMap 3"));
			TArray<uint8> B; B.SetNumZeroed(9);
			AsdaBytes::WrI16(B.GetData() + 0, (int16)kMe);
			AsdaBytes::WrI16(B.GetData() + 2, 256);
			AsdaBytes::WrI16(B.GetData() + 4, 256);
			B[8] = 1;
			S->Net->DebugFeedPacket(4003, B);
		}));
		S->Steps.Add(Wait(4.0f));   // эффект появления
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			if (S->bWantFemale && Me && S->WS.Get() && S->WS->SelfSex != 2)
			{
				// Женский персонаж: тот же вход модели, что смена внешности (InitAppearance по снимку надетого).
				S->OrigSex = S->WS->SelfSex;
				S->WS->SelfSex = 2;
				Me->InitAppearance(S->WS->SelfSex, S->WS->VisualSelfParts());
			}
			S->bMale = S->WS.Get() && S->WS->SelfSex != 2;
			if (const UAsdaInventorySubsystem* Inv = S->Inv.Get())
			{
				for (const FWear& Wr : S->Wears)
				{
					const FAsdaItemInstance* C = Inv->GetCell(3, Wr.Slot);
					S->Orig.Add(Wr.Slot, (C && !C->IsEmpty()) ? C->ItemCode : 0);
				}
			}
			S->Check(Me && S->Wears.Num() > 0, FString::Printf(TEXT("павн %s, пол %s, вещей %d"),
				Me ? TEXT("есть") : TEXT("НЕТ"), S->bMale ? TEXT("м") : TEXT("ж"), S->Wears.Num()));
			if (!Me || S->Wears.Num() == 0) { S->bAbort = true; return; }
			WearAll(*S);
		}));
		// Три состояния: исправление (1/1), как alpha-5.0.4 (1/0), прежний порт (0/1).
		static const TCHAR* kState[] = { TEXT("fix"), TEXT("504"), TEXT("old") };
		static const int32 kKeyA[] = { 1, 1, 0 };
		static const int32 kKeyB[] = { 1, 0, 1 };
		for (int32 st = 0; st < 3; ++st)
		{
			S->Steps.Add(Do([S, st]() { S->CVars->Set(kKey, kKeyA[st]); S->CVars->Set(kKeyBit, kKeyB[st]); WearAll(*S); }));
			S->Steps.Add(Wait(2.0f));
			S->Steps.Add(Do([S, st]() { CheckParts(*S, kKeyA[st], kKeyB[st]); }));
			static const TCHAR* kView[] = { TEXT("front"), TEXT("back"), TEXT("side") };
			static const float kYaw[] = { -20.f, 180.f, 90.f };
			for (int32 v = 0; v < 3; ++v)
			{
				S->Steps.Add(Do([S, st, v]() { Shot(*S, FString::Printf(TEXT("wear_%s_%s"), kState[st], kView[v]), kYaw[v]); }));
				S->Steps.Add(Wait(1.0f));
			}
		}
		S->Steps.Add(Do([S]() { S->CVars->Set(kKey, 1); S->CVars->Set(kKeyBit, 1); WearAll(*S); }));
		S->Steps.Add(Wait(0.3f));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestItemAlphaWear(
	TEXT("asda.SelfTest.ItemAlphaWear"),
	TEXT("Самотест прозрачных частей на надетом комплекте: `items=<слот>:<код>[:<заточка>],…` своему персонажу (офлайн, карта мира), "
	     "мастер каждой части при ключе asda.Char.ItemAlpha 1/0 и кадры спереди/сзади/сбоку. `shot=<папка>` — кадры, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaItemAlphaWearTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (!Net || !WS || !Items || !Inv || !IConsoleManager::Get().FindConsoleVariable(kKey)
			|| !IConsoleManager::Get().FindConsoleVariable(kKeyBit))
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем сети/мира/предметов/инвентаря или ключа отката"));
			return;
		}
		TSharedRef<FState> SP = MakeShared<FState>();
		SP->W = W; SP->bQuit = KA.bQuit; SP->Net = Net; SP->WS = WS; SP->Items = Items; SP->Inv = Inv;
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase))
			{
				SP->ShotDir = FPaths::ConvertRelativePathToFull(P.Mid(5));
				IFileManager::Get().MakeDirectory(*SP->ShotDir, /*Tree*/true);
			}
			else if (P.Equals(TEXT("sex=f"), ESearchCase::IgnoreCase)) { SP->bWantFemale = true; }
			else if (P.StartsWith(TEXT("items="), ESearchCase::IgnoreCase))
			{
				TArray<FString> List;
				P.Mid(6).ParseIntoArray(List, TEXT(","));
				for (const FString& E : List)
				{
					TArray<FString> F;
					E.ParseIntoArray(F, TEXT(":"));
					if (F.Num() < 2) { continue; }
					FWear Wr;
					Wr.Slot = FCString::Atoi(*F[0]);
					Wr.Id = FCString::Atoi(*F[1]);
					Wr.Grade = F.Num() > 2 ? FCString::Atoi(*F[2]) : -1;
					SP->Wears.Add(Wr);
				}
			}
		}
		SP->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		SP->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
