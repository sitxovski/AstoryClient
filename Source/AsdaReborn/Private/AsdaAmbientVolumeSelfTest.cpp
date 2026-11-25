// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ГРОМКОСТИ ЗВУКОВ МИРА ПО НАСТРОЙКАМ (05.11.2026, баг игроков «гул дирижабля в Альпене громкий при звуке 0»):
//   `asda.SelfTest.AmbientVolume [quit]`. Путь игрока: офлайн (`-AsdaNoNet`), переход на КАЖДУЮ карту `/Game/Map/**` штатным
//   `OpenLevel` (как портал), громкость — той же точкой, что окно настроек 69500 (`UAsdaSoundSubsystem::ApplyUserVolumes`:
//   полоса 70013 «музыка», чекбокс 70012, полоса 70016 «звук», чекбокс 70015).
//   Игра идёт с `-nosound` — устройства нет, поэтому «действующая громкость» компонента = его `VolumeMultiplier` (тот множитель,
//   что движок умножает на громкость ассета при старте и на лету); громкость щелчка Slate — множитель своего устройства порта.
//   На каждой карте:
//     П) по прибытии (громкость с прошлой карты = всё 0): звуки, запечённые в уровень (`AAmbientSound`), уже немые — множитель
//        поставлен ДО активации (колбэк инициализации мира);
//     Б) рабочие громкости (музыка 0.5, звук 0.8): эмиттеры уровня = 0.8, музыка = 0.5 — ползунок до них доходит;
//     1) звук 0 → все немузыкальные компоненты мира = 0;  2) «заглушить звук» при полосе 1 → 0;
//     3) музыка 0 → музыка = 0;  4) «заглушить музыку» → 0;  5) всё 0 + «заглушить» → КАЖДЫЙ аудиокомпонент мира = 0,
//        щелчок Slate = 0; новая петля 2D/3D, заведённая при немом звуке, — 0;
//   Плюс один раз — воспроизведение бага: `asda.Sound.LevelAmbientSlider 0` → эмиттеры уровня при звуке 0 остаются на 1.
//   В конце громкости и ключи возвращаются (`AsdaCVarSave::RestoreAll`). Итог: «ГРОМКОСТЬ-МИРА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"
#include "AsdaCVarSave.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Resources/AsdaAssetPaths.h"
#include "Resources/AsdaGameUserSettings.h"   // флажок «Прорисовка травы» (Render3D[2])
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/AudioComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/AmbientSound.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"   // FSoundAttenuationSettings::Evaluate — громкость компонента на дистанции
#include "UObject/UObjectIterator.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaAmbientVolTest, Log, All);

// Определены в AsdaSoundSubsystem.cpp (без правки заголовка): своё ли устройство звука Slate и громкость щелчка сейчас.
ASDAREBORN_API bool AsdaSlateSoundDeviceIsPorts();
ASDAREBORN_API float AsdaSlateSoundEffectiveVolume();
// Драйвер эмиттеров карты (AsdaSoundSubsystem.cpp): шаг, состояние, заморозка тикера, флажок «Прорисовка травы».
ASDAREBORN_API void AsdaMapEmitterStep(UWorld* World, const FVector& Cam, double Now, bool bGrass);
ASDAREBORN_API bool AsdaMapEmitterInfo(const UAudioComponent* AC, bool& bSounding, double& StopAt, float& TrigR);
ASDAREBORN_API void AsdaMapEmitterHold(bool bHold);
ASDAREBORN_API bool AsdaMapEmitterGrassOn();

namespace AsdaAmbientVolTest
{
	const TCHAR* const kCmd = TEXT("asda.SelfTest.AmbientVolume");   // имя команды
	const TCHAR* const kPrefix = TEXT("ГРОМКОСТЬ-МИРА");             // префикс строк журнала
	constexpr float kMusicWork = 0.5f;                                // рабочая громкость музыки (полоса 70013)
	constexpr float kSfxWork = 0.8f;                                  // рабочая громкость звука (полоса 70016)
	constexpr double kTravelTimeoutSec = 120.0;                       // потолок ожидания карты
	constexpr double kSettleSec = 2.0;                                // пауза после начала игры на карте

