// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВОЛНЫ В1 (часть C, эффекты/рендер) АУДИТА ДУБЛЕЙ №2 — СВЕРКА ВЫНЕСЕННЫХ ТОЧЕК С ПРЕЖНИМИ КОПИЯМИ
//   (11.10.2026; план `Reports/dedup_audit2/00_PLAN.md` §5 В1, находки `03_fx_render.md`, отчёт `wave1_C.md`).
//
// ★ ЗАЧЕМ. Волна В1 — ЧИСТЫЙ ВЫНОС: имена, пути, теги, сроки жизни, привязки и веса света меняться не должны. Здесь каждая
//   новая общая точка сравнивается с КОПИЕЙ ПРЕЖНЕГО КОДА (функции и литералы `Old*` ниже — дословно из бэкапа
//   `_backup/2026-10-11_dedup2_w1C`) на одних и тех же входах:
//     • F5/F6/F10/F16 — каждая константа против прежнего литерала: строка побайтно, `FName` с учётом регистра, и для путей —
//       тот же загруженный объект (`LoadObject`/`LoadClass`); путь класса BP по правилу реестра против прежнего литерала;
//     • F6 — `AsdaCharFxTags::DestroyAttached` против двух прежних копий гашения (один тег — мир; «оружие ИЛИ щит» — смерть)
//       на настоящих акторах мира со случайными наборами тегов: набор уничтоженных совпадает;
//     • F5 — спавн блёсток лута `SpawnByBPName` против прежнего `LoadClass + SpawnActor` (класс, владелец, точка);
//     • F12 — таблица `EtalonAttach` против прежнего `switch` канала состояний (коды −3…20 × класс размера −1…3 × подъём);
//       `SpawnOnCarrier` против прежней последовательности «SpawnByJebName → [ConfigureBoneAttach] → SetLifeSpan(Natural)»
//       на эффектах всех мест вызова и выборке BP_Fx_* (`[случаев]` штук) во ВСЕХ режимах привязки: класс, владелец,
//       точка, поворот, `InitialLifeSpan` (срок жизни) и все поля привязки — бит в бит;
//     • F15 — `AsdaFxLight::EmitterWeight` против прежней копии формулы: все эмиттеры CDO выборки + синтетика
//       (в т.ч. отрицательные каналы и альфа) — double бит в бит;
//     • F17 — чтение уровней теней/эффектов базой против прежних копий меню (уровни −1…7, профиль возвращается;
//       тела меню недоступны снаружи — статик файла и закрытый член, поэтому новое тело повторено дословно);
//     • F18 — фикстура 4013 против двух прежних копий тела (стенды эффектов и рыбалки) — байты;
//     • F4 — точка наблюдателя `AsdaView` против прежней камеры контроллера в `asda.Fx.ShotStand` (в игре — бит в бит).
//   Входы случайных групп — детерминированный xorshift64 (зерно постоянное).
//
// Консоль: `asda.Dedup.W1CSelfTest [случаев=300] [quit]`. Строки — «САМОТЕСТ-В1В ok|FAIL …», итог — «САМОТЕСТ-В1В ИТОГ: ok=… fail=…».
//   Только офлайн-отладка (`-AsdaNoNet`); пакеты не шлются. Побочные следы: заспавненные акторы уничтожаются в конце
//   каждой группы; уровни качества профиля возвращаются как были (профиль НЕ сохраняется).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaEffectActor.h"
#include "AsdaFxNames.h"
#include "AsdaOutgoingGate.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaSelfTestBatch.h"
#include "AsdaTestPackets.h"
#include "AsdaView.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Resources/AsdaAssetPaths.h"
#include "Resources/AsdaMaterialNames.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaRenderQualitySubsystemBase.h"
#include "World/AsdaWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDedupW1C, Log, All);

namespace AsdaDedupW1C
{
	// Детерминированный генератор (то же устройство, что у самотестов В1-A/В1-B).
	struct FRng
	{
		uint64 S;
		explicit FRng(uint64 Seed) : S(Seed ? Seed : 0x9E3779B97F4A7C15ull) {}
		uint32 Next() { S ^= S << 13; S ^= S >> 7; S ^= S << 17; return (uint32)(S >> 32); }
		int32 Range(int32 N) { return N > 0 ? (int32)(Next() % (uint32)N) : 0; }
		double Real(double Lo, double Hi) { return Lo + (Hi - Lo) * ((double)Next() / 4294967295.0); }
	};

	// Учёт групп: группа ok, если все её сравнения совпали; первая разница печатается.
	struct FTally
	{
		int32 Ok = 0, Fail = 0;
		void Group(const TCHAR* What, int32 N, int32 Bad, const FString& FirstBad, const FString& Extra = FString())
		{
			if (Bad == 0 && N > 0)
			{
				++Ok;
				UE_LOG(LogAsdaDedupW1C, Display, TEXT("САМОТЕСТ-В1В ok %s: совпало %d из %d%s"), What, N, N, *Extra);
			}
			else
			{
				++Fail;
				UE_LOG(LogAsdaDedupW1C, Error, TEXT("САМОТЕСТ-В1В FAIL %s: расхождений %d из %d%s%s%s"), What, Bad, N,
					FirstBad.IsEmpty() ? TEXT("") : TEXT("; первое: "), *FirstBad, *Extra);
			}
		}
	};

