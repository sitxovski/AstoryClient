// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ПРОЗРАЧНЫХ ЧАСТЕЙ ПЕРСОНАЖА (31.10.2026, волна acce_alpha): `asda.SelfTest.ItemAlpha [shot=<папка>] [quit]`. [ПОРТ]
//   Жалоба владельца: у прозрачных аксессуаров вместо модели белая плашка — на персонаже и в окнах предпросмотра.
//   Эталон и правило — `UAsdaCharAssembly::ApplyItemTexture` (AsdaCharAssembly.cpp) и Reports/acce_alpha/REPORT.md.
//   Офлайн (`-AsdaNoNet`), карта мира. Путь игрока: своя экипировка — ячейка контейнера 3 (`DebugPutItem`, та же общая точка,
//   что ответ 5001 → `ApplySelfEquipPart` → `AttachPart`/`AttachWeapon` → `ApplyItemTexture`).
//     A) данные: оба варианта мастера грузятся; предметы item_img_table, попадающие в таблицу (оба пола), — Blend/Mask > 0;
//        у каждой записи таблицы (кроме acce* — их нет в таблицах предметов) есть меш части;
//     B) свой персонаж: по слотам 0–4 и 9 надеть предмет режима Mask, режима Blend и контрольный (не в таблице) —
//        мастер слота = M_AsdaCharLitMask / M_AsdaCharLitBlend / M_AsdaCharLit, текстура «Tex» = текстура предмета;
//     C) ключ asda.Char.ItemAlpha 0 → тот же предмет снова на M_AsdaCharLit (прежний порт = плашка); 1 → снова вариант.
//   Кадры (`shot=`): набор прозрачных частей при ключе 1 и 0. Итог: «АЛЬФА ИТОГ: ok=N fail=M».
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
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры

#include "../Characters/AsdaCharAlphaParts.inl"   // таблица прозрачных частей (данные; проверяется код применения)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaItemAlphaTest, Log, All);