	// Состояние прогона (живёт между тиками и переходами).
	struct FRun
	{
		TWeakObjectPtr<UGameInstance> GI;   // игра
		bool bQuit = false;                 // `quit` после итога
		int32 Ok = 0, Fail = 0;             // счётчики проверок
		TArray<FString> Maps;               // пакеты уровней по порядку
		int32 Idx = -1;                     // текущая карта
		TWeakObjectPtr<UWorld> PrevWorld;   // мир до перехода
		double PhaseStart = 0.0;            // начало ожидания карты
		bool bArrivalChecked = false;       // проверка «по прибытии» сделана
		int32 MapsWithLevelAmb = 0;         // карт с эмиттерами, запечёнными в уровень
		int32 LevelAmbTotal = 0;            // эмиттеров уровня всего
		bool bBugReproduced = false;        // проверка ключа 0 сделана (один раз)
	};
	static TSharedPtr<FRun> GRun;          // единственный прогон

	static void Expect(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok;   UE_LOG(LogAsdaAmbientVolTest, Log,   TEXT("%s ok   %s"), kPrefix, *What); }
		else   { ++R.Fail; UE_LOG(LogAsdaAmbientVolTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
	}

	// Музыкальный ли компонент: звук из папки музыки (`AsdaAssetPaths::kMusicFolder`).
	static bool IsMusic(const UAudioComponent* AC)
	{
		return AC && AC->Sound && AC->Sound->GetPathName().Contains(FString(TEXT("/")) + AsdaAssetPaths::kMusicFolder + TEXT("/"));
	}

	// Все живые аудиокомпоненты мира со звуком (музыкальный стрим переживает смену уровня — его мир может быть прежним,
	//   поэтому музыка берётся из любого игрового мира).
	static void Collect(UWorld* W, TArray<UAudioComponent*>& Sfx, TArray<UAudioComponent*>& Music, TArray<UAudioComponent*>& LevelAmb)
	{
		for (TObjectIterator<UAudioComponent> It; It; ++It)
		{
			UAudioComponent* AC = *It;
			if (!IsValid(AC) || !AC->Sound || AC->IsTemplate()) { continue; }
			UWorld* CW = AC->GetWorld();
			if (!CW || !CW->IsGameWorld()) { continue; }
			if (IsMusic(AC)) { Music.Add(AC); continue; }
			if (CW != W) { continue; }
			Sfx.Add(AC);
			if (Cast<AAmbientSound>(AC->GetOwner())) { LevelAmb.Add(AC); }
		}
	}

	// Все компоненты из списка имеют множитель `Want`; `Bad` — первые расхождения.
	static bool AllAt(const TArray<UAudioComponent*>& L, float Want, FString& Bad)
	{
		bool b = true;
		for (const UAudioComponent* AC : L)
		{
			if (FMath::IsNearlyEqual(AC->VolumeMultiplier, Want, 1e-4f)) { continue; }
			b = false;
			if (Bad.Len() < 300) { Bad += FString::Printf(TEXT(" %s/%s=%.3f"), *GetNameSafe(AC->GetOwner()), *GetNameSafe(AC->Sound), AC->VolumeMultiplier); }
		}
		return b;
	}

	static void Apply(float Music, bool bMusicMuted, float Sfx, bool bSfxMuted)
	{
		UAsdaSoundSubsystem::ApplyUserVolumes(Music, bMusicMuted, Sfx, bSfxMuted);
	}

	// Независимый оракул громкости FMOD3 оригинала: 1 внутри min, далее min / (min + 0.7·(d − min)).
	static float FmodInverse(float Mn, float D) { return D <= Mn ? 1.f : Mn / (Mn + 0.7f * (D - Mn)); }
	// Громкость в дБ (пол −120 дБ для нуля).
	static float Db(float G) { return 20.f * FMath::LogX(10.f, FMath::Max(G, 1e-6f)); }
	// Движковая громкость компонента на дистанции `D` (тот же `Evaluate`, что зовёт аудиопоток): источник в нуле, слушатель на оси X.
	static float Gain(const UAudioComponent* AC, float D)
	{
		return AC->AttenuationOverrides.Evaluate(FTransform::Identity, FVector(D, 0.f, 0.f));
	}
	// Радиусы эмиттера из его затухания (min — сфера, max — min + спад).
	static void Radii(const UAudioComponent* AC, float& Mn, float& Mx)
	{
		Mn = (float)AC->AttenuationOverrides.AttenuationShapeExtents.X;
		Mx = Mn + AC->AttenuationOverrides.FalloffDistance;
	}

	// К) кривая эмиттеров уровня: радиусы = данные карты; на 0.05 / 1 / 5 / 10 м и на половине радиуса — формула оригинала
	//   (±0.3 дБ); у края радиуса — 0; ключ 0 возвращает прежний линейный спад (бага «на 17–31 дБ громче»).
	static void CheckCurve(FRun& R, const FString& Map, const TArray<UAudioComponent*>& Amb)
	{
		// Пары min/max из Pos.POS (ед. карты = см UE), отсортированные по max.
		TArray<int32> WantMax;
		if (Map == TEXT("alpen"))   { WantMax = { 2000, 2000, 3000 }; }
		if (Map == TEXT("flammio")) { WantMax = { 2000, 2000, 2000, 2000, 2000 }; }
		TArray<int32> GotMax;
		bool bMinOk = true;
		for (const UAudioComponent* AC : Amb)
		{
			float Mn, Mx; Radii(AC, Mn, Mx);
			GotMax.Add(FMath::RoundToInt(Mx));
			bMinOk &= FMath::IsNearlyEqual(Mn, 10.f, 0.01f);
		}
		GotMax.Sort();
		Expect(R, bMinOk && GotMax == WantMax, FString::Printf(TEXT("%s К: радиусы эмиттеров = Pos.POS (min 10, max %s)"), *Map,
			*FString::JoinBy(GotMax, TEXT("/"), [](int32 V) { return FString::FromInt(V); })));
		for (const UAudioComponent* AC : Amb)
		{
			float Mn, Mx; Radii(AC, Mn, Mx);
			const bool bCustom = AC->AttenuationOverrides.DistanceAlgorithm == EAttenuationDistanceModel::Custom;
			FString Row;
			float MaxErr = 0.f;
			for (const float D : { 5.f, 100.f, 500.f, 1000.f, 0.5f * Mx })
			{
				const float G = Gain(AC, D), O = FmodInverse(Mn, D);
				MaxErr = FMath::Max(MaxErr, FMath::Abs(Db(G) - Db(O)));
				Row += FString::Printf(TEXT(" %.0f:%.1f/%.1f"), D, Db(G), Db(O));
			}
			const float Edge = Gain(AC, Mx);
			Expect(R, bCustom && MaxErr <= 0.3f && Edge <= 1e-3f,
				FString::Printf(TEXT("%s К: %s %.0f/%.0f — кривая FMOD3 (порт/оригинал дБ на ед.:%s), ошибка %.2f дБ, у края %.4f"),
					*Map, *GetNameSafe(AC->GetOwner()), Mn, Mx, *Row, MaxErr, Edge));
		}
		// Прежний линейный спад ключом 0 — воспроизведение расхождения, затем обратно.
		AsdaCVarSave::SetForTest(TEXT("asda.Sound.LevelAmbientEtalonCurve"), 0);
		{
			const UAudioComponent* AC = Amb[0];
			float Mn, Mx; Radii(AC, Mn, Mx);
			const float Lin5 = Db(Gain(AC, 500.f)), Etal5 = Db(FmodInverse(Mn, 500.f));
			Expect(R, Mn > 0.f && Lin5 - Etal5 >= 17.f,
				FString::Printf(TEXT("%s К: ключ 0 — прежний линейный спад, на 5 м %.1f дБ против эталонных %.1f (громче на %.1f дБ — было до правки)"),
					*Map, Lin5, Etal5, Lin5 - Etal5));
		}
		AsdaCVarSave::SetForTest(TEXT("asda.Sound.LevelAmbientEtalonCurve"), 1);
		{
			float Mn, Mx; Radii(Amb[0], Mn, Mx);
			Expect(R, Amb[0]->AttenuationOverrides.DistanceAlgorithm == EAttenuationDistanceModel::Custom && FMath::IsNearlyEqual(Mn, 10.f, 0.01f),
				FString::Printf(TEXT("%s К: ключ 1 снова — эталонная кривая, радиусы сохранены (%.0f/%.0f)"), *Map, Mn, Mx));
		}
	}

	// Т) запуск эмиттера по радиусу камеры и доигрыш (эталон `mapObjUpdateEmittersAndSounds@0xB5C480`) + гейт «Прорисовка травы»
	//   (GRASS RENDER = флажок 3D №3, профиль `Render3D[2]`). Шаги ведёт тест (`AsdaMapEmitterStep` — тот же шаг, что у тикера).
	static void CheckTrigger(FRun& R, UWorld* W, const FString& Map, const TArray<UAudioComponent*>& Amb)
	{
		// Радиусы запуска = √(+0x118) из Pos.POS: alpen 3000 + 2 × 2000, flammio 5 × 2000.
		TArray<int32> WantTrig;
		if (Map == TEXT("alpen"))   { WantTrig = { 2000, 2000, 3000 }; }
		if (Map == TEXT("flammio")) { WantTrig = { 2000, 2000, 2000, 2000, 2000 }; }
		TArray<int32> GotTrig;
		bool bUnderDriver = true;
		for (const UAudioComponent* A : Amb)
		{
			bool bS0 = false; double St0 = 0.0; float Tr0 = 0.f;
			bUnderDriver &= AsdaMapEmitterInfo(A, bS0, St0, Tr0) && !A->bAutoActivate;
			GotTrig.Add(FMath::RoundToInt(Tr0));
		}
		GotTrig.Sort();
		Expect(R, bUnderDriver && GotTrig == WantTrig, FString::Printf(TEXT("%s Т: все эмиттеры под драйвером, автозапуск снят; радиусы запуска = √(+0x118) Pos.POS (%s)"),
			*Map, *FString::JoinBy(GotTrig, TEXT("/"), [](int32 V) { return FString::FromInt(V); })));

		AsdaMapEmitterHold(true);
		UAudioComponent* AC = Amb[0];
		const FVector P = AC->GetComponentLocation();
		bool bS = false; double StopAt = 0.0; float TrigR = 0.f;
		AsdaMapEmitterInfo(AC, bS, StopAt, TrigR);
		const double Dur = AC->Sound ? AC->Sound->Duration : 0.0;
		Expect(R, FMath::Abs(Dur - 6.37) < 0.05, FString::Printf(TEXT("%s Т: длительность прохода %.2f с (s_obj_propel1.wav — 6,37 с)"), *Map, Dur));
		const FVector Far = P + FVector(TrigR + 50.f, 0.f, 0.f), Near = P + FVector(TrigR - 50.f, 0.f, 0.f);
		const double T = 1000.0;
		// Камера вне радиуса — тишина.
		AsdaMapEmitterStep(W, Far, T, true);
		AsdaMapEmitterInfo(AC, bS, StopAt, TrigR);
		Expect(R, !bS, FString::Printf(TEXT("%s Т: камера в %.0f ед. (за радиусом %.0f) — эмиттер молчит"), *Map, TrigR + 50.f, TrigR));
		// Вошли — звучит с начала прохода.
		AsdaMapEmitterStep(W, Near, T, true);
		AsdaMapEmitterInfo(AC, bS, StopAt, TrigR);
		Expect(R, bS && StopAt == 0.0, FString::Printf(TEXT("%s Т: камера в %.0f ед. — эмиттер звучит"), *Map, TrigR - 50.f));
		// В радиусе 20 с — звучит без конца (перезапуск оригинала без паузы).
		AsdaMapEmitterStep(W, Near, T + 20.0, true);
		AsdaMapEmitterInfo(AC, bS, StopAt, TrigR);
		Expect(R, bS && StopAt == 0.0, FString::Printf(TEXT("%s Т: в радиусе 20 с — звучит, доигрыша нет"), *Map));
		// Вышли на 1,5 прохода позже начала — доигрыш до конца текущего (второго) прохода.
		const double TOut = T + 1.5 * Dur;
		AsdaMapEmitterStep(W, Far, TOut, true);
		AsdaMapEmitterInfo(AC, bS, StopAt, TrigR);
		const double Left = StopAt - TOut;
		Expect(R, bS && FMath::IsNearlyEqual(StopAt, T + 2.0 * Dur, 1e-3) && Left > 0.0 && Left <= Dur + 1e-6,
			FString::Printf(TEXT("%s Т: вышли из радиуса — доигрывает %.2f с до конца прохода (≤ %.2f)"), *Map, Left, Dur));
		const double End = StopAt;
		AsdaMapEmitterStep(W, Far, End - 0.05, true);
		AsdaMapEmitterInfo(AC, bS, StopAt, TrigR);
		Expect(R, bS, FString::Printf(TEXT("%s Т: за 0,05 с до конца прохода ещё звучит"), *Map));
		AsdaMapEmitterStep(W, Far, End + 0.01, true);
		AsdaMapEmitterInfo(AC, bS, StopAt, TrigR);
		Expect(R, !bS, FString::Printf(TEXT("%s Т: проход доигран — тишина"), *Map));
		AsdaMapEmitterStep(W, Far, End + 30.0, true);
		AsdaMapEmitterInfo(AC, bS, StopAt, TrigR);
		Expect(R, !bS, FString::Printf(TEXT("%s Т: через 30 с вне радиуса — повторного запуска нет"), *Map));
		// Возврат в радиус во время доигрыша — проход продолжается, доигрыш снят.
		const double T2 = End + 100.0;
		AsdaMapEmitterStep(W, Near, T2, true);
		AsdaMapEmitterStep(W, Far, T2 + 1.0, true);
		AsdaMapEmitterStep(W, Near, T2 + 2.0, true);
		AsdaMapEmitterInfo(AC, bS, StopAt, TrigR);
		Expect(R, bS && StopAt == 0.0, FString::Printf(TEXT("%s Т: вернулись в радиус во время доигрыша — звучит дальше"), *Map));
		AsdaMapEmitterStep(W, Far, T2 + 3.0, true);
		AsdaMapEmitterStep(W, Far, T2 + 3.0 + 2.0 * Dur, true);
		// Гейт травы: «Прорисовка травы» выключена — в радиусе не запускается.
		AsdaMapEmitterStep(W, Near, T2 + 100.0, false);
		AsdaMapEmitterInfo(AC, bS, StopAt, TrigR);
		Expect(R, !bS, FString::Printf(TEXT("%s Т: «Прорисовка травы» выключена — в радиусе молчит (GRASS RENDER: FALSE)"), *Map));
		// Траву выключили во время звучания — проход доигрывает (эталон: ветка 0xB5D3B0 запись не трогает).
		AsdaMapEmitterStep(W, Near, T2 + 200.0, true);
		AsdaMapEmitterStep(W, Near, T2 + 201.0, false);
		AsdaMapEmitterInfo(AC, bS, StopAt, TrigR);
		Expect(R, bS && FMath::IsNearlyEqual(StopAt, T2 + 200.0 + Dur, 1e-3),
			FString::Printf(TEXT("%s Т: траву выключили во время звучания — доигрывает до конца прохода"), *Map));
		AsdaMapEmitterStep(W, Far, T2 + 300.0, true);
		// Флажок окна → профиль → гейт: `Render3D[2]` = 0 / 1.
		if (UAsdaGameUserSettings* Prof = UAsdaGameUserSettings::Get())
		{
			if (Prof->Render3D.IsValidIndex(2))
			{
				const uint8 Was = Prof->Render3D[2];
				Prof->Render3D[2] = 0; const bool bOff = AsdaMapEmitterGrassOn();
				Prof->Render3D[2] = 1; const bool bOn = AsdaMapEmitterGrassOn();
				Prof->Render3D[2] = Was;
				Expect(R, !bOff && bOn, FString::Printf(TEXT("%s Т: гейт читает флажок «Прорисовка травы» профиля (Render3D[2]: 0 → %d, 1 → %d)"),
					*Map, bOff ? 1 : 0, bOn ? 1 : 0));
			}
		}
		// Ключ 0 — прежняя вечная петля: автозапуск вернулся, драйвер отпустил эмиттеры.
		AsdaCVarSave::SetForTest(TEXT("asda.Sound.MapEmitterTrigger"), 0);
		bool bAuto = true, bAnyUnder = false;
		for (const UAudioComponent* A : Amb)
		{
			bool B2 = false; double S2 = 0.0; float T3 = 0.f;
			bAuto &= A->bAutoActivate;
			bAnyUnder |= AsdaMapEmitterInfo(A, B2, S2, T3);
		}
		Expect(R, bAuto && !bAnyUnder, FString::Printf(TEXT("%s Т: ключ 0 — эмиттеры снова с автозапуском (прежняя петля)"), *Map));
		AsdaCVarSave::SetForTest(TEXT("asda.Sound.MapEmitterTrigger"), 1);
		bool bBack = true;
		for (const UAudioComponent* A : Amb)
		{
			bool B2 = false; double S2 = 0.0; float T3 = 0.f;
			bBack &= AsdaMapEmitterInfo(A, B2, S2, T3) && !A->bAutoActivate;
		}
		Expect(R, bBack, FString::Printf(TEXT("%s Т: ключ 1 снова — эмиттеры под драйвером"), *Map));
		AsdaMapEmitterHold(false);
	}

	// Проверки одной карты (мир уже играет).
	static void CheckMap(FRun& R, UWorld* W)
	{
		const FString Map = W->GetMapName();
		UGameInstance* GI = R.GI.Get();
		UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
		TArray<UAudioComponent*> Sfx, Music, Amb;
		Collect(W, Sfx, Music, Amb);
		UE_LOG(LogAsdaAmbientVolTest, Log, TEXT("%s %s: компонентов звука %d, музыки %d, запечённых в уровень %d"),
			kPrefix, *Map, Sfx.Num(), Music.Num(), Amb.Num());
		if (Amb.Num() > 0) { ++R.MapsWithLevelAmb; R.LevelAmbTotal += Amb.Num(); }
		FString Bad;

		// П) по прибытии: всё было 0 с прошлой карты (первую карту прогон начинает с 0 перед переходом).
		Bad.Reset();
		Expect(R, AllAt(Amb, 0.f, Bad), FString::Printf(TEXT("%s П: эмиттеры уровня (%d) немые с первого кадра при звуке 0%s"), *Map, Amb.Num(), *Bad));

		// Б) рабочие громкости.
		Apply(kMusicWork, false, kSfxWork, false);
		Bad.Reset();
		Expect(R, AllAt(Amb, kSfxWork, Bad), FString::Printf(TEXT("%s Б: эмиттеры уровня следуют полосе «Звук» = %.1f%s"), *Map, kSfxWork, *Bad));
		Bad.Reset();
		Expect(R, AllAt(Music, kMusicWork, Bad), FString::Printf(TEXT("%s Б: музыка (%d) = %.1f%s"), *Map, Music.Num(), kMusicWork, *Bad));

		// К) кривая затухания эмиттеров против формулы оригинала (FMOD3 inverse, rolloff 0.7; `F_sound.md` §3.2) и радиусы против
		//   данных Pos.POS (`F_sound.md` §2: alpen 10/3000 + 2 × 10/2000, flammio 5 × 10/2000).
		if (Amb.Num() > 0) { CheckCurve(R, Map, Amb); }
		if (Amb.Num() > 0) { CheckTrigger(R, W, Map, Amb); }   // Т) запуск по радиусу камеры, доигрыш, гейт травы

		// Ключ 0 — прежнее поведение (воспроизведение бага), один раз на первой карте с эмиттерами.
		if (!R.bBugReproduced && Amb.Num() > 0)
		{
			R.bBugReproduced = true;
			AsdaCVarSave::SetForTest(TEXT("asda.Sound.LevelAmbientSlider"), 0);
			Apply(kMusicWork, false, 0.f, true);
			Bad.Reset();
			Expect(R, AllAt(Amb, 1.f, Bad), FString::Printf(TEXT("%s БАГ: при ключе 0 и звуке 0 эмиттеры уровня остаются на 1 (было до правки)%s"), *Map, *Bad));
			AsdaCVarSave::SetForTest(TEXT("asda.Sound.LevelAmbientSlider"), 1);
			Apply(kMusicWork, false, kSfxWork, false);
		}

		// 1) звук 0.
		Apply(kMusicWork, false, 0.f, false);
		Bad.Reset();
		Expect(R, AllAt(Sfx, 0.f, Bad), FString::Printf(TEXT("%s 1: полоса «Звук» 0 → все %d немузыкальных компонентов = 0%s"), *Map, Sfx.Num(), *Bad));
		// 2) «заглушить звук» при полосе 1.
		Apply(kMusicWork, false, 1.f, true);
		Bad.Reset();
		Expect(R, AllAt(Sfx, 0.f, Bad), FString::Printf(TEXT("%s 2: «заглушить звук» → немузыкальные = 0%s"), *Map, *Bad));
		// 3) музыка 0.
		Apply(0.f, false, kSfxWork, false);
		Bad.Reset();
		Expect(R, AllAt(Music, 0.f, Bad), FString::Printf(TEXT("%s 3: полоса «Музыка» 0 → музыка = 0%s"), *Map, *Bad));
		// 4) «заглушить музыку».
		Apply(1.f, true, kSfxWork, false);
		Bad.Reset();
		Expect(R, AllAt(Music, 0.f, Bad), FString::Printf(TEXT("%s 4: «заглушить музыку» → музыка = 0%s"), *Map, *Bad));

		// 5) всё 0 и заглушено: каждый компонент мира, щелчок Slate, свежие петли менеджера.
		Apply(0.f, true, 0.f, true);
		UAudioComponent* Loop2D = Snd ? Snd->PlaySound2DManaged(FName(TEXT("S_e_button09"))) : nullptr;
		UAudioComponent* Loop3D = nullptr;
		if (Snd)
		{
			FVector Ear = FVector::ZeroVector, Front, Right;
			if (APlayerController* PC = W->GetFirstPlayerController()) { PC->GetAudioListenerPosition(Ear, Front, Right); }
			Loop3D = Snd->PlaySound3DAs(FName(TEXT("S_OBJ_PROPEL1")), Ear, EAsdaSoundClass::Ambient, nullptr, true);
		}
		TArray<UAudioComponent*> Sfx5, Music5, Amb5;
		Collect(W, Sfx5, Music5, Amb5);
		TArray<UAudioComponent*> All = Sfx5;
		All.Append(Music5);
		Bad.Reset();
		// Под `-nosound` устройства нет и менеджер новых компонентов не заводит (петли «нет») — на картах без эмиттеров уровня
		//   компонентов 0, проверка тогда — «ни одного звучащего» (условие отсутствия, а не сбой); эмиттеры обязаны найтись.
		Expect(R, All.Num() >= Amb.Num() && AllAt(All, 0.f, Bad),
			FString::Printf(TEXT("%s 5: всё 0 + «заглушить» → все %d аудиокомпонентов = 0 (в т. ч. новые петли 2D %s / 3D %s)%s"), *Map, All.Num(),
				Loop2D ? TEXT("есть") : TEXT("нет"), Loop3D ? TEXT("есть") : TEXT("нет"), *Bad));
		Expect(R, AsdaSlateSoundDeviceIsPorts() && AsdaSlateSoundEffectiveVolume() == 0.f,
			FString::Printf(TEXT("%s 5: щелчок кнопки Slate — устройство порта %d, громкость %.2f"), *Map, AsdaSlateSoundDeviceIsPorts() ? 1 : 0,
				AsdaSlateSoundEffectiveVolume()));
		for (UAudioComponent* L : { Loop2D, Loop3D })
		{
			if (IsValid(L)) { L->Stop(); L->DestroyComponent(); }
		}
		// Остаётся всё 0 — следующая карта проверяет «по прибытии».
	}

	static void Finish(FRun& R, UWorld* W)
	{
		UE_LOG(LogAsdaAmbientVolTest, Log, TEXT("%s: карт %d, с эмиттерами уровня %d (всего эмиттеров %d)"),
			kPrefix, R.Maps.Num(), R.MapsWithLevelAmb, R.LevelAmbTotal);
		Expect(R, R.bBugReproduced, TEXT("баг воспроизведён ключом 0 хотя бы на одной карте"));
		// Эмиттеры, запечённые в уровень, по данным карт (Pos.POS секция E): alpen 3 + flammio 5 — должны найтись все.
		Expect(R, R.MapsWithLevelAmb == 2 && R.LevelAmbTotal == 8,
			FString::Printf(TEXT("эмиттеры уровня найдены на %d картах, всего %d (ожидается 2 карты, 8 эмиттеров)"), R.MapsWithLevelAmb, R.LevelAmbTotal));
		AsdaSelfTestKit::Finish(W, kCmd, R.bQuit, LogAsdaAmbientVolTest, kPrefix, R.Ok, R.Fail);   // ключи и громкости — обратно
	}

	static void TravelNext(FRun& R, UWorld* W)
	{
		++R.Idx;
		if (!R.Maps.IsValidIndex(R.Idx)) { Finish(R, W); GRun.Reset(); return; }
		R.PrevWorld = W;
		R.PhaseStart = FPlatformTime::Seconds();
		R.bArrivalChecked = false;
		UE_LOG(LogAsdaAmbientVolTest, Log, TEXT("%s: переход %d/%d → %s"), kPrefix, R.Idx + 1, R.Maps.Num(), *R.Maps[R.Idx]);
		UGameplayStatics::OpenLevel(W, FName(*R.Maps[R.Idx]), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode"));
	}

	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		UGameInstance* GI = R.GI.Get();
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		const double Now = FPlatformTime::Seconds();
		const FString Want = FPackageName::GetShortName(R.Maps[R.Idx]);
		const bool bArrived = W && W != R.PrevWorld.Get() && W->GetMapName().EndsWith(Want) && W->HasBegunPlay();
		if (!bArrived)
		{
			if (Now - R.PhaseStart < kTravelTimeoutSec) { return true; }
			Expect(R, false, FString::Printf(TEXT("%s: за %.0f с карта не поднялась"), *R.Maps[R.Idx], kTravelTimeoutSec));
			if (!W) { Finish(R, nullptr); GRun.Reset(); return false; }
			TravelNext(R, W);
			return GRun.IsValid();
		}
		if (!R.bArrivalChecked)
		{
			R.bArrivalChecked = true;
			R.PhaseStart = Now;   // отсчёт паузы после начала игры
			return true;
		}
		if (Now - R.PhaseStart < kSettleSec) { return true; }
		CheckMap(R, W);
		TravelNext(R, W);
		return GRun.IsValid();
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 1800.f)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI || !GI->GetSubsystem<UAsdaSoundSubsystem>()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистемы звука")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.GI = GI;
		R.bQuit = Args.bQuit;

		// Каждая карта проекта: все уровни под /Game/Map (реестр ассетов), по алфавиту.
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Worlds;
		AR.GetAssetsByPath(FName(TEXT("/Game/Map")), Worlds, true);
		for (const FAssetData& D : Worlds)
		{
			if (D.AssetClassPath == UWorld::StaticClass()->GetClassPathName()) { R.Maps.AddUnique(D.PackageName.ToString()); }
		}
		R.Maps.Sort();
		UE_LOG(LogAsdaAmbientVolTest, Log, TEXT("%s: карт %d"), kPrefix, R.Maps.Num());
		if (R.Maps.Num() == 0) { Expect(R, false, TEXT("нет ни одной карты под /Game/Map")); Finish(R, W); GRun.Reset(); return; }

		// Громкости и ключ — под возврат в конце; старт с «всё 0», чтобы первая карта проверила путь «до активации».
		for (const TCHAR* K : { TEXT("asda.Sound.MusicVolume"), TEXT("asda.Sound.MusicEnable"), TEXT("asda.Sound.SfxVolume"),
		                        TEXT("asda.Sound.LevelAmbientSlider"), TEXT("asda.Sound.SlateSoundSlider"), TEXT("asda.Sound.LevelAmbientEtalonCurve"),
		                        TEXT("asda.Sound.MapEmitterTrigger") })
		{
			AsdaCVarSave::Remember(K);
		}
		Apply(0.f, true, 0.f, true);
		TravelNext(R, W);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.25f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestAmbientVolume(
	TEXT("asda.SelfTest.AmbientVolume"),
	TEXT("Самотест: на каждой карте /Game/Map полосы «Музыка»/«Звук» и «заглушить» окна настроек доводят КАЖДЫЙ аудиокомпонент мира "
	     "(эмиттеры уровня, петли менеджера, музыка, щелчок Slate) до 0; эмиттеры уровня немые с первого кадра. Офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaAmbientVolTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