	// Счётчик сравнений одной группы.
	struct FCmp
	{
		int32 N = 0, Bad = 0;
		FString First;
		void Check(bool bSame, const FString& What)
		{
			++N;
			if (!bSame) { ++Bad; if (First.IsEmpty()) { First = What; } }
		}
	};

	// Строки равны побайтно (с учётом регистра).
	bool SameStr(const TCHAR* A, const TCHAR* B) { return FCString::Strcmp(A, B) == 0; }

	// ── Прежние копии (дословно из бэкапа) ─────────────────────────────────────────────────────────────────────

	// Прежний `DestroyCharFxByTag` (AsdaWorldSubsystem.cpp): один тег.
	void OldDestroyCharFxByTag(AActor* Owner, FName Tag)
	{
		if (!Owner) { return; }
		TArray<AActor*> Attached;
		Owner->GetAttachedActors(Attached);
		for (AActor* A : Attached)
		{
			if (A && A->ActorHasTag(Tag)) { A->Destroy(); }
		}
	}

	// Прежнее гашение свечения на смерти (AsdaPlayerCharacter.cpp, case Dead): «оружие ИЛИ щит» одним проходом.
	void OldDeadGlowOff(AActor* Self)
	{
		TArray<AActor*> Attached;
		Self->GetAttachedActors(Attached);
		for (AActor* A : Attached)
		{
			if (A && (A->ActorHasTag(FName(TEXT("WeaponGlowFx"))) || A->ActorHasTag(FName(TEXT("ShieldGlowFx")))))
			{
				A->Destroy();
			}
		}
	}

	// Прежний `switch` канала состояний (AsdaEffectsSubsystem.cpp). Возврат: известен ли код (default — нет).
	bool OldAttachSwitch(int32 Attach, int32 SizeClass, float BuffFxTrackRiseZ, EAsdaFxAttachMode& Mode, float& RiseZ)
	{
		Mode = EAsdaFxAttachMode::CharPos;
		RiseZ = 0.f;
		bool bKnown = true;
		switch (Attach)
		{
		case 5:  Mode = EAsdaFxAttachMode::CharPos;   RiseZ = BuffFxTrackRiseZ; break;  // +15 по игровой Y
		case 7:  Mode = EAsdaFxAttachMode::BonePos;   RiseZ = BuffFxTrackRiseZ; break;  // кость + 15
		case 6:
		case 8:  Mode = EAsdaFxAttachMode::CharWorld; break;                             // полная матрица носителя
		case 3:
		case 9:  Mode = EAsdaFxAttachMode::CharPos;   break;                             // позиция сущности
		case 12: Mode = EAsdaFxAttachMode::CharPos;                                      // +25/45/60 по классу
		         RiseZ = (SizeClass == 0) ? 25.f : (SizeClass == 1 ? 45.f : 60.f); break;
		case 13: Mode = EAsdaFxAttachMode::CharPos;                                      // НАД сущностью
		         RiseZ = (SizeClass == 0) ? 200.f : (SizeClass == 1 ? 400.f : 600.f); break;
		default: bKnown = false; break;
		}
		return bKnown;
	}

	// Прежняя последовательность одноразового эффекта на носителе (все копии F12): спавн → [привязка] → срок.
	AAsdaEffectActor* OldOneShot(UWorld* W, const FString& Jeb, const FVector& At, const FRotator& Rot, AActor* Carrier,
		bool bAttach, USkeletalMeshComponent* Skel, FName Bone, EAsdaFxAttachMode Mode, const FVector& Offset)
	{
		AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnByJebName(W, Jeb, At, Rot, Carrier);
		if (!Fx) { return nullptr; }
		if (bAttach) { Fx->ConfigureBoneAttach(Carrier, Skel, Bone, Mode, Offset); }
		Fx->SetLifeSpan(Fx->NaturalLifeSpan());
		return Fx;
	}

	// Прежняя формула веса эмиттера (AsdaFxLightComponent.cpp PickDonorEmitter ≡ AsdaFxLightSubsystem.cpp кошелёк).
	double OldEmitterWeight(const FAsdaFxEmitter& E)
	{
		double W = 0.0;
		for (const FAsdaFxNode& N : E.Nodes)
		{
			const double Lum = 0.2126 * N.Color.R + 0.7152 * N.Color.G + 0.0722 * N.Color.B;
			W = FMath::Max(W, Lum * FMath::Max(N.Alpha, 0.f) * FMath::Max(N.SizeX, N.SizeY));
		}
		return W;
	}

