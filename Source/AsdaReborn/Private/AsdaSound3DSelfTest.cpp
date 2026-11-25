// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ 3D-ЗВУКА (30.10.2026, баг игроков «звуки боя без затухания по расстоянию»): `asda.SelfTest.Sound3D [quit]`.
//   Офлайн (`-AsdaNoNet`, игра идёт с `-nosound` — звук не звучит, поэтому громкость считается ДВИЖКОВЫМ расчётом затухания
//   `FBaseAttenuationSettings::Evaluate` по тому объекту, что игровой путь отдаёт движку при старте). Разделы:
//     Р) рост моделей персонажа (мужская и женская базы, вершины в позе привязки) против константы порта → единиц на метр;
//     A) причина бага: у звуковых ассетов своего затухания нет (прежний путь стартовал без него — «плоско»), план нового пути
//        несёт объект затухания, и компонент отдаёт его движку в момент старта (`GetAttenuationSettingsToApply`);
//     Б) таблица классов: громкость на 0 / 5 / 15 / 30 / 60 м, радиус до нуля, сверка с независимым оракулом FMOD3
//        (inverse, rolloff 0.7), монотонность, ноль на краю и за куполом; размеры существ mon_s/m/b/l;
//     В) правило концентрации: пул 15 голосов «самый тихий», группа класса «самый дальний», вес, лимит на источник; петли — вне;
//     Г) назначение класса по имени: все звуковые ассеты /Game/AsdaAssets/Sound получают класс (ни «по имени», ни «интерфейс»),
//        контрольные имена — ожидаемые классы;
//     Д) ключи: `asda.Sound.Classes 0` — прежний единый радиус, `asda.Sound.DistanceScale 2` — радиусы ×2;
//   Реальный путь удара соседа/своего (`ShowHitResult` приватный) проверяется журналом запусков звука (`Log LogAsdaSound Verbose`,
//   строки «ЗВУК 2D/3D …») на штатном самотесте боя наблюдателя `asda.Combat.SelfTestObserverSync` (пакеты 4029 соседа).
//   Итог: «ЗВУК3D ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "Test/AsdaSelfTestKit.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSound3DTest, Log, All);

namespace AsdaSound3DTestLocal
{
	const TCHAR* const kCmd = TEXT("asda.SelfTest.Sound3D");   // имя команды

	// Независимый оракул громкости FMOD3 оригинала (SPEC_FMOD_ATTEN.md §3): 1 внутри min, далее min/(min + 0.7·(d − min)).
	static float FmodInverse(float Mn, float D) { return D <= Mn ? 1.f : Mn / (Mn + 0.7f * (D - Mn)); }

