// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ДАЛЬНИЕ АТАКИ МОБОВ: ЗАМАХ, СНАРЯД, ЭФФЕКТЫ» (05.11.2026, mob-fx-audit). [ПОРТ]
//   `asda.SelfTest.MobRangedFx [cases=<тип>:<навык>,…] [shot=<папка>] [quit]`
//   Жалобы игроков 24.10/27.10 (Player52 «Kaiya ranged attacks», Player57 «Kaiya hit animation»), координатор 05.11: у Кайи (обе
//   версии), Ладьи и Слона дальние атаки без замаха, снаряда и анимации. Дальняя атака моба в оригинале — только НАВЫК моба
//   (8012 → автомат 15/16/17, клип слота 18–21, снаряд .jeb фазы 3, удар фазы 4; разбор Reports/mob_skills/FLOW.md);
//   обычный удар моба (состояние 3, `Combat_ApplyDamage@0xF7C470`, combat_pvp.c:7620) снаряда не имеет — только искры
//   `eff_0_mon_99_battk01-a_00` у цели. Навыки по типам — серверный MonsterSkillCondition.mst (запись = MobId − 1):
//   Кайя 286 — 1..6, Истеричная Кайя 287 — 2,3,5,7,9,11,12, Слон 303/305 — 2,29..33, Ладья 319/321 — 37..40.
//   Путь игрока, офлайн (`-AsdaNoNet`): свой персонаж, моб — 4013, атака — штатный приём 8012 (`DebugFeedPacket` = сокет),
//   обычный удар — 4015 kind 3. Проверяется ФАКТ на сцене, а не журнал автомата:
//     A) у меша моба играл клип навыка (`…anim_11..14` по правилу слота) или атаки;
//     B) в мире появился актор снаряда (`AAsdaProjectileActor`) — если у навыка есть .jeb фазы 3;
//     C) появились акторы эффектов фаз 2/3/4 навыка (классы `BP_Fx_<jeb>_C`);
//     D) кадры `<папка>/ranged_<тип>_<навык>_{wind|flight}.png` (замах ~0.35 с, полёт — A + 0.3 с).
//   Итог: «ДАЛЬНИЕ АТАКИ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaTestPackets.h"
#include "AsdaEffectActor.h"
#include "Test/AsdaSelfTestKit.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaProjectileActor.h"
#include "World/AsdaCoord.h"
#include "World/AsdaMobSkillSubsystem.h"
#include "Npc/AsdaMobActor.h"
#include "Resources/AsdaAssetPaths.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMobRangedTest, Log, All);