	// Прежнее тело 4013 стенда эффектов (AsdaFxStatusSelfTest.cpp).
	TArray<uint8> OldBody4013Fx(int32 Rid, int32 Type, const FIntPoint& Tile)
	{
		TArray<uint8> B; B.SetNumZeroed(174);
		AsdaBytes::WrI16(&B[0], Rid);
		AsdaBytes::WrI16(&B[2], Type);
		B[8] = 1;
		AsdaBytes::WrI32(&B[9], 100);
		AsdaBytes::WrI16(&B[13], 10);
		AsdaBytes::WrI16(&B[19], Tile.X); AsdaBytes::WrI16(&B[21], Tile.Y);
		AsdaBytes::WrI16(&B[23], Tile.X); AsdaBytes::WrI16(&B[25], Tile.Y);
		AsdaBytes::WrI32(&B[27], 1);
		return B;
	}

	// Прежнее тело 4013 стенда рыбалки (AsdaFishZoneSelfTest.cpp, шаг DM).
	TArray<uint8> OldBody4013Fish(int32 kDcMobRid, int32 MobId, int32 Tx, int32 Tz)
	{
		TArray<uint8> B; B.SetNumZeroed(174);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)kDcMobRid);
		AsdaBytes::WrI16(B.GetData() + 2, (int16)MobId);
		AsdaBytes::WrI32(B.GetData() + 9, 100);          // HP
		AsdaBytes::WrI16(B.GetData() + 13, 100);         // знаменатель скорости (1.0)
		AsdaBytes::WrI16(B.GetData() + 19, (int16)Tx); AsdaBytes::WrI16(B.GetData() + 21, (int16)Tz);   // cur
		AsdaBytes::WrI16(B.GetData() + 23, (int16)Tx); AsdaBytes::WrI16(B.GetData() + 25, (int16)Tz);   // dst
		AsdaBytes::WrI32(B.GetData() + 27, 1);           // гейт `*(u32)(pkt+27)` Recv_MobSpawn
		return B;
	}

	// ── Вспомогательное ────────────────────────────────────────────────────────────────────────────────────────

	// Подпись эффекта: всё, что задают спавн, привязка и срок (числа — %.17g, т.е. бит в бит для float/double).
	FString FxSig(const AAsdaEffectActor* E)
	{
		if (!E) { return TEXT("нет"); }
		const FVector L = E->GetActorLocation();
		const FRotator R = E->GetActorRotation();
		const FVector O = E->FollowOffset;
		FString AttachM;
		for (int32 i = 0; i < 16; ++i) { AttachM += FString::Printf(TEXT("%.17g,"), E->FollowBoneAttach.M[i / 4][i % 4]); }
		return FString::Printf(
			TEXT("кл=%s влад=%s L=(%.17g,%.17g,%.17g) R=(%.17g,%.17g,%.17g) срок=%.17g след=%s кост=%s комп=%s скел=%s ")
			TEXT("O=(%.17g,%.17g,%.17g) поз=%d пов=%d рысь=%d пол=%d мир=%d носпоз=%d квад=%d послепозы=%d авт=(%.17g,%.17g,%.17g) M=%s"),
			*GetNameSafe(E->GetClass()), *GetNameSafe(E->GetOwner()), L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll, E->InitialLifeSpan,
			*GetNameSafe(E->FollowActor.Get()), *E->FollowBone.ToString(), *GetNameSafe(E->FollowComponent.Get()),
			*GetNameSafe(E->FollowSkelComponent.Get()), O.X, O.Y, O.Z, E->bFollowPosition ? 1 : 0, E->bFollowRotation ? 1 : 0,
			E->bFollowActorYaw ? 1 : 0, E->bFollowBoneFull ? 1 : 0, E->bFollowCharWorld ? 1 : 0, E->bFollowCharPos ? 1 : 0,
			E->bStrictQuadAttach ? 1 : 0, E->bFollowTickAfterPose ? 1 : 0, E->AuthoredRot.Pitch, E->AuthoredRot.Yaw,
			E->AuthoredRot.Roll, *AttachM);
	}

	// Выборка эффект-BP: имена .jeb по файлам `Content/AsdaAssets/Effects/BP_Fx_*.uasset` (не зависит от того, успел ли
	//   реестр ассетов просканировать папку), равномерно `Want` штук.
	TArray<FString> SampleFxJebs(int32 Want)
	{
		TArray<FString> Out;
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(FPaths::ProjectContentDir() / TEXT("AsdaAssets/Effects/BP_Fx_*.uasset")), true, false);
		TArray<FString> Names;
		for (const FString& F : Files) { Names.Add(FPaths::GetBaseFilename(F)); }
		Names.Sort();
		if (Names.Num() == 0 || Want <= 0) { return Out; }
		const int32 Step = FMath::Max(1, Names.Num() / Want);
		for (int32 i = 0; i < Names.Num() && Out.Num() < Want; i += Step)
		{
			Out.Add(AsdaAssetPaths::JebFromFxClassName(Names[i] + TEXT("_C")));
		}
		return Out;
	}
}