	// Рост модели: вершины базового меша в позе привязки, переведённые трансформом базового компонента персонажа (тот же
	//   масштаб/поворот, что в мире). Возврат — высота (ед.), −1 если меша нет.
	static float MeshHeight(USkeletalMesh* Mesh, const FTransform& CompXf)
	{
		if (!Mesh) { return -1.f; }
		const FBox B = Mesh->GetImportedBounds().GetBox().TransformBy(CompXf);
		return B.Max.Z - B.Min.Z;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSound3D(
	TEXT("asda.SelfTest.Sound3D"),
	TEXT("Самотест 3D-звука: рост персонажа → ед./м, затухание до старта, громкость классов на 0/5/15/30/60 м, концентрация, "
	     "классы всех ассетов, ключи, реальный путь удара соседа/своего. Офлайн, карта мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaSound3DTestLocal;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		if (!Snd || !WS || !Net)
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем звука/мира/сети"));
			return;
		}
		int32 Ok = 0, Fail = 0;
		auto Check = [&Ok, &Fail](bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaSound3DTest, Log,   TEXT("ЗВУК3D ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaSound3DTest, Error, TEXT("ЗВУК3D FAIL %s"), *What); }
		};
		FAsdaCVarScope Cv(W, kCmd);
		// Эталонные значения ключей на время прогона (пользовательский профиль мог их сдвинуть).
		Cv.Set(TEXT("asda.Sound.Spatial3D"), 1);
		Cv.Set(TEXT("asda.Sound.Classes"), 1);
		Cv.Set(TEXT("asda.Sound.DistanceScale"), 1.f);
		Cv.Set(TEXT("asda.Sound.HearingRadiusM"), 35.f);
		Cv.Set(TEXT("asda.Sound.SilenceBeyondMax"), 1);
		Cv.Set(TEXT("asda.Sound.ClassConcurrency"), 1);
		Cv.Set(TEXT("asda.Sound.MaxVoices"), 16);
		Cv.Set(TEXT("asda.Sound.PoolFullPreventNew"), 0);
		Cv.Set(TEXT("asda.Sound.ForeignHit3D"), 1);

		// Свой персонаж (офлайн-мир alpen его даёт; иначе — штатный крюк, как у самотестов дедупа).
		if (!::IsValid(WS->LocalPlayerActor.Get()) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
		}
		AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get());
		const FVector Ear = Me ? Me->GetActorLocation() : FVector::ZeroVector;

		// ── Р) Рост персонажа = рост человека ──
		const float Upm = UAsdaSoundSubsystem::UnitsPerMeter();
		{
			float Hm = -1.f, Hf = -1.f;
			if (Me && Me->BaseSkel)
			{
				const FTransform Xf = Me->BaseSkel->GetComponentTransform();
				Hm = MeshHeight(UAsdaModelAnimSubsystem::LoadMeshChecked(FString(AsdaCharPreviewBase(1))), Xf);
				Hf = MeshHeight(UAsdaModelAnimSubsystem::LoadMeshChecked(FString(AsdaCharPreviewBase(2))), Xf);
			}
			const float Avg = (Hm > 0.f && Hf > 0.f) ? 0.5f * (Hm + Hf) : FMath::Max(Hm, Hf);
			UE_LOG(LogAsdaSound3DTest, Log, TEXT("ЗВУК3D РОСТ: мужская модель %.1f ед., женская %.1f ед., средний %.1f; константа порта %.1f; "
				"ед./м = %.2f (рост человека 1,72 м)"), Hm, Hf, Avg, UAsdaSoundSubsystem::CharHeightUnits(), Upm);
			Check(Avg > 0.f && FMath::Abs(Avg - UAsdaSoundSubsystem::CharHeightUnits()) <= 0.03f * UAsdaSoundSubsystem::CharHeightUnits(),
				FString::Printf(TEXT("Р: замеренный рост %.1f ед. совпадает с константой %.1f (допуск 3 %%)"), Avg, UAsdaSoundSubsystem::CharHeightUnits()));
		}

		// ── A) Причина бага и исправление ──
		{
			USoundBase* S = Snd->ResolveSound(FName(TEXT("S_c_sdamage")));
			Check(S && S->GetAttenuationSettingsToApply() == nullptr,
				TEXT("A: у звукового ассета S_c_sdamage своего затухания нет — прежний старт без объекта затухания шёл плоско (было)"));
			FAsdaSound3DPlan P;
			Snd->Plan3D(FName(TEXT("S_c_sdamage")), Ear, EAsdaSoundClass::Hit, 0.f, 0.f, false, P);
			UAudioComponent* AC = NewObject<UAudioComponent>(GetTransientPackage());
			AC->Sound = S;
			AC->AttenuationSettings = P.Att;   // так его заполняет `FAudioDevice::CreateComponent` из `Params.AttenuationSettings`
			const FSoundAttenuationSettings* At = AC->GetAttenuationSettingsToApply();   // это читает `PlayInternal` при старте
			Check(P.Att && At && At->bAttenuate && At->bSpatialize && FMath::IsNearlyEqual(At->FalloffDistance, P.Mx - P.Mn, 1.f),
				FString::Printf(TEXT("A: в момент старта движок получает затухание (сфера %.0f ед., спад %.0f ед., панорама) — стало"),
					At ? At->AttenuationShapeExtents.X : -1.f, At ? At->FalloffDistance : -1.f));
		}

		// ── Б) Таблица классов: громкость по дистанциям ──
		struct FCase { EAsdaSoundClass C; const TCHAR* Name; };
		const FCase Cases[] = {
			{ EAsdaSoundClass::Step,     TEXT("mon_s_0_Franken01-a_00_2") },
			{ EAsdaSoundClass::Step,     TEXT("mon_m_0_test-a_00_2") },
			{ EAsdaSoundClass::Step,     TEXT("mon_b_0_test-a_00_2") },
			{ EAsdaSoundClass::Step,     TEXT("mon_l_0_kliza01-a_00_2") },
			{ EAsdaSoundClass::Item,     TEXT("Sod_drop_Gold_0") },
			{ EAsdaSoundClass::Action,   TEXT("S_c_ssnormal1") },
			{ EAsdaSoundClass::Hit,      TEXT("S_c_sdamage") },
			{ EAsdaSoundClass::Voice,    TEXT("s_c_cri1") },
			{ EAsdaSoundClass::Creature, TEXT("mon_s_0_Franken01-a_00_3") },
			{ EAsdaSoundClass::Creature, TEXT("mon_l_0_kliza01-a_00_3") },
			{ EAsdaSoundClass::Magic,    TEXT("S_c_entry") },
			{ EAsdaSoundClass::Vehicle,  TEXT("s_e_riding_mustang") },
		};
		const float DistM[] = { 0.f, 5.f, 15.f, 30.f, 60.f };
		for (const FCase& K : Cases)
		{
			FAsdaSound3DPlan P;
			Snd->Plan3D(FName(K.Name), Ear, K.C, 0.f, 0.f, false, P);
			const FAsdaSoundClassSpec Sp = UAsdaSoundSubsystem::GetClassSpec(K.C, FName(K.Name));
			float G[5];
			for (int32 i = 0; i < 5; ++i) { G[i] = UAsdaSoundSubsystem::EvalGain(P, DistM[i] * Upm); }
			UE_LOG(LogAsdaSound3DTest, Log, TEXT("ЗВУК3D ТАБЛИЦА %-9s %-28s L1=%.0f дБ: полн. %.1f м (%.0f ед.), до нуля %.1f м (%.0f ед.) | 0 м %.3f | 5 м %.3f (%.1f дБ) | 15 м %.3f | 30 м %.3f | 60 м %.3f"),
				Sp.Name, K.Name, Sp.SourceDb, P.Mn / Upm, P.Mn, P.Mx / Upm, P.Mx, G[0], G[1], 20.f * FMath::LogX(10.f, FMath::Max(G[1], 1e-5f)), G[2], G[3], G[4]);
			bool bMono = true;
			for (int32 i = 1; i < 5; ++i) { bMono &= (G[i] <= G[i - 1] + 1e-4f); }
			Check(FMath::IsNearlyEqual(G[0], 1.f, 1e-3f) && bMono, FString::Printf(TEXT("Б: %s/%s — у источника 1.0, дальше только тише"), Sp.Name, K.Name));
			Check(UAsdaSoundSubsystem::EvalGain(P, P.Mx) <= 1e-3f && G[4] <= 1e-3f,
				FString::Printf(TEXT("Б: %s/%s — на краю радиуса (%.1f м) и на 60 м тишина"), Sp.Name, K.Name, P.Mx / Upm));
			// Оракул FMOD3 на эталонном участке [мин; 0,9·макс] — допуск 3 % или 0,005.
			float WorstErr = 0.f;
			for (int32 k = 0; k <= 20; ++k)
			{
				const float D = P.Mn + (0.9f * P.Mx - P.Mn) * k / 20.f;
				const float Ref = FmodInverse(P.Mn, D);
				WorstErr = FMath::Max(WorstErr, FMath::Abs(UAsdaSoundSubsystem::EvalGain(P, D) - Ref) / FMath::Max(Ref, 0.17f));
			}
			Check(WorstErr <= 0.03f, FString::Printf(TEXT("Б: %s/%s — кривая = FMOD3 inverse rolloff 0.7 (худшее расхождение %.1f %%)"), Sp.Name, K.Name, WorstErr * 100.f));
			// Ожидаемый радиус: 10^((L1 − 45)/20) м, сверху купол 35 м.
			const float WantM = FMath::Min(35.f, FMath::Pow(10.f, (Sp.SourceDb - 45.f) / 20.f));
			Check(FMath::IsNearlyEqual(P.Mx / Upm, FMath::Max(WantM, Sp.InnerM + 1.f / Upm), 0.05f),
				FString::Printf(TEXT("Б: %s/%s — радиус до нуля %.2f м = 10^((%.0f−45)/20) с куполом 35 м (%.2f м)"), Sp.Name, K.Name, P.Mx / Upm, Sp.SourceDb, WantM));
		}
		// Сравнение: далёкий бой не звучит как ближний — удар на 30 м минимум на 20 дБ тише удара на 1 м.
		{
			FAsdaSound3DPlan P; Snd->Plan3D(FName(TEXT("S_c_sdamage")), Ear, EAsdaSoundClass::Hit, 0.f, 0.f, false, P);
			const float G30 = UAsdaSoundSubsystem::EvalGain(P, 30.f * Upm);
			Check(G30 > 0.f && 20.f * FMath::LogX(10.f, G30) <= -20.f, FString::Printf(TEXT("Б: удар на 30 м тише удара рядом на %.1f дБ (≥ 20)"), -20.f * FMath::LogX(10.f, FMath::Max(G30, 1e-5f))));
			FAsdaSound3DPlan Ps; Snd->Plan3D(FName(TEXT("mon_s_0_Franken01-a_00_2")), Ear, EAsdaSoundClass::Auto, 0.f, 0.f, false, Ps);
			FAsdaSound3DPlan Pl; Snd->Plan3D(FName(TEXT("mon_l_0_kliza01-a_00_2")), Ear, EAsdaSoundClass::Auto, 0.f, 0.f, false, Pl);
			Check(Pl.Mx > Ps.Mx * 2.5f && Pl.Mn > Ps.Mn, FString::Printf(TEXT("Б: шаги огромного существа слышны дальше (%.1f м) малого (%.1f м)"), Pl.Mx / Upm, Ps.Mx / Upm));
			// Окружение карты (явные радиусы Pos.POS) — как заданы, сверху купол.
			FAsdaSound3DPlan Pa; Snd->Plan3D(FName(TEXT("S_OBJ_PROPEL1")), Ear, EAsdaSoundClass::Auto, 300.f, 1500.f, true, Pa);
			Check(Pa.Class == EAsdaSoundClass::Ambient && FMath::IsNearlyEqual(Pa.Mn, 300.f) && FMath::IsNearlyEqual(Pa.Mx, 1500.f) && Pa.ClassConc == nullptr,
				FString::Printf(TEXT("Б: окружение карты — радиусы из данных (300/1500 → %.0f/%.0f), петля вне групп"), Pa.Mn, Pa.Mx));
		}

		// ── В) Концентрация ──
		{
			FAsdaSound3DPlan P; Snd->Plan3D(FName(TEXT("S_c_sdamage")), Ear, EAsdaSoundClass::Hit, 0.f, 0.f, false, P);
			const USoundConcurrency* C = P.ClassConc;
			Check(C && C->Concurrency.MaxCount == 8 && C->Concurrency.ResolutionRule == EMaxConcurrentResolutionRule::StopFarthestThenOldest
				&& FMath::IsNearlyEqual(C->Concurrency.GetVolumeScale(), 0.85f, 1e-3f) && C->Concurrency.VolumeScaleMode == EConcurrencyVolumeScaleMode::Distance,
				FString::Printf(TEXT("В: группа «удар» — 8 голосов, лишний — самый дальний, вес %.2f по дальности"), C ? C->Concurrency.GetVolumeScale() : -1.f));
			Check(P.MaxPerSource == 2 && FMath::IsNearlyEqual(P.Priority, 1.5f), FString::Printf(TEXT("В: удар — не больше %d голосов на источник, приоритет %.1f"), P.MaxPerSource, P.Priority));
			FAsdaSound3DPlan Ps; Snd->Plan3D(FName(TEXT("mon_s_0_Franken01-a_00_2")), Ear, EAsdaSoundClass::Auto, 0.f, 0.f, false, Ps);
			Check(Ps.Class == EAsdaSoundClass::Step && Ps.MaxPerSource == 1 && Ps.ClassConc && Ps.ClassConc->Concurrency.MaxCount == 6,
				TEXT("В: шаги — 1 голос на моба, 6 на сцену"));
			FAsdaSound3DPlan Pl; Snd->Plan3D(FName(TEXT("S_c_sdamage")), Ear, EAsdaSoundClass::Hit, 0.f, 0.f, true, Pl);
			Check(Pl.ClassConc == nullptr && Pl.MaxPerSource == 0, TEXT("В: петли (окружение, автоматы) — вне групп и лимитов (ими управляет вызывающий)"));
			Cv.Set(TEXT("asda.Sound.ClassConcurrency"), 0);
			FAsdaSound3DPlan P0; Snd->Plan3D(FName(TEXT("S_c_sdamage")), Ear, EAsdaSoundClass::Hit, 0.f, 0.f, false, P0);
			Check(P0.ClassConc == nullptr && P0.MaxPerSource == 0, TEXT("В: asda.Sound.ClassConcurrency 0 — классовых лимитов нет (прежнее)"));
			Cv.Set(TEXT("asda.Sound.ClassConcurrency"), 1);
		}

		// ── Г) Класс по имени: контрольные имена и все ассеты ──
		{
			struct FNameCase { const TCHAR* Name; EAsdaSoundClass Want; };
			const FNameCase Names[] = {
				{ TEXT("S_c_sdamage"), EAsdaSoundClass::Hit },        { TEXT("S_c_scri"), EAsdaSoundClass::Hit },
				{ TEXT("S_c_stnormal2"), EAsdaSoundClass::Hit },      { TEXT("s_c_barrior3"), EAsdaSoundClass::Hit },
				{ TEXT("S_c_ssnormal1"), EAsdaSoundClass::Action },   { TEXT("S_c_bnormal1"), EAsdaSoundClass::Action },
				{ TEXT("Snd_char_n_0_arrowfly01"), EAsdaSoundClass::Action },
				{ TEXT("s_c_cri1"), EAsdaSoundClass::Voice },         { TEXT("Snd_char_m_0_chardie"), EAsdaSoundClass::Voice },
				{ TEXT("mon_s_0_Franken01-a_00_2"), EAsdaSoundClass::Step }, { TEXT("mon_s_0_Franken01-a_00_3"), EAsdaSoundClass::Creature },
				{ TEXT("mon_s_0_Franken01-a_00_7"), EAsdaSoundClass::Hit },  { TEXT("mon_s_0_pawn01-a_00_12d"), EAsdaSoundClass::Magic },
				{ TEXT("mon_s_0_Franken01-a_00_31"), EAsdaSoundClass::Creature },
				{ TEXT("Sod_drop_Gold_0"), EAsdaSoundClass::Item },   { TEXT("Snd_char_n_0_charpickup"), EAsdaSoundClass::Item },
				{ TEXT("s_e_riding_mustang"), EAsdaSoundClass::Vehicle }, { TEXT("itm1_n_96_vehicle_0030"), EAsdaSoundClass::Vehicle },
				{ TEXT("S_OBJ_PROPEL1"), EAsdaSoundClass::Ambient },  { TEXT("S_c_entry"), EAsdaSoundClass::Magic },
				{ TEXT("S_c_F2PowerBlade0"), EAsdaSoundClass::Magic }, { TEXT("s_e_fishing_start_00"), EAsdaSoundClass::Action },
				{ TEXT("S_e_shovelling01"), EAsdaSoundClass::Action }, { TEXT("S_e_sb_md_oh_at_mt_00"), EAsdaSoundClass::Magic },
			};
			int32 Bad = 0;
			for (const FNameCase& N : Names)
			{
				const EAsdaSoundClass Got = UAsdaSoundSubsystem::ClassifyByName(FName(N.Name));
				if (Got != N.Want)
				{
					++Bad;
					UE_LOG(LogAsdaSound3DTest, Error, TEXT("ЗВУК3D   %s → %s (ждали %s)"), N.Name, UAsdaSoundSubsystem::GetClassSpec(Got).Name,
						UAsdaSoundSubsystem::GetClassSpec(N.Want).Name);
				}
			}
			Check(Bad == 0, FString::Printf(TEXT("Г: контрольные имена — классы по правилу (%d из %d неверно)"), Bad, (int32)UE_ARRAY_COUNT(Names)));
			IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			TArray<FAssetData> Assets;
			AR.GetAssetsByPath(FName(TEXT("/Game/AsdaAssets/Sound")), Assets, /*bRecursive*/false);
			int32 PerClass[(int32)EAsdaSoundClass::Count] = {};
			int32 Total = 0, Wrong = 0;
			for (const FAssetData& A : Assets)
			{
				if (!A.IsInstanceOf(USoundBase::StaticClass())) { continue; }
				++Total;
				const EAsdaSoundClass C = UAsdaSoundSubsystem::ClassifyByName(A.AssetName);
				++PerClass[(int32)C];
				if (C == EAsdaSoundClass::Auto || C == EAsdaSoundClass::Ui || C == EAsdaSoundClass::Count) { ++Wrong; }
			}
			FString Dist;
			for (int32 i = 1; i < (int32)EAsdaSoundClass::Count; ++i)
			{
				Dist += FString::Printf(TEXT(" %s=%d"), UAsdaSoundSubsystem::GetClassSpec((EAsdaSoundClass)i).Name, PerClass[i]);
			}
			UE_LOG(LogAsdaSound3DTest, Log, TEXT("ЗВУК3D ПЕРЕПИСЬ АССЕТОВ: всего %d;%s"), Total, *Dist);
			Check(Total > 1000 && Wrong == 0, FString::Printf(TEXT("Г: все %d звуковых ассетов получают игровой класс (без класса/интерфейс — %d)"), Total, Wrong));
		}

		// ── Д) Ключи ──
		{
			Cv.Set(TEXT("asda.Sound.Classes"), 0);
			FAsdaSound3DPlan P0; Snd->Plan3D(FName(TEXT("mon_s_0_Franken01-a_00_2")), Ear, EAsdaSoundClass::Auto, 0.f, 0.f, false, P0);
			Check(FMath::IsNearlyEqual(P0.Mn, 100.f) && FMath::IsNearlyEqual(P0.Mx, 35.f * Upm, 2.f),
				FString::Printf(TEXT("Д: asda.Sound.Classes 0 — прежний единый закон (мин %.0f, макс %.0f = купол)"), P0.Mn, P0.Mx));
			Cv.Set(TEXT("asda.Sound.Classes"), 1);
			FAsdaSound3DPlan P1; Snd->Plan3D(FName(TEXT("mon_s_0_Franken01-a_00_2")), Ear, EAsdaSoundClass::Auto, 0.f, 0.f, false, P1);
			Cv.Set(TEXT("asda.Sound.DistanceScale"), 2.f);
			FAsdaSound3DPlan P2; Snd->Plan3D(FName(TEXT("mon_s_0_Franken01-a_00_2")), Ear, EAsdaSoundClass::Auto, 0.f, 0.f, false, P2);
			Cv.Set(TEXT("asda.Sound.DistanceScale"), 1.f);
			Check(FMath::IsNearlyEqual(P2.Mx, 2.f * P1.Mx, 2.f), FString::Printf(TEXT("Д: asda.Sound.DistanceScale 2 — радиус ×2 (%.0f → %.0f)"), P1.Mx, P2.Mx));
			FAsdaSound3DPlan Pf; Snd->Plan3D(FName(TEXT("S_c_sdamage")), Ear + FVector(60.f * Upm, 0.f, 0.f), EAsdaSoundClass::Hit, 0.f, 0.f, false, Pf);
			Check(!Me || Pf.bCulled, TEXT("Д: удар на 60 м — за радиусом, голос не заводится"));
		}

		AsdaSelfTestKit::Finish(W, kCmd, KA.bQuit, LogAsdaSound3DTest, TEXT("ЗВУК3D"), Ok, Fail);
	}),
	ECVF_Default);

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
