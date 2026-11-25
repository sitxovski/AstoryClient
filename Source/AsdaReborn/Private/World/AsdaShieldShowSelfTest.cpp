// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ МЕША ЩИТА (25.10.2026): `asda.SelfTest.ShieldShow [shot=<папка>] [quit]`. [ПОРТ]
//   Жалоба игроков: «Shield is functional and increases defense, but is visually not showing up or being held by the character».
//   Эталон — `CCharacter_SetParts_Armor@0x9EB3C0` (разбор у объявления `UAsdaCharAssembly::AttachShield`).
//   Офлайн (`-AsdaNoNet`), карта мира. Путь игрока (закон diagnostic-must-use-real-path):
//     • свой — ячейка контейнера 3 (`UAsdaInventorySubsystem::DebugPutItem` → та же общая точка, что ответ 5001:
//       `ApplySelfEquipPart`), спавн — пакет 4003, транспорт — делегат 5453 (`OnMountState`);
//     • сосед — пакеты 4005 (появление, слоты 8/9), 4021 (надел), 4023 (снял) через общий разбор `DebugFeedPacket`.
//   Проверка — по МОДЕЛИ: компоненты с тегом `ShieldMeshTag`, их меш, кость, единичный relative, текстура «Tex», мировая
//   позиция = позиция кости.
//     A) все щиты таблицы (ItemType 8): меш грузится, текстура грузится; без модели — только коды, у которых и у эталона
//        модели нет (пустое имя / модель предмета на земле itm1_*);
//     B) свой: надеть щит при мече → меш на "_7_a1"; ключ asda.Char.ShieldMesh 0 → меша нет (прежний порт = баг);
//        смена щита; смена оружия, снятие оружия — щит остаётся; снятие щита — меша нет;
//     C) бег, боевая стойка — щит на "_7_a1" и следует за костью; транспорт — "_2_a1" (оружие "2_a1"); спешился — снова в руке;
//     D) сосед: 4005 со щитом → меш; 4021 смена щита → новый меш; 4023 снятие → меша нет; 4005 без щита → меша нет.
//   Кадры (`shot=`): своя рука, бег, транспорт, сосед. Итог: «ЩИТ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "AsdaTestPackets.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

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
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры

DEFINE_LOG_CATEGORY_STATIC(LogAsdaShieldTest, Log, All);