static void AsdaDedupW1CRun(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaDedupW1C;
	int32 Cases = 300;
	bool bQuit = false;
	for (const FString& A : Args)
	{
		if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		else if (A.IsNumeric()) { Cases = FMath::Max(1, FCString::Atoi(*A)); }
	}
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
	if (AsdaSelfTestKit::RefuseStart(GI, TEXT("asda.Dedup.W1CSelfTest"), KitArgs)) { return; }
	FTally T;
	if (!W || !GI)
	{
		UE_LOG(LogAsdaDedupW1C, Error, TEXT("САМОТЕСТ-В1В FAIL нет мира/игры"));
		++T.Fail;
		UE_LOG(LogAsdaDedupW1C, Display, TEXT("САМОТЕСТ-В1В ИТОГ: ok=%d fail=%d"), T.Ok, T.Fail);
		if (bQuit) { AsdaSelfTestBatch::Quit(W); }
		return;
	}

	// ══ F6: теги эффектов персонажа ══
	{
		FCmp C;
		struct FPair { const TCHAR* Old; const TCHAR* New; };
		const FPair Tags[] = {
			{ TEXT("WeaponGlowFx"), AsdaCharFxTags::kWeaponGlow }, { TEXT("ShieldGlowFx"), AsdaCharFxTags::kShieldGlow },
			{ TEXT("WingFx"), AsdaCharFxTags::kWing }, { TEXT("SoulRingFx"), AsdaCharFxTags::kSoulRing },
			{ TEXT("EntryFx"), AsdaCharFxTags::kEntry }, { TEXT("TargetRingFx"), AsdaCharFxTags::kTargetRing } };
		for (const FPair& P : Tags)
		{
			const FName A(P.Old), B(P.New);
			C.Check(SameStr(P.Old, P.New) && A.IsEqual(B, ENameCase::CaseSensitive) && A.ToString().Equals(B.ToString(), ESearchCase::CaseSensitive),
				FString::Printf(TEXT("тег «%s» ≠ «%s»"), P.Old, P.New));
		}
		T.Group(TEXT("F6 теги эффектов персонажа (строка, FName с регистром)"), C.N, C.Bad, C.First);
	}

	// ══ F5/F10/F16: пути и имена против прежних литералов + те же загруженные объекты ══
	{
		FCmp C;
		struct FPath { const TCHAR* Old; FString New; bool bClass; };
		const FPath Paths[] = {
			{ TEXT("/Game/AsdaAssets/Effects/BP_LootDropFx.BP_LootDropFx_C"), AsdaAssetPaths::FxClassPathFromBp(AsdaFxBp::kLootDrop), true },
			{ TEXT("/Game/AsdaAssets/Effects/BP_ClickMarker.BP_ClickMarker_C"), AsdaAssetPaths::FxClassPathFromBp(AsdaFxBp::kClickMarker), true },
			{ TEXT("/Game/AsdaAssets/Effects/BP_Fx_test_000.BP_Fx_test_000_C"), AsdaAssetPaths::FxClassPathFromBp(AsdaFxBp::kLagTest), true },
			{ TEXT("/Game/AsdaAssets/Effects/BP_Fx_eff_0_char_03_flyarr01_a_00.BP_Fx_eff_0_char_03_flyarr01_a_00_C"),
			  AsdaAssetPaths::FxClassPathFromJeb(TEXT("eff_0_char_03_flyarr01-a_00")), true },
			{ TEXT("/Game/Materials/M_AsdaPreviewFade.M_AsdaPreviewFade"), AsdaMat::kPreviewFadePath, false },
			{ TEXT("/Game/Materials/M_AsdaCharLitSoul.M_AsdaCharLitSoul"), AsdaMat::kCharLitSoulPath, false },
			{ TEXT("/Game/Materials/MPC_AsdaNormals.MPC_AsdaNormals"), AsdaMat::kNormalsMpcPath, false },
			{ TEXT("/Game/AsdaAssets/UI/Dev/M_LoadingBG.M_LoadingBG"), AsdaMat::kLoadingBgPath, false },
			{ TEXT("/Game/AsdaAssets/Textures/Maps/load/Start_logo.Start_logo"), AsdaLoadScreen::kStartLogoTex, false } };
		int32 nLoaded = 0;
		for (const FPath& P : Paths)
		{
			C.Check(SameStr(P.Old, *P.New), FString::Printf(TEXT("путь «%s» ≠ «%s»"), P.Old, *P.New));
			const UObject* A = P.bClass ? (UObject*)LoadClass<AActor>(nullptr, P.Old) : LoadObject<UObject>(nullptr, P.Old);
			const UObject* B = P.bClass ? (UObject*)LoadClass<AActor>(nullptr, *P.New) : LoadObject<UObject>(nullptr, *P.New);
			C.Check(A == B && A != nullptr, FString::Printf(TEXT("объект «%s»: прежний %s, новый %s"), P.Old, *GetNameSafe(A), *GetNameSafe(B)));
			if (A) { ++nLoaded; }
		}
		struct FPair { const TCHAR* Old; const TCHAR* New; };
		const FPair Names[] = {
			{ TEXT("M_AsdaPreviewFade"), AsdaMat::kPreviewFadeName }, { TEXT("M_AsdaCharLitSoul"), AsdaMat::kCharLitSoulName },
			{ TEXT("Tex"), AsdaMat::kParamTex }, { TEXT("FadeIn"), AsdaMat::kParamFadeIn },
			{ TEXT("SoulOpacity"), AsdaMat::kParamSoulOpacity }, { TEXT("EmissiveBoost"), AsdaMat::kParamEmissiveBoost } };
		for (const FPair& P : Names)
		{
			const FName A(P.Old), B(P.New);
			C.Check(SameStr(P.Old, P.New) && A.IsEqual(B, ENameCase::CaseSensitive), FString::Printf(TEXT("имя «%s» ≠ «%s»"), P.Old, P.New));
		}
		// Имя мастера, с которым сравнивает фейд/«душа», совпадает с именем загруженного объекта.
		const UObject* Fade = LoadObject<UObject>(nullptr, AsdaMat::kPreviewFadePath);
		const UObject* Soul = LoadObject<UObject>(nullptr, AsdaMat::kCharLitSoulPath);
		C.Check(Fade && Fade->GetName() == AsdaMat::kPreviewFadeName, TEXT("имя загруженного мастера фейда ≠ kPreviewFadeName"));
		C.Check(Soul && Soul->GetName() == AsdaMat::kCharLitSoulName, TEXT("имя загруженного мастера «души» ≠ kCharLitSoulName"));
		T.Group(TEXT("F5/F10/F16 пути, имена мастеров и параметров против прежних литералов"), C.N, C.Bad, C.First,
			FString::Printf(TEXT(" (загружено объектов %d из 9)"), nLoaded));
	}

	// Носитель для групп со спавном: свой персонаж мира (иначе — временный персонаж, как у `asda.FxFollowLagTest`).
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	AAsdaPlayerCharacter* Carrier = WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	AAsdaPlayerCharacter* TempCarrier = nullptr;
	if (!Carrier)
	{
		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TempCarrier = W->SpawnActor<AAsdaPlayerCharacter>(AAsdaPlayerCharacter::StaticClass(), FVector(0, 0, 500), FRotator::ZeroRotator, SP);
		Carrier = TempCarrier;
	}
	FRng R(0xC0FFEE1234ull);

	// ══ F6: гашение по тегу против двух прежних копий ══
	if (Carrier)
	{
		FCmp C;
		const FName TagPool[] = { FName(TEXT("WeaponGlowFx")), FName(TEXT("ShieldGlowFx")), FName(TEXT("WingFx")), FName(TEXT("Other")) };
		const int32 Trials = FMath::Clamp(Cases, 10, 200);
		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (int32 t = 0; t < Trials; ++t)
		{
			const int32 Kind = t % 3;   // 0 — один тег (мир), 1 — «оружие ИЛИ щит» (смерть), 2 — один тег NAME_None
			const int32 K = 1 + R.Range(6);
			TArray<TArray<int32>> TagSets;
			for (int32 k = 0; k < K; ++k)
			{
				TArray<int32> S;
				for (int32 j = 0; j < 4; ++j) { if (R.Range(3) == 0) { S.Add(j); } }
				TagSets.Add(S);
			}
			const FName Pick = (Kind == 2) ? NAME_None : TagPool[R.Range(4)];
			AActor* Hosts[2] = { nullptr, nullptr };
			TArray<AActor*> Kids[2];
			for (int32 h = 0; h < 2; ++h)
			{
				Hosts[h] = W->SpawnActor<AActor>(AActor::StaticClass(), FVector(0, 0, 1000), FRotator::ZeroRotator, SP);
				if (!Hosts[h]) { continue; }
				USceneComponent* Root = NewObject<USceneComponent>(Hosts[h], TEXT("W1CRoot"));
				Hosts[h]->SetRootComponent(Root); Root->RegisterComponent();
				for (int32 k = 0; k < K; ++k)
				{
					AActor* Kid = W->SpawnActor<AActor>(AActor::StaticClass(), FVector(0, 0, 1000), FRotator::ZeroRotator, SP);
					if (!Kid) { continue; }
					USceneComponent* KR = NewObject<USceneComponent>(Kid, TEXT("W1CKidRoot"));
					Kid->SetRootComponent(KR); KR->RegisterComponent();
					for (int32 j : TagSets[k]) { Kid->Tags.Add(TagPool[j]); }
					Kid->AttachToActor(Hosts[h], FAttachmentTransformRules::KeepWorldTransform);
					Kids[h].Add(Kid);
				}
			}
			if (!Hosts[0] || !Hosts[1] || Kids[0].Num() != Kids[1].Num()) { C.Check(false, TEXT("стенд не заспавнился")); continue; }
			if (Kind == 1) { OldDeadGlowOff(Hosts[0]); AsdaCharFxTags::DestroyAttached(Hosts[1], FName(AsdaCharFxTags::kWeaponGlow), FName(AsdaCharFxTags::kShieldGlow)); }
			else           { OldDestroyCharFxByTag(Hosts[0], Pick); AsdaCharFxTags::DestroyAttached(Hosts[1], Pick); }
			FString PA, PB;
			for (int32 k = 0; k < Kids[0].Num(); ++k)
			{
				PA += (!IsValid(Kids[0][k]) || Kids[0][k]->IsActorBeingDestroyed()) ? TEXT("x") : TEXT("o");
				PB += (!IsValid(Kids[1][k]) || Kids[1][k]->IsActorBeingDestroyed()) ? TEXT("x") : TEXT("o");
			}
			C.Check(PA == PB, FString::Printf(TEXT("вид %d тег %s: прежний «%s», новый «%s»"), Kind, *Pick.ToString(), *PA, *PB));
			for (int32 h = 0; h < 2; ++h)
			{
				for (AActor* Kid : Kids[h]) { if (IsValid(Kid)) { Kid->Destroy(); } }
				Hosts[h]->Destroy();
			}
		}
		T.Group(TEXT("F6 гашение по тегу (мир: один тег; смерть: оружие ИЛИ щит) на акторах мира"), C.N, C.Bad, C.First);
	}

	// ══ F5: спавн блёсток лута ══
	if (Carrier)
	{
		FCmp C;
		for (int32 i = 0; i < 20; ++i)
		{
			const FVector At(R.Real(-5000, 5000), R.Real(-5000, 5000), R.Real(0, 2000));
			AActor* Old = nullptr;
			if (UClass* FxCls = LoadClass<AActor>(nullptr, TEXT("/Game/AsdaAssets/Effects/BP_LootDropFx.BP_LootDropFx_C")))
			{
				FActorSpawnParameters SP;
				SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				SP.Owner = Carrier;
				Old = W->SpawnActor<AActor>(FxCls, At, FRotator::ZeroRotator, SP);
			}
			AAsdaEffectActor* New = AAsdaEffectActor::SpawnByBPName(W, AsdaFxBp::kLootDrop, At, FRotator::ZeroRotator, Carrier);
			const FString SA = FxSig(Cast<AAsdaEffectActor>(Old)), SB = FxSig(New);
			C.Check(Old && New && SA == SB, FString::Printf(TEXT("прежний %s | новый %s"), *SA, *SB));
			if (Old) { Old->Destroy(); }
			if (New) { New->Destroy(); }
		}
		T.Group(TEXT("F5 спавн блёсток лута SpawnByBPName против LoadClass+SpawnActor"), C.N, C.Bad, C.First);
	}

	// ══ F12: таблица режимов привязки ══
	{
		FCmp C;
		const float Rises[] = { 15.f, AAsdaEffectActor::kEtalonTrackRiseZ, 7.25f, 0.f, -3.5f };
		for (int32 Code = -3; Code <= 20; ++Code)
		{
			for (int32 Sc = -1; Sc <= 3; ++Sc)
			{
				for (float Rise : Rises)
				{
					EAsdaFxAttachMode MA, MB; float RA = -1.f, RB = -2.f;
					const bool KA = OldAttachSwitch(Code, Sc, Rise, MA, RA);
					const bool KB = AAsdaEffectActor::EtalonAttach(Code, Sc, Rise, MB, RB);
					C.Check(KA == KB && MA == MB && FMemory::Memcmp(&RA, &RB, sizeof(float)) == 0,
						FString::Printf(TEXT("код %d класс %d подъём %g: прежний (%d, %d, %.9g), новый (%d, %d, %.9g)"),
							Code, Sc, Rise, KA ? 1 : 0, (int32)MA, RA, KB ? 1 : 0, (int32)MB, RB));
				}
			}
		}
		C.Check(AAsdaEffectActor::kEtalonTrackRiseZ == 15.f, TEXT("kEtalonTrackRiseZ ≠ 15"));
		if (const UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>())
		{
			C.Check(Fx->BuffFxTrackRiseZ == 15.f, FString::Printf(TEXT("BuffFxTrackRiseZ = %.9g ≠ 15"), Fx->BuffFxTrackRiseZ));
		}
		T.Group(TEXT("F12 таблица режимов привязки эталона против прежнего switch"), C.N, C.Bad, C.First);
	}

	// ══ F12 + F15: одноразовый эффект на носителе и вес эмиттера на настоящих BP ══
	TArray<FString> Jebs = {
		TEXT("itm1_n_00_vehicle_0000_e"), TEXT("Eff_0_char_99_c-shovel-a_00"), TEXT("Eff_0_char_99_c-suc-a_00"),
		TEXT("Eff_0_char_99_c-fail-a_00"), TEXT("fish_end_01"), TEXT("fish_end_02"), TEXT("eff_lucky_a_00"), TEXT("eff_lucky_a_01"),
		TEXT("eff_0_char_99_cmchar01-a_00"), TEXT("eff_1_char_99_soullink03-a_00") };
	Jebs.Append(SampleFxJebs(Cases));
	if (Carrier)
	{
		FCmp C, CW;
		int32 nSpawned = 0, nEmit = 0;
		USkeletalMeshComponent* Skel = Carrier->BaseSkel;
		const FName Bones[] = { NAME_None, FName(TEXT("M_DUMMY")), FName(TEXT("8_a1")), FName(TEXT("M")) };
		const EAsdaFxAttachMode Modes[] = { EAsdaFxAttachMode::CharWorld, EAsdaFxAttachMode::CharPos, EAsdaFxAttachMode::BonePos,
			EAsdaFxAttachMode::BoneFull, EAsdaFxAttachMode::Wing, EAsdaFxAttachMode::WeaponGlow, EAsdaFxAttachMode::OwnerBoundFx };
		for (int32 i = 0; i < Jebs.Num(); ++i)
		{
			const FString& Jeb = Jebs[i];
			// 0 — без привязки, нулевой поворот; 1 — без привязки, свой поворот (хранитель); 2.. — режимы привязки.
			constexpr int32 NumModes = (int32)UE_ARRAY_COUNT(Modes);   // режимов привязки в переборе
			const int32 Variant = (i < 10) ? -1 : (i % (2 + NumModes));
			const int32 First = (Variant < 0) ? 0 : Variant;
			const int32 Last = (Variant < 0) ? (1 + NumModes) : Variant;
			for (int32 v = First; v <= Last; ++v)
			{
				const FVector At = Carrier->GetActorLocation() + FVector(R.Real(-200, 200), R.Real(-200, 200), R.Real(0, 200));
				const FVector Off(0.f, 0.f, (float)R.Real(-50, 200));
				const FName Bone = Bones[R.Range(4)];
				AAsdaEffectActor* A = nullptr; AAsdaEffectActor* B = nullptr;
				if (v <= 1)
				{
					const FRotator Rot = (v == 0) ? FRotator::ZeroRotator : FRotator(R.Real(-80, 80), R.Real(-180, 180), 0.0);
					A = OldOneShot(W, Jeb, At, Rot, Carrier, false, nullptr, NAME_None, EAsdaFxAttachMode::CharPos, FVector::ZeroVector);
					B = AAsdaEffectActor::SpawnOnCarrier(W, Jeb, At, Carrier, Rot);
				}
				else
				{
					const EAsdaFxAttachMode M = Modes[v - 2];
					A = OldOneShot(W, Jeb, At, FRotator::ZeroRotator, Carrier, true, Skel, Bone, M, Off);
					B = AAsdaEffectActor::SpawnOnCarrier(W, Jeb, At, Carrier, Skel, Bone, M, Off);
				}
				const FString SA = FxSig(A), SB = FxSig(B);
				C.Check((A == nullptr) == (B == nullptr) && SA == SB,
					FString::Printf(TEXT("%s вариант %d: прежний %s | новый %s"), *Jeb, v, *SA, *SB));
				if (A) { ++nSpawned; }
				// F15 — вес каждого эмиттера настоящего эффекта.
				if (A && v == First)
				{
					for (const FAsdaFxEmitter& E : A->Emitters)
					{
						const double WA = OldEmitterWeight(E), WB = AsdaFxLight::EmitterWeight(E);
						CW.Check(FMemory::Memcmp(&WA, &WB, sizeof(double)) == 0,
							FString::Printf(TEXT("%s эмиттер %s: прежний %.17g, новый %.17g"), *Jeb, *E.Name, WA, WB));
						++nEmit;
					}
				}
				if (A) { A->Destroy(); }
				if (B) { B->Destroy(); }
			}
		}
		T.Group(TEXT("F12 SpawnOnCarrier против прежней последовательности (класс, владелец, точка, поворот, срок, привязка)"), C.N, C.Bad, C.First,
			FString::Printf(TEXT(" (эффектов %d, заспавнено прежним путём %d)"), Jebs.Num(), nSpawned));
		// Синтетика: отрицательные каналы, отрицательная альфа, нулевые и большие размеры, пустой список узлов.
		for (int32 i = 0; i < FMath::Max(Cases, 2000); ++i)
		{
			FAsdaFxEmitter E;
			const int32 NN = R.Range(6);
			for (int32 k = 0; k < NN; ++k)
			{
				FAsdaFxNode N;
				N.Color = FLinearColor((float)R.Real(-1, 3), (float)R.Real(-1, 3), (float)R.Real(-1, 3), 1.f);
				N.Alpha = (float)R.Real(-1, 2);
				N.SizeX = (float)R.Real(-10, 500); N.SizeY = (float)R.Real(-10, 500);
				E.Nodes.Add(N);
			}
			const double WA = OldEmitterWeight(E), WB = AsdaFxLight::EmitterWeight(E);
			CW.Check(FMemory::Memcmp(&WA, &WB, sizeof(double)) == 0, FString::Printf(TEXT("синтетика #%d: прежний %.17g, новый %.17g"), i, WA, WB));
		}
		T.Group(TEXT("F15 вес светимости эмиттера против прежней формулы (BP + синтетика)"), CW.N, CW.Bad, CW.First,
			FString::Printf(TEXT(" (эмиттеров настоящих эффектов %d)"), nEmit));
	}
	else
	{
		T.Group(TEXT("F12/F15 носителя нет"), 0, 1, TEXT("персонаж не заспавнился"));
	}
	if (TempCarrier) { TempCarrier->Destroy(); }

	// ══ F17: чтение уровней качества ══
	if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		FCmp C;
		const int32 Shadow0 = S->GetShadowQuality(), Fx0 = S->GetVisualEffectQuality();   // вернуть после проверки
		for (int32 L = -1; L <= 7; ++L)
		{
			S->SetShadowQuality(L);
			S->SetVisualEffectQuality(L);
			// Прежние копии меню: тени (ShadowDistancePresetBase) и уровень рельефа (GetAsdaQualityLevel).
			const int32 OldShadow = S ? S->GetShadowQuality() : 3;
			const int32 OldFx = S ? FMath::Clamp(S->GetVisualEffectQuality(), 0, 4) : 3;
			// Новые тела меню (`ShadowDistancePresetBase` — статик файла, `GetAsdaQualityLevel` — закрытый член): чтение базой
			//   + зажим меню, дословно как в правленом `AsdaDevMenuSubsystem.cpp`.
			const int32 NewShadow = UAsdaRenderQualitySubsystemBase::ShadowQualityLevel();
			const int32 NewFx = FMath::Clamp(UAsdaRenderQualitySubsystemBase::EffectsQualityLevel(), 0, 4);
			C.Check(OldShadow == NewShadow && OldFx == NewFx,
				FString::Printf(TEXT("уровень %d: тени %d/%d, эффекты %d/%d"), L, OldShadow, NewShadow, OldFx, NewFx));
		}
		S->SetShadowQuality(Shadow0);
		S->SetVisualEffectQuality(Fx0);
		C.Check(S->GetShadowQuality() == Shadow0 && S->GetVisualEffectQuality() == Fx0, TEXT("профиль не вернулся"));
		T.Group(TEXT("F17 чтение уровней теней/эффектов (база против копий меню, зажим меню сохранён)"), C.N, C.Bad, C.First);
	}

	// ══ F18: фикстура 4013 ══
	{
		FCmp C;
		for (int32 i = 0; i < FMath::Max(Cases, 2000); ++i)
		{
			const int32 Rid = (int32)R.Range(140000) - 70000, Type = (int32)R.Range(140000) - 70000;
			const FIntPoint Tile((int32)R.Range(140000) - 70000, (int32)R.Range(140000) - 70000);
			C.Check(OldBody4013Fx(Rid, Type, Tile) == AsdaTestPackets::MobSpawn4013(Rid, Type, Tile, 1, 10),
				FString::Printf(TEXT("стенд эффектов rid %d тип %d тайл (%d,%d)"), Rid, Type, Tile.X, Tile.Y));
			C.Check(OldBody4013Fish(Rid, Type, Tile.X, Tile.Y) == AsdaTestPackets::MobSpawn4013(Rid, Type, Tile, 0, 100),
				FString::Printf(TEXT("стенд рыбалки rid %d тип %d тайл (%d,%d)"), Rid, Type, Tile.X, Tile.Y));
		}
		T.Group(TEXT("F18 тело 4013: фикстура против двух прежних копий (байты)"), C.N, C.Bad, C.First);
	}

	// ══ F4: камера стенда снимков ══
	{
		FCmp C;
		APlayerController* PC = W->GetFirstPlayerController();
		const bool bOld = PC && PC->PlayerCameraManager;
		FAsdaViewPoint VP;
		const bool bNew = AsdaView::GetViewPoint(W, VP) && VP.bHasRotation;
		FString D = FString::Printf(TEXT("камера прежняя %d, новая %d (источник %s)"), bOld ? 1 : 0, bNew ? 1 : 0, AsdaView::SourceName(VP.Source));
		bool bSame = (bOld == bNew);
		if (bOld && bNew)
		{
			const FVector L = PC->PlayerCameraManager->GetCameraLocation();
			const FRotator Rr = PC->PlayerCameraManager->GetCameraRotation();
			bSame = FMemory::Memcmp(&L, &VP.Location, sizeof(FVector)) == 0 && FMemory::Memcmp(&Rr, &VP.Rotation, sizeof(FRotator)) == 0;
			D += FString::Printf(TEXT(" L %s/%s R %s/%s"), *L.ToString(), *VP.Location.ToString(), *Rr.ToString(), *VP.Rotation.ToString());
		}
		C.Check(bSame, D);
		T.Group(TEXT("F4 точка камеры asda.Fx.ShotStand: AsdaView против камеры контроллера"), C.N, C.Bad, C.First, TEXT(" (") + D + TEXT(")"));
	}

	UE_LOG(LogAsdaDedupW1C, Display, TEXT("САМОТЕСТ-В1В ИТОГ: ok=%d fail=%d"), T.Ok, T.Fail);
	if (bQuit) { AsdaSelfTestBatch::Quit(W); }
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaDedupW1CSelfTest(
	TEXT("asda.Dedup.W1CSelfTest"),
	TEXT("Самотест В1 (часть C) дедупа №2: вынесенные точки эффектов/рендера (F4, F5, F6, F10, F12, F15, F16, F17, F18) против прежних копий. ")
	TEXT("asda.Dedup.W1CSelfTest [случаев=300] [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		AsdaDedupW1CRun(Args, W);
	}));

#endif // !UE_BUILD_SHIPPING