namespace AsdaItemAlphaTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.ItemAlpha");   // имя команды
	static const TCHAR* kKey = TEXT("asda.Char.ItemAlpha");        // ключ отката
	constexpr int32 kMe = 4321;                                    // свой userNum (4003)
	static const int32 kSlots[] = { 0, 1, 2, 3, 4, 9 };            // шлем, грудь, штаны, обувь, перчатки, оружие

	// Шаг сценария: true — шаг завершён, false — ждёт следующего кадра. Аргумент — секунды от начала шага.
	using FStep = TFunction<bool(float)>;

	// Проверяемый предмет: слот, код, ожидаемый режим (0 — непрозрачный, 1 — Blend, 2 — Mask).
	struct FPick { int32 Slot = 0; int32 Id = 0; uint8 Mode = 0; };

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
		TMap<FString, uint8> Table;                   // «меш|текстура» → режим
		TArray<FPick> Picks;                          // проверяемые предметы
		TMap<int32, int32> Orig;                      // слот → надетое до прогона
		bool bMale = true;                            // пол своего персонажа
		bool bAbort = false;                          // прервать сценарий
		FRotator OrigRot = FRotator::ZeroRotator;     // поворот актёра до кадров
		bool bRotSaved = false;                       // поворот снят

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaItemAlphaTest, Log,   TEXT("АЛЬФА ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaItemAlphaTest, Error, TEXT("АЛЬФА FAIL %s"), *What); }
		}
		// Режим пары по таблице (независимо от кода применения).
		uint8 ModeOf(const FString& Mesh, const FString& Tex) const
		{
			if (const uint8* V = Table.Find(Mesh.ToLower() + TEXT("|") + Tex.ToLower())) { return *V; }
			if (const uint8* V = Table.Find(Mesh.ToLower() + TEXT("|"))) { return *V; }
			return 0;
		}
	};

	// Свой павн.
	static AAsdaPlayerCharacter* MeOf(const FState& S)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Слот экипировки по сегменту имени меша части (ch1_<пол>_<класс>_<часть>NNN…; ch1_n_* — оружие). −1 — не наш слот.
	static int32 SlotOfMesh(const FString& Mesh)
	{
		if (Mesh.StartsWith(TEXT("ch1_n_"))) { return 9; }
		static const TCHAR* kPart[] = { TEXT("_hcap"), TEXT("_chst"), TEXT("_pant"), TEXT("_shoe"), TEXT("_grov") };
		for (int32 i = 0; i < 5; ++i) { if (Mesh.Contains(kPart[i])) { return i; } }
		return -1;
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

	// Мастер и текстура «Tex» прозрачного слота компонента («—» — нет): слот бит-подсеток `_asdabit`, если меш разделён
	//   (баг 8, UAsdaAlphaSplitLib), иначе слот 0.
	static void Describe(const UMeshComponent* C, FString& OutBase, FString& OutTex)
	{
		OutBase = TEXT("—"); OutTex = TEXT("—");
		int32 Slot = 0;   // проверяемый слот
		if (C)
		{
			const TArray<FName> Slots = C->GetMaterialSlotNames();
			for (int32 i = 0; i < Slots.Num(); ++i) { if (Slots[i].ToString().EndsWith(AsdaMat::kBitSlotSuffix)) { Slot = i; } }
		}
		const UMaterialInterface* M = (C && C->GetNumMaterials() > Slot) ? C->GetMaterial(Slot) : nullptr;
		if (!M) { return; }
		OutBase = GetNameSafe(M->GetMaterial());
		UTexture* T = nullptr;
		if (M->GetTextureParameterValue(FName(AsdaMat::kParamTex), T) && T) { OutTex = T->GetName(); }
	}

	// Надеть/снять вещь своему — ячейка контейнера 3 (общая точка ответа 5001). Нет ячейки — прямо в общую точку модели.
	static void Wear(FState& S, int32 Slot, int32 ItemId)
	{
		UAsdaInventorySubsystem* Inv = S.Inv.Get();
		if (Inv && Inv->DebugPutItem(3, Slot, ItemId, 1)) { return; }
		if (UAsdaWorldSubsystem* WS = S.WS.Get()) { WS->ApplySelfEquipPart(Slot, ItemId, 0); }
	}

	// Проверить надетый предмет: мастер слота по режиму, текстура предмета.
	static void CheckPick(FState& S, const FPick& P, uint8 WantMode, const TCHAR* Tag)
	{
		UAsdaItemTableSubsystem* Items = S.Items.Get();
		const FString Mesh = Items->GetItemMeshName(P.Id, P.Slot == 9 ? true : S.bMale);
		const FString Tex = Items->GetItemTexName(P.Id, P.Slot == 9 ? true : S.bMale);
		const UMeshComponent* C = PartComp(MeOf(S), Mesh);
		FString Base, T;
		Describe(C, Base, T);
		static const TCHAR* kWant[] = { TEXT("M_AsdaCharLit"), TEXT("M_AsdaCharLitBlend"), TEXT("M_AsdaCharLitMask") };
		S.Check(C && Base == kWant[WantMode] && T.Equals(Tex, ESearchCase::IgnoreCase),
			FString::Printf(TEXT("%s слот %d предмет %d (%s, tex %s): мастер %s (ждём %s), Tex %s"),
				Tag, P.Slot, P.Id, *Mesh, *Tex, *Base, kWant[WantMode], *T));
	}

	// Развернуть своего к камере и снять кадр.
	static void Shot(FState& S, const TCHAR* Name)
	{
		UWorld* W = S.W.Get();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		AAsdaPlayerCharacter* Me = MeOf(S);
		if (PC && Me)
		{
			if (!S.bRotSaved) { S.OrigRot = Me->GetActorRotation(); S.bRotSaved = true; }
			Me->SetActorRotation(FRotator(0.f, PC->GetControlRotation().Yaw + 180.f - 20.f, 0.f));   // лицом к камере
		}
		if (S.ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(S.ShotDir, FString(Name) + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaItemAlphaTest, Log, TEXT("АЛЬФА кадр %s"), *Path);
	}

	// Конец прогона: вернуть надетое, поворот, ключи, номер.
	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		for (const TPair<int32, int32>& O : S.Orig) { Wear(S, O.Key, O.Value); }
		if (AAsdaPlayerCharacter* Me = MeOf(S)) { if (S.bRotSaved) { Me->SetActorRotation(S.OrigRot); } }
		S.CVars.Reset();
		S.NetScope.Reset();
		if (UAsdaNetSubsystem* N = S.Net.Get())
		{
			if (MeOf(S) && !N->IsSelfUserNumKnown()) { N->SetSelfUserNumForDebug(N->SelfUserNum); }
		}
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaItemAlphaTest, TEXT("АЛЬФА"), S.Ok, S.Fail);
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

		// ═══ A. Данные ═══
		S->Steps.Add(Do([S]()
		{
			for (const FAsdaCharAlphaPart& P : GAsdaCharAlphaParts) { S->Table.Add(FString(P.Mesh) + TEXT("|") + P.Tex, P.Mode); }
			const bool bB = LoadObject<UMaterial>(nullptr, AsdaMat::kCharLitBlendPath) != nullptr;
			const bool bM = LoadObject<UMaterial>(nullptr, AsdaMat::kCharLitMaskPath) != nullptr;
			S->Check(bB && bM, FString::Printf(TEXT("A1 варианты мастера: Blend %d, Mask %d"), bB ? 1 : 0, bM ? 1 : 0));
			UAsdaItemTableSubsystem* Items = S->Items.Get();
			int32 NB = 0, NM = 0;
			for (int32 Id = 20000; Id < 140000; ++Id)
			{
				if (!Items->GetItem(Id)) { continue; }
				for (const bool bMale : { true, false })
				{
					const uint8 Md = S->ModeOf(Items->GetItemMeshName(Id, bMale), Items->GetItemTexName(Id, bMale));
					NB += Md == 1; NM += Md == 2;
				}
			}
			int32 NoMesh = 0, NonAcce = 0;
			FString Miss;
			for (const FAsdaCharAlphaPart& P : GAsdaCharAlphaParts)
			{
				const FString Mesh(P.Mesh);
				if (Mesh.StartsWith(TEXT("acce"))) { continue; }
				++NonAcce;
				const bool bHas = LoadObject<UObject>(nullptr, *UAsdaCharAssembly::EquipPartMeshPath(Mesh), nullptr, LOAD_NoWarn | LOAD_Quiet)
					|| LoadObject<UStaticMesh>(nullptr, *UAsdaCharAssembly::CharStaticMeshPath(Mesh), nullptr, LOAD_NoWarn | LOAD_Quiet);
				if (!bHas) { ++NoMesh; if (Miss.Len() < 400) { Miss += TEXT(" ") + Mesh; } }
			}
			S->Check(NB > 0 && NM > 0, FString::Printf(TEXT("A2 предметов (пол×код) в таблице: Blend %d, Mask %d"), NB, NM));
			S->Check(NoMesh == 0, FString::Printf(TEXT("A3 записей таблицы (кроме acce*) %d, без меша части %d%s"), NonAcce, NoMesh, *Miss));
			// ★ 01.11.2026 (баг 8): у КАЖДОГО смешанного меша таблицы есть слот бит-подсеток `_asdabit` (переимпорт стирает его).
			TSet<FString> MixedSeen;
			int32 NoSlot = 0;
			FString MissSlot;
			for (const FAsdaCharAlphaPart& P : GAsdaCharAlphaParts)
			{
				const FString Mesh(P.Mesh);
				if (!P.bMixed || MixedSeen.Contains(Mesh)) { continue; }
				MixedSeen.Add(Mesh);
				TArray<FName> Slots;
				if (const USkeletalMesh* SKM = UAsdaCharAssembly::LoadEquipPartMesh(Mesh))
				{
					for (const FSkeletalMaterial& Mt : SKM->GetMaterials()) { Slots.Add(Mt.MaterialSlotName); }
				}
				else if (const UStaticMesh* STM = LoadObject<UStaticMesh>(nullptr, *UAsdaCharAssembly::CharStaticMeshPath(Mesh), nullptr, LOAD_NoWarn | LOAD_Quiet))
				{
					for (const FStaticMaterial& Mt : STM->GetStaticMaterials()) { Slots.Add(Mt.MaterialSlotName); }
				}
				bool bSlot = false;
				for (const FName& N : Slots) { bSlot |= N.ToString().EndsWith(AsdaMat::kBitSlotSuffix); }
				if (!bSlot) { ++NoSlot; if (MissSlot.Len() < 400) { MissSlot += TEXT(" ") + Mesh; } }
			}
			S->Check(MixedSeen.Num() > 0 && NoSlot == 0, FString::Printf(TEXT("A4 смешанных мешей %d, без слота бит-подсеток %d%s"),
				MixedSeen.Num(), NoSlot, *MissSlot));
		}));
		// ★ 01.11.2026 (баг 8): защита от переимпорта — смешанный меш БЕЗ слота `_asdabit` (копия cbow103 с переименованным
		//   слотом, как после переимпорта) не получает прозрачный мастер ни на один слот; исходный — Mask только слоту бит-подсеток.
		S->Steps.Add(Do([S]()
		{
			static const TCHAR* kMesh = TEXT("ch1_n_04_cbow103_00_00_0");   // смешанный меш оружия, Mask (таблица)
			UStaticMesh* Src = LoadObject<UStaticMesh>(nullptr, *UAsdaCharAssembly::CharStaticMeshPath(kMesh), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Src) { S->Check(false, TEXT("A5 нет меша cbow103")); return; }
			// Мастер каждого слота компонента с мешем `M` после ApplyItemTexture: «имя слота=мастер».
			auto Run = [](UStaticMesh* M) -> FString
			{
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetTransientPackage());
				C->SetStaticMesh(M);
				UAsdaCharAssembly::ApplyItemTexture(C, kMesh);
				FString R;
				const TArray<FName> Slots = C->GetMaterialSlotNames();
				for (int32 i = 0; i < C->GetNumMaterials(); ++i)
				{
					const UMaterialInterface* Mi = C->GetMaterial(i);
					R += FString::Printf(TEXT(" %s=%s"), Slots.IsValidIndex(i) ? *Slots[i].ToString() : TEXT("?"),
						Mi && Mi->GetMaterial() ? *Mi->GetMaterial()->GetName() : TEXT("—"));
				}
				return R;
			};
			const FString Orig = Run(Src);
			// «Переимпортированная» копия: то же имя и те же материалы слотов, у слота бит-подсеток стёрт хвост `_asdabit`.
			// ★ 11.11.2026 (D3, падение cooked-игры): копия ДОЛЖНА быть новым пустым мешем, а не DuplicateObject исходного.
			//   DuplicateObject сериализует подобъект NavCollision cooked-меша без cooked-данных, и в собранной игре
			//   UNavCollision::Serialize падает Fatal (NavCollision.cpp:535 «NavCollision data was not cooked»). Игра меши
			//   не дублирует — дефект только стенда. ApplyItemTexture читает у меша лишь имя, имена слотов и материалы:
			//   у нового меша они те же (список материалов скопирован, слот переименован), поэтому проверка та же самая.
			UStaticMesh* Dup = FindObject<UStaticMesh>(GetTransientPackage(), kMesh);   // копия с прошлого прогона
			if (!Dup) { Dup = NewObject<UStaticMesh>(GetTransientPackage(), FName(kMesh), RF_Transient); }
			TArray<FStaticMaterial> Mats = Src->GetStaticMaterials();   // материалы и имена слотов исходного меша
			for (FStaticMaterial& Mt : Mats)
			{
				FString N = Mt.MaterialSlotName.ToString();
				if (N.RemoveFromEnd(AsdaMat::kBitSlotSuffix)) { Mt.MaterialSlotName = FName(*(N + TEXT("_reimport"))); }
			}
			Dup->SetStaticMaterials(Mats);
			const FString Re = Run(Dup);
			const FString BitSlot = FString(kMesh) + AsdaMat::kBitSlotSuffix + TEXT("=M_AsdaCharLitMask");
			S->Check(Orig.Contains(BitSlot) && Orig.Contains(FString(kMesh) + TEXT("=M_AsdaCharLit ")) ,
				FString::Printf(TEXT("A5 исходный меш: Mask только слоту бит-подсеток:%s"), *Orig));
			S->Check(!Re.Contains(TEXT("M_AsdaCharLitMask")) && !Re.Contains(TEXT("M_AsdaCharLitBlend")) && Re.Contains(TEXT("=M_AsdaCharLit")),
				FString::Printf(TEXT("A5 без слота _asdabit (переимпорт): весь меш непрозрачный:%s"), *Re));
		}));

		// ═══ B. Свой персонаж ═══
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
			UAsdaItemTableSubsystem* Items = S->Items.Get();
			S->bMale = S->WS.Get() && S->WS->SelfSex != 2;
			// Предметы: по каждому слоту — по одному режима Mask, Blend и контрольному (первый по коду; для шлема Mask —
			//   венок hcap333, если подходит полу).
			for (const int32 Slot : kSlots)
			{
				int32 Have[3] = { 0, 0, 0 };
				for (int32 Id = 20000; Id < 140000; ++Id)
				{
					if (!Items->GetItem(Id)) { continue; }
					const bool bSexM = Slot == 9 ? true : S->bMale;
					const FString Mesh = Items->GetItemMeshName(Id, bSexM);
					if (Mesh.IsEmpty() || SlotOfMesh(Mesh) != Slot) { continue; }
					const uint8 Md = S->ModeOf(Mesh, Items->GetItemTexName(Id, bSexM));
					if (Md == 2 && Slot == 0 && Mesh.Contains(TEXT("hcap333"))) { Have[2] = Id; continue; }
					if (Have[Md] == 0) { Have[Md] = Id; }
				}
				for (uint8 Md = 0; Md < 3; ++Md) { if (Have[Md]) { FPick P; P.Slot = Slot; P.Id = Have[Md]; P.Mode = Md; S->Picks.Add(P); } }
				if (const UAsdaInventorySubsystem* Inv = S->Inv.Get())
				{
					const FAsdaItemInstance* C = Inv->GetCell(3, Slot);
					S->Orig.Add(Slot, (C && !C->IsEmpty()) ? C->ItemCode : 0);
				}
			}
			int32 NB = 0, NM = 0;
			for (const FPick& P : S->Picks) { NB += P.Mode == 1; NM += P.Mode == 2; }
			S->Check(Me && NB > 0 && NM > 0, FString::Printf(TEXT("B0 павн %s, пол %s; предметов к проверке %d (Blend %d, Mask %d)"),
				Me ? TEXT("есть") : TEXT("НЕТ"), S->bMale ? TEXT("м") : TEXT("ж"), S->Picks.Num(), NB, NM));
			if (!Me || S->Picks.Num() == 0) { S->bAbort = true; }
		}));
		// По предмету: надеть → проверить (ключ 1).
		for (int32 k = 0; k < 18; ++k)
		{
			S->Steps.Add(Do([S, k]()
			{
				if (!S->Picks.IsValidIndex(k)) { return; }
				Wear(*S, S->Picks[k].Slot, S->Picks[k].Id);
			}));
			S->Steps.Add(Wait(0.2f));
			S->Steps.Add(Do([S, k]()
			{
				if (!S->Picks.IsValidIndex(k)) { return; }
				CheckPick(*S, S->Picks[k], S->Picks[k].Mode, TEXT("B1"));
			}));
		}
		// ═══ C. Ключ отката и кадры: надеть прозрачные варианты всех слотов, кадр при 1, затем при 0 ═══
		S->Steps.Add(Do([S]()
		{
			TSet<int32> Done;
			for (const uint8 Want : { (uint8)2, (uint8)1 })   // Mask предпочтительнее (венок), иначе Blend
			{
				for (const FPick& P : S->Picks) { if (P.Mode == Want && !Done.Contains(P.Slot)) { Wear(*S, P.Slot, P.Id); Done.Add(P.Slot); } }
			}
		}));
		S->Steps.Add(Wait(1.0f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("itemalpha_key1")); }));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S]()
		{
			S->CVars->Set(kKey, 0);
			for (const FPick& P : S->Picks)
			{
				if (P.Mode == 0) { continue; }
				Wear(*S, P.Slot, P.Id);
				CheckPick(*S, P, 0, TEXT("C1 ключ 0 (прежний порт)"));
			}
			TSet<int32> Done;
			for (const uint8 Want : { (uint8)2, (uint8)1 })
			{
				for (const FPick& P : S->Picks) { if (P.Mode == Want && !Done.Contains(P.Slot)) { Wear(*S, P.Slot, P.Id); Done.Add(P.Slot); } }
			}
		}));
		S->Steps.Add(Wait(1.0f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("itemalpha_key0")); }));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S]()
		{
			S->CVars->Set(kKey, 1);
			for (const FPick& P : S->Picks)
			{
				if (P.Mode == 0) { continue; }
				Wear(*S, P.Slot, P.Id);
				CheckPick(*S, P, P.Mode, TEXT("C2 ключ 1 снова"));
			}
		}));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestItemAlpha(
	TEXT("asda.SelfTest.ItemAlpha"),
	TEXT("Самотест прозрачных частей персонажа (бит очереди .jbm → M_AsdaCharLitBlend/Mask): данные таблицы, свой персонаж по "
	     "слотам 0–4 и 9 (Mask/Blend/контроль), ключ asda.Char.ItemAlpha 0/1. Офлайн, карта мира. `shot=<папка>` — кадры, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaItemAlphaTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (!Net || !WS || !Items || !Inv || !IConsoleManager::Get().FindConsoleVariable(kKey))
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
		}
		SP->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		SP->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		SP->CVars->Set(kKey, 1);
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