namespace AsdaShieldTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.ShieldShow");   // имя команды
	static const TCHAR* kKey = TEXT("asda.Char.ShieldMesh");        // ключ отката меша щита
	constexpr int32 kMe = 4321;        // свой userNum (4003)
	constexpr int32 kPeer = 52;        // сосед со щитом
	constexpr int32 kPeerNo = 53;      // сосед без щита (контроль)

	// Шаг сценария: true — шаг завершён, false — ждёт следующего кадра. Аргумент — секунды от начала шага.
	using FStep = TFunction<bool(float)>;

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
		TUniquePtr<FAsdaNetSelfScope> NetScope;       // свой номер/уровень — вернуть после прогона
		TUniquePtr<FAsdaCVarScope> CVars;             // ключи — вернуть после прогона
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;       // мир
		TWeakObjectPtr<UAsdaItemTableSubsystem> Items;// таблицы предметов
		TWeakObjectPtr<UAsdaInventorySubsystem> Inv;  // инвентарь (контейнер 3 = надетое)
		int32 Sword = 0, Sword2 = 0;                  // два одноручных меча (подтип 0) с разными мешами
		int32 Shield = 0, Shield2 = 0;                // два щита с разными мешами
		int32 MountCode = 0;                          // транспорт (SubType 15)
		bool bMale = true;                            // пол своего персонажа
		bool bAbort = false;                          // прервать сценарий (нет данных для проверок)
		// ★ 26.10.2026 (уборка): что вернуть после прогона — надетое в слотах 8/9 (контейнер 3) и причёска своего.
		int32 OrigShieldCell = 0, OrigWeaponCell = 0; // предметы ячеек 3/8 и 3/9 до прогона (0 — пусто)
		int32 OrigHair = 0, OrigHairColor = 0;        // `SelfHair`/`SelfHairColor` до прогона (сценарий B ставит 0)
		bool bWornTouched = false;                    // сценарий успел надеть вещи (B0 прошёл)
		FRotator OrigSelfRot = FRotator::ZeroRotator; // поворот актёра своего до первого `Aim` (кадры разворачивают актёра)
		bool bSelfRotSaved = false;                   // поворот снят

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaShieldTest, Log,   TEXT("ЩИТ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaShieldTest, Error, TEXT("ЩИТ FAIL %s"), *What); }
		}
	};

	// Свой павн.
	static AAsdaPlayerCharacter* MeOf(const FState& S)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Компоненты меша щита у актёра (живые, зарегистрированные).
	static TArray<UStaticMeshComponent*> Shields(const AActor* A)
	{
		TArray<UStaticMeshComponent*> Out;
		if (!A) { return Out; }
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (UStaticMeshComponent* C : Comps)
		{
			if (IsValid(C) && C->IsRegistered() && C->ComponentHasTag(UAsdaCharAssembly::ShieldMeshTag)) { Out.Add(C); }
		}
		return Out;
	}

	// Кость меша оружия (NAME_None — оружия нет).
	static FName WeaponBone(const AActor* A)
	{
		if (!A) { return NAME_None; }
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (const UStaticMeshComponent* C : Comps)
		{
			if (IsValid(C) && C->IsRegistered() && C->ComponentHasTag(UAsdaCharAssembly::WeaponMeshTag)) { return C->GetAttachSocketName(); }
		}
		return NAME_None;
	}

	// Описание единственного щита: меш, кость, relative, текстура «Tex», расстояние до кости. Пусто — щитов не 1.
	struct FShieldInfo
	{
		int32 Count = 0;            // компонентов щита
		FString Mesh;               // имя меша
		FName Bone;                 // кость крепления
		bool bIdentity = false;     // relative единичный
		FString Tex;                // текстура параметра «Tex» слота 0
		float DistToBone = -1.f;    // расстояние мировой позиции меша до кости, см
		bool bHidden = false;       // скрыт
	};
	static FShieldInfo Describe(const AActor* A, const USkeletalMeshComponent* Skel)
	{
		FShieldInfo I;
		const TArray<UStaticMeshComponent*> L = Shields(A);
		I.Count = L.Num();
		if (L.Num() != 1) { return I; }
		const UStaticMeshComponent* C = L[0];
		I.Mesh = C->GetStaticMesh() ? C->GetStaticMesh()->GetName() : FString();
		I.Bone = C->GetAttachSocketName();
		I.bIdentity = C->GetRelativeTransform().Equals(FTransform::Identity, 0.001f);
		I.bHidden = C->bHiddenInGame;
		if (const UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)))
		{
			UTexture* T = nullptr;
			if (MID->GetTextureParameterValue(FMaterialParameterInfo(FName(AsdaMat::kParamTex)), T) && T) { I.Tex = T->GetName(); }
		}
		if (Skel && Skel->DoesSocketExist(I.Bone))
		{
			I.DistToBone = (float)FVector::Dist(C->GetComponentLocation(), Skel->GetSocketLocation(I.Bone));
		}
		return I;
	}
	static FString Str(const FShieldInfo& I)
	{
		return FString::Printf(TEXT("щитов %d, меш «%s», кость %s, relative=%s, tex «%s», до кости %.2f, скрыт %d"),
			I.Count, *I.Mesh, *I.Bone.ToString(), I.bIdentity ? TEXT("I") : TEXT("НЕ I"), *I.Tex, I.DistToBone, I.bHidden ? 1 : 0);
	}

	// Предмет ItemType `Type` (подтип `Sub`, −1 — любой) с загружаемым жёстким мешем; меш не `ExceptMesh`. 0 — нет.
	static int32 FindStatic(UAsdaItemTableSubsystem* Items, int32 Type, int32 Sub, bool bMale, const FString& ExceptMesh)
	{
		for (int32 Id = 20000; Id < 140000; ++Id)
		{
			const FAsdaItemRecord* Rec = Items->GetItem(Id);
			if (!Rec || Rec->ItemType != Type || (Sub >= 0 && Rec->ItemSubType != Sub)) { continue; }
			const FString Mesh = Items->GetItemMeshName(Id, bMale);
			if (Mesh.IsEmpty() || Mesh == ExceptMesh) { continue; }
			if (!LoadObject<UStaticMesh>(nullptr, *UAsdaCharAssembly::CharStaticMeshPath(Mesh))) { continue; }
			return Id;
		}
		return 0;
	}

	// Тело 4003 (9 б): userNum, тайлы, состояние, показ шлема.
	using AsdaTestPackets::Body4003;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.

	// Тело 4005 (151 б): сосед-мужчина, слоты экипа 8 (щит, +136) и 9 (оружие, +141), позиция game XZ.
	static TArray<uint8> Body4005(int32 Uidx, int32 ShieldId, int32 WeaponId, float GameX, float GameZ)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		B[6] = 1;                                             // шлем показан
		AsdaBytes::WrI32(B.GetData() + 11, 980000 + Uidx);    // charId — заведомо не свой
		const char* Name = "ShieldTest";
		FMemory::Memcpy(B.GetData() + 15, Name, FCStringAnsi::Strlen(Name));
		B[37] = 1; B[38] = 1; B[40] = 10;                     // пол, класс, уровень
		FMemory::Memcpy(B.GetData() + 56, &GameX, 4);
		FMemory::Memcpy(B.GetData() + 60, &GameZ, 4);
		AsdaBytes::WrI32(B.GetData() + 96 + 5 * 8, ShieldId); // слот 8 — щит
		AsdaBytes::WrI32(B.GetData() + 96 + 5 * 9, WeaponId); // слот 9 — оружие
		return B;
	}

	// Тело 4021/4023 (9 б): userIdx, экип-слот, предмет, заточка.
	static TArray<uint8> BodyPart(int32 Uidx, int32 Slot, int32 ItemId)
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		AsdaBytes::WrI16(B.GetData() + 2, (int16)Slot);
		AsdaBytes::WrI32(B.GetData() + 4, ItemId);
		return B;
	}

	// Надеть/снять вещь своему — ячейка контейнера 3 (общая точка ответа 5001). Нет ячейки — прямо в общую точку модели.
	static void Wear(FState& S, int32 Slot, int32 ItemId)
	{
		UAsdaInventorySubsystem* Inv = S.Inv.Get();
		if (Inv && Inv->DebugPutItem(3, Slot, ItemId, 1)) { return; }
		UE_LOG(LogAsdaShieldTest, Warning, TEXT("ЩИТ: ячейки 3/%d нет — общая точка ApplySelfEquipPart напрямую"), Slot);
		if (UAsdaWorldSubsystem* WS = S.WS.Get()) { WS->ApplySelfEquipPart(Slot, ItemId, 0); }
	}

	// Развернуть камеру на актёра спереди-слева (щит — на левой руке) и снять кадр.
	static void Aim(FState& S, AActor* Target)
	{
		UWorld* W = S.W.Get();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		AAsdaPlayerCharacter* Me = MeOf(S);
		if (!PC || !Me || !Target) { return; }
		// Персонажа поворачиваем к камере левым боком: камера смотрит вдоль своего Yaw, персонаж — навстречу, со сдвигом.
		const float CamYaw = PC->GetControlRotation().Yaw;
		// ★ 26.10.2026 (уборка): поворот своего актёра запоминается до первого разворота и возвращается в `Finish` — повёрнутый актёр
		//   ломал следующему самотесту хвост сетевой поправки (KB47: хвост — мировой вектор, а кладётся относительным смещением
		//   MeshRoot/камеры; при повороте актёра камера в кадр поправки «прыгала» на 362 ед.).
		if (Target == Me && !S.bSelfRotSaved) { S.OrigSelfRot = Me->GetActorRotation(); S.bSelfRotSaved = true; }
		Target->SetActorRotation(FRotator(0.f, CamYaw + 180.f - 50.f, 0.f));
	}
	static void Shot(const FState& S, const TCHAR* Name)
	{
		if (S.ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(S.ShotDir, FString(Name) + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaShieldTest, Log, TEXT("ЩИТ кадр %s"), *Path);
	}

	// Конец прогона.
	//   ★ 26.10.2026 УБОРКА (пакет ShieldShow → asda.Move.SelfTestKeyboard давал 157/101). Сценарий B кладёт свой 4003 с userNum
	//   kMe — это объявляет номер известным и пересоздаёт павна; `FAsdaNetSelfScope` возвращал номер и признак «неизвестен», а павн
	//   оставался. Следующий самотест видел павна (свой `FakeSelfSpawn` не делал), но `AsdaSelf::IsSelfUserIdx` для своего номера
	//   давал false (до 4003 «своим» не считается никто) — грант 4007 своему не применялся, персонаж не шёл. Теперь: соседи уходят
	//   штатным 4011, надетое, причёска и поворот актёра (`Aim`; после него KB47 давал 246/1) возвращаются, а оставленный павн
	//   получает тот же признак «номер известен», что ставит
	//   `Asda.World.FakeSelfSpawn` (номер — прежний, возвращённый снимком).
	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		if (UAsdaNetSubsystem* N = S.Net.Get())
		{
			for (const int32 U : { kPeer, kPeerNo })
			{
				TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(B.GetData(), (int16)U);
				N->DebugFeedPacket(4011, B);   // сосед ушёл из видимости
			}
		}
		if (AAsdaPlayerCharacter* Me = MeOf(S))
		{
			if (Me->IsMounted()) { Me->ClearMount(); }
			Me->SetCombatStance(false);
			Me->SetAnimState(EAsdaPlayerState::Idle);
			if (S.bSelfRotSaved) { Me->SetActorRotation(S.OrigSelfRot); }   // кадры разворачивали актёра (`Aim`)
		}
		if (S.bWornTouched)
		{
			Wear(S, 9, S.OrigWeaponCell);
			Wear(S, 8, S.OrigShieldCell);
		}
		S.CVars.Reset();
		S.NetScope.Reset();
		if (UAsdaNetSubsystem* N = S.Net.Get())
		{
			N->SelfHair = S.OrigHair; N->SelfHairColor = S.OrigHairColor;
			if (MeOf(S) && !N->IsSelfUserNumKnown()) { N->SetSelfUserNumForDebug(N->SelfUserNum); }
			UE_LOG(LogAsdaShieldTest, Log, TEXT("ЩИТ уборка: соседи %d/%d ушли, надетое 8=%d 9=%d, свой павн %s, поворот %s, номер %d (известен %d)"),
				kPeer, kPeerNo, S.OrigShieldCell, S.OrigWeaponCell, MeOf(S) ? TEXT("есть") : TEXT("нет"),
				MeOf(S) ? *MeOf(S)->GetActorRotation().ToString() : TEXT("—"), (int32)N->SelfUserNum, N->IsSelfUserNumKnown() ? 1 : 0);
		}
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaShieldTest, TEXT("ЩИТ"), S.Ok, S.Fail);
	}

	// Тик сценария.
	static void Tick(const TSharedRef<FState>& InSP, UWorld* InW)
	{
		// Своя ссылка: `Finish` снимает подписку, а с ней — копию состояния, которую держала лямбда тика.
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

		// ═══ A. Все щиты таблицы ═══
		S->Steps.Add(Do([S]()
		{
			UAsdaItemTableSubsystem* Items = S->Items.Get();
			int32 Total = 0, MeshOk = 0, TexOk = 0, NoModelLikeEtalon = 0, Bad = 0;
			TSet<FString> Meshes;
			FString BadList;
			for (int32 Id = 20000; Id < 140000; ++Id)
			{
				const FAsdaItemRecord* Rec = Items->GetItem(Id);
				if (!Rec || Rec->ItemType != 8) { continue; }
				++Total;
				for (const bool bM : { true, false })
				{
					const FString Mesh = Items->GetItemMeshName(Id, bM);
					// У эталона модели нет: пустое имя ("Model\.jbm") или модель предмета на земле (в Character\Model её нет).
					const bool bEtalonNone = Mesh.IsEmpty() || Mesh.StartsWith(TEXT("itm1_"));
					const bool bMesh = !Mesh.IsEmpty() && LoadObject<UStaticMesh>(nullptr, *UAsdaCharAssembly::CharStaticMeshPath(Mesh)) != nullptr;
					const FString Tex = Items->GetItemTexName(Id, bM);
					const bool bTex = !Tex.IsEmpty() && LoadObject<UTexture2D>(nullptr, *UAsdaCharAssembly::CharTexturePath(Tex)) != nullptr;
					if (bMesh && bTex) { if (bM) { ++MeshOk; ++TexOk; Meshes.Add(Mesh); } continue; }
					if (bEtalonNone) { if (bM) { ++NoModelLikeEtalon; } continue; }
					++Bad;
					if (BadList.Len() < 600) { BadList += FString::Printf(TEXT(" %d(%s:%s/%s)"), Id, bM ? TEXT("м") : TEXT("ж"), *Mesh, *Tex); }
				}
			}
			UE_LOG(LogAsdaShieldTest, Log, TEXT("ЩИТ A: щитов %d, с мешем и текстурой %d (мешей %d), без модели как у эталона %d, ошибок %d%s"),
				Total, MeshOk, Meshes.Num(), NoModelLikeEtalon, Bad, *BadList);
			S->Check(Total > 700 && Bad == 0 && MeshOk + NoModelLikeEtalon == Total,
				FString::Printf(TEXT("A1 все щиты таблицы: %d, меш+текстура %d, без модели (как у эталона) %d, ошибок %d"),
					Total, MeshOk, NoModelLikeEtalon, Bad));
		}));

		// ═══ B. Свой персонаж ═══
		S->Steps.Add(Do([S]()
		{
			UWorld* W = S->W.Get();
			UAsdaNetSubsystem* Net = S->Net.Get();
			S->OrigHair = Net->SelfHair; S->OrigHairColor = Net->SelfHairColor;   // ★ 26.10.2026: вернуть в уборке
			if (const UAsdaInventorySubsystem* Inv = S->Inv.Get())
			{
				const FAsdaItemInstance* C8 = Inv->GetCell(3, 8);
				const FAsdaItemInstance* C9 = Inv->GetCell(3, 9);
				S->OrigShieldCell = (C8 && !C8->IsEmpty()) ? C8->ItemCode : 0;
				S->OrigWeaponCell = (C9 && !C9->IsEmpty()) ? C9->ItemCode : 0;
			}
			Net->SelfHair = 0; Net->SelfHairColor = 0;
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			Net->DebugFeedPacket(4003, Body4003(kMe));
		}));
		// Эффект появления (столб света) держится ≈3 с — кадры своего персонажа снимаем после него.
		S->Steps.Add(Wait(4.0f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			UAsdaItemTableSubsystem* Items = S->Items.Get();
			S->bMale = S->WS.Get() && S->WS->SelfSex != 2;
			S->Sword   = FindStatic(Items, 9, 0, true, FString());
			// Второе оружие правой руки с другим мешем: одноручное (0), иначе 1/3/4 (луки 8/10 — левая рука, не берём).
			for (const int32 Sub : { 0, 1, 3, 4 })
			{
				if (S->Sword2 == 0) { S->Sword2 = FindStatic(Items, 9, Sub, true, Items->GetItemMeshName(S->Sword, true)); }
			}
			S->Shield  = FindStatic(Items, 8, -1, S->bMale, FString());
			S->Shield2 = FindStatic(Items, 8, -1, S->bMale, Items->GetItemMeshName(S->Shield, S->bMale));
			// Транспорт: коды DT_special_itemlist (SubType 15) лежат в 1…4000 — тот же перебор, что у asda.Mount.FxSelfTest.
			for (int32 Id = 1; Id <= 4000 && S->MountCode == 0; ++Id)
			{
				FString Mdl, Set; float H = 0.f;
				if (S->WS->LookupMount(Id, Mdl, Set, H)) { S->MountCode = Id; }
			}
			S->Check(Me && S->Sword && S->Sword2 && S->Shield && S->Shield2 && S->MountCode,
				FString::Printf(TEXT("B0 павн %s; мечи %d/%d, щиты %d (%s) / %d (%s), транспорт %d"), Me ? TEXT("есть") : TEXT("НЕТ"),
					S->Sword, S->Sword2, S->Shield, *Items->GetItemMeshName(S->Shield, S->bMale), S->Shield2,
					*Items->GetItemMeshName(S->Shield2, S->bMale), S->MountCode));
			if (!Me || !S->Sword || !S->Sword2 || !S->Shield || !S->Shield2 || !S->MountCode) { S->bAbort = true; return; }
			S->bWornTouched = true;
			Wear(*S, 9, S->Sword);
			Wear(*S, 8, S->Shield);
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			UAsdaItemTableSubsystem* Items = S->Items.Get();
			const FShieldInfo I = Describe(Me, Me ? Me->BaseSkel : nullptr);
			S->Check(I.Count == 1 && I.Mesh == Items->GetItemMeshName(S->Shield, S->bMale) && I.Bone == FName(TEXT("_7_a1")) && I.bIdentity
				&& I.Tex == Items->GetItemTexName(S->Shield, S->bMale) && I.DistToBone >= 0.f && I.DistToBone < 1.f && !I.bHidden,
				FString::Printf(TEXT("B1 надет щит %d при мече %d: %s"), S->Shield, S->Sword, *Str(I)));
			S->Check(WeaponBone(Me) == FName(TEXT("8_a1")), FString::Printf(TEXT("B2 меч в правой руке: кость %s"), *WeaponBone(Me).ToString()));
			S->Check(S->WS->SelfParts.Shield == S->Shield, FString::Printf(TEXT("B3 снимок внешности: Shield=%d"), S->WS->SelfParts.Shield));
			// Ключ 0 — прежний порт: тот же путь надевания, меша нет (это и есть жалоба игроков).
			S->CVars->Set(kKey, 0);
			Wear(*S, 8, S->Shield);
			S->Check(Shields(MeOf(*S)).Num() == 0, FString::Printf(TEXT("B4 ключ asda.Char.ShieldMesh 0 (прежний порт): меша щита нет (%d) — баг воспроизведён"),
				Shields(MeOf(*S)).Num()));
			S->CVars->Set(kKey, 1);
			Wear(*S, 8, S->Shield);
			S->Check(Shields(MeOf(*S)).Num() == 1, FString::Printf(TEXT("B5 ключ 1: меш щита снова есть (%d)"), Shields(MeOf(*S)).Num()));
		}));
		S->Steps.Add(Do([S]() { Aim(*S, MeOf(*S)); }));
		S->Steps.Add(Wait(0.8f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("shield_self_hand")); }));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S]()
		{
			UAsdaItemTableSubsystem* Items = S->Items.Get();
			Wear(*S, 8, S->Shield2);
			AAsdaPlayerCharacter* Me = MeOf(*S);
			FShieldInfo I = Describe(Me, Me->BaseSkel);
			S->Check(I.Count == 1 && I.Mesh == Items->GetItemMeshName(S->Shield2, S->bMale) && I.Bone == FName(TEXT("_7_a1")),
				FString::Printf(TEXT("B6 смена щита → %d: %s"), S->Shield2, *Str(I)));
			Wear(*S, 9, S->Sword2);
			I = Describe(Me, Me->BaseSkel);
			S->Check(I.Count == 1 && I.Mesh == Items->GetItemMeshName(S->Shield2, S->bMale) && I.Bone == FName(TEXT("_7_a1")) && WeaponBone(Me) == FName(TEXT("8_a1")),
				FString::Printf(TEXT("B7 смена оружия → %d: щит на месте (%s), меч на %s"), S->Sword2, *Str(I), *WeaponBone(Me).ToString()));
			Wear(*S, 9, 0);
			I = Describe(Me, Me->BaseSkel);
			S->Check(I.Count == 1 && I.Bone == FName(TEXT("_7_a1")) && WeaponBone(Me) == NAME_None,
				FString::Printf(TEXT("B8 оружие снято: щит остаётся (%s), оружия нет (%s)"), *Str(I), *WeaponBone(Me).ToString()));
			Wear(*S, 8, 0);
			S->Check(Shields(Me).Num() == 0 && S->WS->SelfParts.Shield == 0,
				FString::Printf(TEXT("B9 щит снят: компонентов %d, снимок %d"), Shields(Me).Num(), S->WS->SelfParts.Shield));
			Wear(*S, 9, S->Sword);
			Wear(*S, 8, S->Shield);
			S->Check(Shields(Me).Num() == 1, FString::Printf(TEXT("B10 надет снова: компонентов %d (повторная пересборка не копит меши)"), Shields(Me).Num()));
		}));

		// ═══ C. Бег, боевая стойка, транспорт ═══
		S->Steps.Add(Do([S]() { MeOf(*S)->SetAnimState(EAsdaPlayerState::Move); Aim(*S, MeOf(*S)); }));
		S->Steps.Add(Wait(0.6f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FShieldInfo I = Describe(Me, Me->BaseSkel);
			S->Check(I.Count == 1 && I.Bone == FName(TEXT("_7_a1")) && I.DistToBone >= 0.f && I.DistToBone < 1.f,
				FString::Printf(TEXT("C1 бег: щит в руке и идёт за костью (%s)"), *Str(I)));
			Shot(*S, TEXT("shield_self_run"));
		}));
		S->Steps.Add(Wait(0.4f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			Me->SetAnimState(EAsdaPlayerState::Idle);
			Me->SetCombatStance(true);
		}));
		S->Steps.Add(Wait(0.4f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FShieldInfo I = Describe(Me, Me->BaseSkel);
			S->Check(Me->IsCombatStance() && I.Count == 1 && I.Bone == FName(TEXT("_7_a1")) && I.DistToBone >= 0.f && I.DistToBone < 1.f,
				FString::Printf(TEXT("C2 боевая стойка %d: щит в руке (%s)"), Me->IsCombatStance() ? 1 : 0, *Str(I)));
			Me->SetCombatStance(false);
			// Транспорт — тем же делегатом, что приёмник 5453.
			S->Net->OnMountState.Broadcast(kMe, S->MountCode, true);
		}));
		S->Steps.Add(Wait(1.0f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FShieldInfo I = Describe(Me, Me->BaseSkel);
			S->Check(Me->IsMounted() && I.Count == 1 && I.Bone == UAsdaCharAssembly::ShieldAttachBone(/*bStowed*/true) && I.bIdentity && I.DistToBone >= 0.f && I.DistToBone < 1.f
				&& WeaponBone(Me) == FName(TEXT("2_a1")),
				FString::Printf(TEXT("C3 транспорт %d (верхом %d): щит за спиной (%s), меч на %s"), S->MountCode, Me->IsMounted() ? 1 : 0, *Str(I),
					*WeaponBone(Me).ToString()));
			Aim(*S, Me);
		}));
		S->Steps.Add(Wait(0.6f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("shield_self_mount")); }));
		S->Steps.Add(Wait(0.4f));
		S->Steps.Add(Do([S]() { S->Net->OnMountState.Broadcast(kMe, S->MountCode, false); }));
		S->Steps.Add(Wait(0.6f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FShieldInfo I = Describe(Me, Me->BaseSkel);
			S->Check(!Me->IsMounted() && I.Count == 1 && I.Bone == FName(TEXT("_7_a1")) && WeaponBone(Me) == FName(TEXT("8_a1")),
				FString::Printf(TEXT("C4 спешился: щит снова в руке (%s), меч на %s"), *Str(I), *WeaponBone(Me).ToString()));
		}));

		// ═══ D. Сосед ═══
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FVector2D P = Me->GetGamePosXZ();
			S->Net->DebugFeedPacket(4005, Body4005(kPeer, S->Shield, S->Sword, P.X + 150.f, P.Y));
			S->Net->DebugFeedPacket(4005, Body4005(kPeerNo, 0, S->Sword, P.X - 150.f, P.Y));
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			UAsdaItemTableSubsystem* Items = S->Items.Get();
			AAsdaPlayerCharacter* PA = Cast<AAsdaPlayerCharacter>(S->WS->ActorByUserIdx(kPeer));
			AAsdaPlayerCharacter* PN = Cast<AAsdaPlayerCharacter>(S->WS->ActorByUserIdx(kPeerNo));
			const FShieldInfo I = Describe(PA, PA ? PA->BaseSkel : nullptr);
			S->Check(PA && I.Count == 1 && I.Mesh == Items->GetItemMeshName(S->Shield, true) && I.Bone == FName(TEXT("_7_a1")) && I.bIdentity
				&& I.Tex == Items->GetItemTexName(S->Shield, true) && I.DistToBone >= 0.f && I.DistToBone < 1.f,
				FString::Printf(TEXT("D1 4005 соседа со щитом %d: %s"), S->Shield, *Str(I)));
			S->Check(PN && Shields(PN).Num() == 0, FString::Printf(TEXT("D2 4005 соседа без щита: компонентов %d"), Shields(PN).Num()));
			Aim(*S, PA);
		}));
		S->Steps.Add(Wait(0.8f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("shield_peer")); }));
		S->Steps.Add(Wait(0.4f));
		S->Steps.Add(Do([S]()
		{
			UAsdaItemTableSubsystem* Items = S->Items.Get();
			S->Net->DebugFeedPacket(4021, BodyPart(kPeer, 8, S->Shield2));
			AAsdaPlayerCharacter* PA = Cast<AAsdaPlayerCharacter>(S->WS->ActorByUserIdx(kPeer));
			FShieldInfo I = Describe(PA, PA ? PA->BaseSkel : nullptr);
			S->Check(I.Count == 1 && I.Mesh == Items->GetItemMeshName(S->Shield2, true) && I.Bone == FName(TEXT("_7_a1")),
				FString::Printf(TEXT("D3 4021 слот 8 → %d: %s"), S->Shield2, *Str(I)));
			S->Net->DebugFeedPacket(4021, BodyPart(kPeer, 9, S->Sword2));
			I = Describe(PA, PA ? PA->BaseSkel : nullptr);
			S->Check(I.Count == 1 && I.Mesh == Items->GetItemMeshName(S->Shield2, true), FString::Printf(TEXT("D4 4021 смена оружия соседу: щит на месте (%s)"), *Str(I)));
			S->Net->DebugFeedPacket(4023, BodyPart(kPeer, 8, 0));
			S->Check(Shields(PA).Num() == 0, FString::Printf(TEXT("D5 4023 слот 8: щит снят, компонентов %d"), Shields(PA).Num()));
			S->Net->DebugFeedPacket(4021, BodyPart(kPeer, 8, S->Shield));
			S->Check(Shields(PA).Num() == 1, FString::Printf(TEXT("D6 4021 снова надел: компонентов %d"), Shields(PA).Num()));
		}));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestShieldShow(
	TEXT("asda.SelfTest.ShieldShow"),
	TEXT("Самотест меша щита (CCharacter_SetParts_Armor): все щиты таблицы, свой (надел/снял/смена щита и оружия, ключ "
	     "asda.Char.ShieldMesh 0/1), бег, стойка, транспорт (за спину), сосед 4005/4021/4023. Офлайн, карта мира. "
	     "`shot=<папка>` — кадры, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaShieldTest;
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