namespace AsdaMobRangedTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.MobRangedFx");   // имя команды
	constexpr int32 kSelfFallback = 4322;                            // свой userNum, если не известен
	constexpr int32 kSpdDenom = 250;                                 // знаменатель скорости 4013/4015
	constexpr int32 kRidBase = 31151;                                // rid мобов прогона (свои, не пересекаются с другими самотестами)

	// Случай: тип моба, навык (0 — обычный удар 4015 kind 3), .jeb фаз 2/3/4 (Monster_SkillEffect.BIN), пуск снаряда A (мс).
	// bCast — навык с фазой каста: сервер шлёт 8011 (каст), затем 8012 того же навыка (как живые журналы, FLOW.md §2).
	struct FCase { int32 Type; int32 Skill; const TCHAR* Jeb2; const TCHAR* Jeb3; const TCHAR* Jeb4; int32 A; bool bCast = false; };

	// Значения .jeb и A — из Tools/_out/MonsterSkillEffect.csv (записи 6, 12, 33, 38, 39, 40, 5).
	static const FCase kDefault[] = {
		{ 286,  6, TEXT(""),                         TEXT("Mon_KaiyaNormal_AR_00"),      TEXT("Mon_KaiyaNormal_BO_00"),    1000 },
		{ 286,  5, TEXT("Mon_5mAreaDam_AC_00"),      TEXT("Mon_5mAreaDam_CH_00"),        TEXT("Mon_KaiyaNormal_BO_00"),     500, true },
		{ 287, 12, TEXT(""),                         TEXT("Mon_HisKaiyaNormal_AR_00"),   TEXT("Mon_HisKaiyaNormal_BO_00"), 1000 },
		{ 286,  0, TEXT(""),                         TEXT(""),                           TEXT(""),                           0 },
		{ 303, 33, TEXT(""),                         TEXT("Mon_BishopNormal_AR_00"),     TEXT("Mon_BishopNormal_BO_00"),   1000 },
		{ 305, 33, TEXT(""),                         TEXT("Mon_BishopNormal_AR_00"),     TEXT("Mon_BishopNormal_BO_00"),   1000 },
		{ 319, 38, TEXT("Mon_RangeArrowDam_AC_00"),  TEXT("Mon_RangeArrowDam_AR_00"),    TEXT("Mon_RangeArrowDam_BO_00"),  2000 },
		{ 319, 39, TEXT("Mon_RangeArrowSpDn_AR_00"), TEXT("Mon_RangeArrowSpDn_BO_00"),   TEXT(""),                         2000 },
		{ 319, 40, TEXT(""),                         TEXT("Mon_RookNormal_AR_00"),       TEXT(""),                         2000 },
		{ 321, 40, TEXT(""),                         TEXT("Mon_RookNormal_AR_00"),       TEXT(""),                         2000 },
	};

	// Шаг сценария: true — шаг завершён. Аргумент — секунды от начала шага.
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
		TUniquePtr<FAsdaNetSelfScope> NetScope;       // свой номер — вернуть после прогона
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;       // мир
		TArray<FCase> Cases;                          // случаи прогона
		int32 Self = -1;                              // свой userNum
		FIntPoint Home = FIntPoint::ZeroValue;        // своя клетка
		float OrigArm = -1.f;                         // длина штанги камеры до кадров (−1 — не менялась)
		bool bWasInGame = false;                      // флаг «в игре» до прогона
		// Наблюдения текущего случая.
		TSet<FString> Clips;                          // имена клипов, игравших у меша моба
		TSet<FString> FxSeen;                         // классы акторов эффектов, появившихся после начала случая
		TSet<TWeakObjectPtr<AActor>> FxBefore;        // эффекты, жившие до начала случая
		int32 MaxProj = 0;                            // наибольшее число снарядов навыка в мире одновременно
		bool bMobAtMark = false;                      // моб был в мире к началу атаки

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaMobRangedTest, Log,   TEXT("ДАЛЬНИЕ АТАКИ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaMobRangedTest, Error, TEXT("ДАЛЬНИЕ АТАКИ FAIL %s"), *What); }
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* N = Net.Get()) { N->DebugFeedPacket(Op, B); } }
		AAsdaMobActor* Mob(int32 Rid) const { const UAsdaWorldSubsystem* X = WS.Get(); return X ? X->FindNetMobByRid(Rid) : nullptr; }
	};

	// Свой павн.
	static AAsdaPlayerCharacter* MeOf(const FState& S)
	{
		const UAsdaWorldSubsystem* X = S.WS.Get();
		return X ? Cast<AAsdaPlayerCharacter>(X->LocalPlayerActor.Get()) : nullptr;
	}

	// 8011: навык @0, вид цели @2 (сервер — 0), цель @3 (сервер — −1), renderId моба @5.
	static TArray<uint8> Body8011(int32 Skill, int32 Rid)
	{
		TArray<uint8> B; B.SetNumZeroed(7);
		AsdaBytes::WrI16(&B[0], Skill); AsdaBytes::WrI16(&B[3], -1); AsdaBytes::WrI16(&B[5], Rid);
		return B;
	}

	// 8012: шапка 9 б {err, навык, моб, усл, вид цели, цель} + 1 запись 29 б {вид 1, индекс, урон, флаги 1 (попал), …, −1 @19}.
	static TArray<uint8> Body8012(int32 Skill, int32 Rid, int32 Target, int32 Dmg)
	{
		TArray<uint8> B; B.SetNumZeroed(9 + 29);
		AsdaBytes::WrI16(&B[1], Skill); AsdaBytes::WrI16(&B[3], Rid); B[6] = 1; AsdaBytes::WrI16(&B[7], Target);
		uint8* R = B.GetData() + 9;
		R[0] = 1; AsdaBytes::WrI16(R + 1, Target); AsdaBytes::WrI32(R + 3, Dmg); R[7] = 0x01; AsdaBytes::WrI16(R + 19, -1);
		return B;
	}

	// 4015 kind 3 (обычный удар моба по жертве): @0 жертва, @2 тип, @4 rid, @6 = 3, клетки, скорость, темп, урон @21, HP @25.
	static TArray<uint8> Body4015Hit(int32 Victim, int32 Rid, int32 Type, const FIntPoint& Cur, int32 Dmg)
	{
		TArray<uint8> B; B.SetNumZeroed(37);
		AsdaBytes::WrI16(&B[0], Victim); AsdaBytes::WrI16(&B[2], Type); AsdaBytes::WrI16(&B[4], Rid); B[6] = 3;
		AsdaBytes::WrI16(&B[7], Cur.X); AsdaBytes::WrI16(&B[9], Cur.Y); AsdaBytes::WrI16(&B[11], Cur.X); AsdaBytes::WrI16(&B[13], Cur.Y);
		AsdaBytes::WrI16(&B[15], kSpdDenom); AsdaBytes::WrI16(&B[17], 10000); AsdaBytes::WrI16(&B[19], 10000);
		AsdaBytes::WrI32(&B[21], Dmg); AsdaBytes::WrI32(&B[25], 100); AsdaBytes::WrI16(&B[31], -1);
		return B;
	}

	// Наблюдение кадра: клип меша моба, новые эффекты, снаряды навыка.
	static void Sample(FState& S, int32 Rid)
	{
		UWorld* W = S.W.Get();
		if (!W) { return; }
		if (const AAsdaMobActor* M = S.Mob(Rid))
		{
			if (M->Mesh && M->Mesh->GetSingleNodeInstance() && M->Mesh->GetSingleNodeInstance()->GetAnimationAsset())
			{
				S.Clips.Add(M->Mesh->GetSingleNodeInstance()->GetAnimationAsset()->GetName());
			}
		}
		for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
		{
			if (!S.FxBefore.Contains(*It)) { S.FxSeen.Add(It->GetClass()->GetName()); }
		}
		int32 P = 0;
		for (TActorIterator<AAsdaProjectileActor> It(W); It; ++It) { if (IsValid(*It)) { ++P; } }
		S.MaxProj = FMath::Max(S.MaxProj, P);
	}

	// Начать наблюдение случая: запомнить живые эффекты, обнулить наблюдения.
	static void Mark(FState& S)
	{
		S.Clips.Reset(); S.FxSeen.Reset(); S.FxBefore.Reset(); S.MaxProj = 0;
		if (UWorld* W = S.W.Get()) { for (TActorIterator<AAsdaEffectActor> It(W); It; ++It) { S.FxBefore.Add(*It); } }
	}

	// Класс BP эффекта по .jeb (как AAsdaEffectActor::SpawnByJebName): `BP_Fx_<jeb, - → _>_C`, без учёта регистра.
	static bool SawFx(const FState& S, const TCHAR* Jeb)
	{
		const FString Want = AsdaAssetPaths::FxBpName(Jeb) + TEXT("_C");
		for (const FString& C : S.FxSeen) { if (C.Equals(Want, ESearchCase::IgnoreCase)) { return true; } }
		return false;
	}

	// Камера: свой персонаж между мобом и камерой не стоит — штанга длиннее, вид сбоку на линию «моб → я».
	static void Frame(FState& S, const FString& Name)
	{
		AAsdaPlayerCharacter* Me = MeOf(S);
		if (Me && Me->CameraArm)
		{
			if (S.OrigArm < 0.f) { S.OrigArm = Me->CameraArm->TargetArmLength; }
			Me->CameraArm->TargetArmLength = 1300.f;
			Me->CameraArm->bEnableCameraLag = false;
		}
		if (S.ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(S.ShotDir, Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaMobRangedTest, Log, TEXT("ДАЛЬНИЕ АТАКИ кадр %s"), *Path);
	}

	// Итоги случая.
	static void Verdict(FState& S, const FCase& C, int32 Rid)
	{
		FString Clips; for (const FString& N : S.Clips) { Clips += N + TEXT(" "); }
		FString Fx; for (const FString& N : S.FxSeen) { Fx += N + TEXT(" "); }
		const FString Who = FString::Printf(TEXT("тип %d %s"), C.Type, C.Skill ? *FString::Printf(TEXT("навык %d"), C.Skill) : TEXT("обычный удар"));
		// Наличие — на момент атаки: офлайн три неудачных 11004 дают сброс сессии «разрыв связи», и моб к концу случая уже убран.
		S.Check(S.bMobAtMark, Who + TEXT(": моб на месте к началу атаки"));
		if (C.Skill == 0)
		{
			bool bAtk = false;   // играл клип атаки (anim_04/05/06 — слоты удара 4/5/6) или крита
			for (const FString& N : S.Clips) { bAtk |= N.EndsWith(TEXT("anim_04")) || N.EndsWith(TEXT("anim_05")) || N.EndsWith(TEXT("anim_06")); }
			S.Check(bAtk, FString::Printf(TEXT("%s: клип удара у меша моба (клипы: %s)"), *Who, *Clips));
			return;
		}
		bool bSkillClip = false;   // клип слота навыка 18–21 = файлы anim_11..14
		for (const FString& N : S.Clips)
		{
			for (int32 F = 11; F <= 14; ++F) { bSkillClip |= N.EndsWith(FString::Printf(TEXT("anim_%02d"), F)); }
		}
		S.Check(bSkillClip, FString::Printf(TEXT("%s: замах — клип навыка у меша моба (клипы: %s)"), *Who, *Clips));
		// Снаряд: у навыка с .jeb фазы 3 и пуском A > 0 автомат пускает снаряд (у навыков 5/38 фаза 3 — эффект на мобе, см. FLOW.md §3).
		const bool bWantProj = C.Skill == 6 || C.Skill == 12 || C.Skill == 33 || C.Skill == 38 || C.Skill == 39 || C.Skill == 40;
		if (bWantProj) { S.Check(S.MaxProj > 0, FString::Printf(TEXT("%s: актор снаряда в мире (наибольшее число %d)"), *Who, S.MaxProj)); }
		for (const TCHAR* J : { C.Jeb2, C.Jeb3, C.Jeb4 })
		{
			if (*J) { S.Check(SawFx(S, J), FString::Printf(TEXT("%s: эффект %s заспавнен (эффекты: %s)"), *Who, J, *Fx)); }
		}
	}

	// Конец прогона: мобы, камера, флаги, номер.
	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		for (int32 i = 0; i < S.Cases.Num(); ++i)
		{
			if (AAsdaMobActor* M = S.Mob(kRidBase + i)) { M->StartFadeOut(EAsdaMobFade::Aoi, AAsdaMobActor::AoiFadeSeconds); }
		}
		if (AAsdaPlayerCharacter* Me = MeOf(S)) { if (S.OrigArm >= 0.f && Me->CameraArm) { Me->CameraArm->TargetArmLength = S.OrigArm; } }
		if (UAsdaNetSubsystem* N = S.Net.Get()) { N->bInGameConnected = S.bWasInGame; }
		S.NetScope.Reset();
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaMobRangedTest, TEXT("ДАЛЬНИЕ АТАКИ"), S.Ok, S.Fail);
	}

	// Тик сценария.
	static void Tick(const TSharedRef<FState>& InSP, UWorld* InW)
	{
		const TSharedRef<FState> SP = InSP;
		FState& S = *SP;
		UWorld* W = S.W.Get();
		if (!W) { FWorldDelegates::OnWorldPostActorTick.Remove(S.H); return; }
		if (InW != W) { return; }
		while (S.Steps.IsValidIndex(S.Idx))
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
			if (!MeOf(*S) && GEngine)
			{
				GEngine->Exec(S->W.Get(), TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(S->W.Get(), TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}));
		S->Steps.Add(Wait(4.0f));
		S->Steps.Add(Do([S]()
		{
			UAsdaNetSubsystem* N = S->Net.Get();
			AAsdaPlayerCharacter* Me = MeOf(*S);
			S->Check(Me != nullptr && N != nullptr, TEXT("свой персонаж на карте"));
			if (!Me || !N) { S->Steps.SetNum(S->Idx + 1); return; }
			if (!N->bSelfUserNumKnown) { N->SelfUserNum = kSelfFallback; N->bSelfUserNumKnown = true; }
			N->bSelfDead = false;
			S->Self = (int32)N->SelfUserNum;
			S->bWasInGame = N->bInGameConnected; N->bInGameConnected = true;
			S->Home = AsdaCoord::GameToTile(Me->GetGamePosXZ());
		}));
		for (int32 i = 0; i < S->Cases.Num(); ++i)
		{
			const FCase C = S->Cases[i];
			const int32 Rid = kRidBase + i;
			const FIntPoint MobTile(0, 6);   // моб в 6 клетках от меня — дальняя дистанция
			S->Steps.Add(Do([S, C, Rid, MobTile]()
			{
				// Прежний моб гаснет; новый — 4013 рядом (клетка от своей).
				if (Rid > kRidBase) { if (AAsdaMobActor* P = S->Mob(Rid - 1)) { P->StartFadeOut(EAsdaMobFade::Aoi, AAsdaMobActor::AoiFadeSeconds); } }
				S->Feed(4013, AsdaTestPackets::MobSpawn4013(Rid, C.Type, S->Home + MobTile, 1, kSpdDenom));
			}));
			S->Steps.Add(Wait(2.0f));   // загрузка меша и клипов, AoI-фейд прежнего
			S->Steps.Add(Do([S, C, Rid, MobTile]()
			{
				Mark(*S);
				S->bMobAtMark = S->Mob(Rid) != nullptr;
				if (C.bCast) { S->Feed(8011, Body8011(C.Skill, Rid)); }
				else if (C.Skill > 0) { S->Feed(8012, Body8012(C.Skill, Rid, S->Self, 120)); }
				else { S->Feed(4015, Body4015Hit(S->Self, Rid, C.Type, S->Home + MobTile, 120)); }
			}));
			if (C.bCast)
			{
				// Каст ~1 с, затем 8012 того же навыка (голова очереди в касте → атака 15).
				S->Steps.Add(FStep([S, Rid](float T) { Sample(*S, Rid); return T >= 1.0f; }));
				S->Steps.Add(Do([S, C, Rid]() { S->Feed(8012, Body8012(C.Skill, Rid, S->Self, 120)); }));
			}
			// Наблюдение 4.5 с (навык Ладьи: пуск A = 2 с + полёт), кадры замаха и полёта.
			const float TShot1 = 0.35f, TShot2 = FMath::Max(0.6f, C.A / 1000.f + 0.3f);
			S->Steps.Add(FStep([S, C, Rid, TShot1, TShot2](float T)
			{
				Sample(*S, Rid);
				static TMap<int32, int32> Shots;   // rid → сколько кадров снято
				int32& N = Shots.FindOrAdd(Rid);
				if (T < 0.05f) { N = 0; }
				if (N == 0 && T >= TShot1) { Frame(*S, FString::Printf(TEXT("ranged_%d_%d_wind"), C.Type, C.Skill)); ++N; }
				if (N == 1 && T >= TShot2) { Frame(*S, FString::Printf(TEXT("ranged_%d_%d_flight"), C.Type, C.Skill)); ++N; }
				return T >= 4.5f;
			}));
			S->Steps.Add(Do([S, C, Rid]() { Verdict(*S, C, Rid); }));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMobRangedFx(
	TEXT("asda.SelfTest.MobRangedFx"),
	TEXT("Самотест дальних атак мобов (Кайя 286/287, Слон 303/305, Ладья 319/321): 8012 навыка/4015 удара путём приёма пакета, "
	     "проверка клипа замаха, актора снаряда и эффектов фаз на сцене. `cases=<тип>:<навык>,…` (навык 0 — обычный удар; без "
	     "аргумента — встроенный список), `shot=<папка>` — кадры, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaMobRangedTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!Net || !WS) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем сети/мира")); return; }
		TSharedRef<FState> SP = MakeShared<FState>();
		SP->W = W; SP->bQuit = KA.bQuit; SP->Net = Net; SP->WS = WS;
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase))
			{
				SP->ShotDir = FPaths::ConvertRelativePathToFull(P.Mid(5));
				IFileManager::Get().MakeDirectory(*SP->ShotDir, /*Tree*/true);
			}
			else if (P.StartsWith(TEXT("cases="), ESearchCase::IgnoreCase))
			{
				TArray<FString> List;
				P.Mid(6).ParseIntoArray(List, TEXT(","));
				for (const FString& E : List)
				{
					FString A, B;
					if (!E.Split(TEXT(":"), &A, &B)) { continue; }
					const int32 Ty = FCString::Atoi(*A), Sk = FCString::Atoi(*B);
					for (const FCase& D : kDefault) { if (D.Type == Ty && D.Skill == Sk) { SP->Cases.Add(D); } }
				}
			}
		}
		if (SP->Cases.Num() == 0) { for (const FCase& D : kDefault) { SP->Cases.Add(D); } }
		SP->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
