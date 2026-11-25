// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТЫ ВОЛНЫ В2 (часть C, эффекты/рендер) АУДИТА ДУБЛЕЙ №2 — «БЫЛО/СТАЛО» ИСПРАВЛЕНИЙ РАЗОШЕДШИХСЯ КОПИЙ
//   (11.10.2026; план `Reports/dedup_audit2/00_PLAN.md` §5 В2, находки `03_fx_render.md`, отчёт `wave2_C.md`).
//
// ★ ЗАЧЕМ. В2 меняет поведение НАМЕРЕННО. Каждая команда ниже одинаково запускается на сборке «до» и «после» и печатает
//   измеренное ШТАТНЫМ путём игры (те же функции, что зовёт игра; своих копий логики нет). Строки «ok/FAIL» — ожидание
//   «после»: на сборке «до» FAIL там, где исправление меняет поведение, — это и есть запись «было».
//     asda.Dedup.W2C.Life      — F2: сроки жизни одноразовых эффектов. Снимок (все места вызова + все эффекты удара навыков
//                                из DT_SkillClips): жёсткий срок, срок эмиттеров, хвост частиц, контейнер DT_FxLoop, `NaturalLifeSpan`;
//                                затем РЕАЛЬНЫЙ путь (импакт, столб входа, церемония бонда, метка клика, импакт сердца) —
//                                `InitialLifeSpan` заспавненного актора против его `NaturalLifeSpan`.
//     asda.Dedup.W2C.Fade      — F1/F9: уход соседа через 0,5 с после появления (поверх фейд-ина) — число фейд-компонентов и
//                                «дрожь» материала слота; контроль — уход через 2 с. F9 — имена материалов слотов во время
//                                фейд-ина (поиск секций сборки `face/hair/chst/pant/shoe`) против тех же после фейда.
//     asda.Dedup.W2C.Ride      — F13: звук чужого транспорта на 5/30/50 м — затухание и «не заводить за куполом» против
//                                `PlaySound3D` того же звука в той же точке.
//     asda.Dedup.W2C.FxLoop    — F3: `DT_FxLoop` в корне? переживает `obj gc`?
//     asda.Dedup.W2C.Quality   — хвост F17: уровень качества базы в 0..4; выбор секции `[Группа@N]` не зависит от зажима.
//     asda.Dedup.W2C.HeartSoak — F14: пары соулмейтов на N секунд (пресет Low): таймеры сердец, всего таймеров мира, сердец
//                                выпущено, время игрового потока — по окнам 30 с.
//     asda.Dedup.W2C.Glyph     — F9/F11 (карта логина): превью выбора персонажа офлайн — альфа глифов и имена MID превью
//                                на 0,15/0,5/0,85/2,0 с + снимки кадра в `Reports/dedup_audit2/w2C_shots/`.
//   Все — только офлайн (`-AsdaNoNet`, отказ при живом соединении), последний аргумент `quit` — выход/конец для пакета.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaDataTableCache.h"
#include "AsdaEffectActor.h"
#include "AsdaFxNames.h"
#include "AsdaNetSubsystem.h"
#include "AsdaOutgoingGate.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaSelfTestBatch.h"
#include "AsdaCVarSave.h"   // ★ ревизия В2 дедупа №2: возврат asda.Login.GlyphFade после Glyph
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"                      // GGameThreadTime — время игрового потока кадра (циклы)
#include "Resources/AsdaMaterialNames.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Skill/AsdaSkillClipRow.h"
#include "Skill/AsdaSkillFxSubsystem.h"
#include "Sound/SoundAttenuation.h"
#include "TimerManager.h"
#include "UnrealClient.h"                     // FScreenshotRequest — кадр с UI в файл
#include "Login/AsdaLoginGameMode.h"
#include "Blueprint/UserWidget.h"              // UUserWidget::SetVisibility — окно логина прячется на время снимка
#include "World/AsdaCoord.h"
#include "World/AsdaFadeComponent.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaRenderQualitySubsystemBase.h"
#include "World/AsdaWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDedupW2C, Log, All);

// Друг мира/персонажа/логина: только подача штатных закрытых точек и чтение их состояния (своих копий логики нет).
struct FAsdaDedupW2CTestAccess
{
	using FSoul = UAsdaWorldSubsystem::FAsdaSoulState;   // запись соулмейта мира (кольцо/партнёр/время сердца)
	static TMap<int32, FSoul>& Souls(UAsdaWorldSubsystem* WS) { return WS->SoulByUserIdx; }
	static int32 HeartTimers(const UAsdaWorldSubsystem* WS) { return WS->ActiveSoulHeartTimers; }
	// Штатный тик сердец (в игре его зовёт раз в секунду `TickAoiDespawn`; офлайн этот таймер не взводится — пакетов спауна нет).
	static void TickHearts(UAsdaWorldSubsystem* WS) { WS->TickSoulHearts(); }
	static void EntryFx(UAsdaWorldSubsystem* WS, AActor* A, bool bSelf) { WS->PlayEntryFx(A, bSelf); }
	static void Bond(UAsdaWorldSubsystem* WS, const FString& Partner) { WS->OnSoulmateBond(1, 0, 0, Partner); }
	static void SoulState(UAsdaWorldSubsystem* WS, int32 U, int32 Soul, int32 Partner) { WS->OnSoulmateState(0, U, Soul, Partner); }
	static void Despawn(UAsdaWorldSubsystem* WS, int32 Rid) { WS->OnEntityDespawn(Rid); }
	static TObjectPtr<UAudioComponent>& MountAudio(AAsdaPlayerCharacter* C) { return C->MountAudio; }
	static void PlayRide(AAsdaPlayerCharacter* C, const FString& S) { C->PlayRideSound(S); }

	// Логин: ПОЛНЫЙ штатный путь char-list → перелёт камеры → открытие шкатулки → превью + фейд (как с сервером), без сети.
	//   Данные — те же, что у `RunCharSelectOfflineDemo` (условный персонаж в слоте 0; слоты 1–2 пустые — глифы).
	static bool StartCharSelectFlow(AAsdaLoginGameMode* GM, int32 FrontSlot)
	{
		UGameInstance* GI = GM ? GM->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { return false; }
		Net->CharList.SetNum(3);
		for (int32 i = 0; i < 3; ++i) { Net->CharList[i] = FAsdaCharRecord(); }
		FAsdaCharRecord& R = Net->CharList[0];
		R.Slot = 0; R.RawSlotId = 10; R.Name = TEXT("GM_sitxovski"); R.Job = 3; R.Level = 17; R.Exp = 0;
		R.HpMax = R.HpCur = 3270; R.MpMax = R.MpCur = 169; R.Stats = { 134, 206, 119, 146, 86, 206 };
		R.Constellation = 4;
		GM->SelectedSlot = FMath::Clamp(FrontSlot, 0, 2);   // слот впереди карусели (1 — пустой: глиф в кадре)
		if (GM->LoginWidget) { GM->LoginWidget->SetVisibility(ESlateVisibility::Collapsed); }
		GM->OnCharListReady();
		return true;
	}
	static const TArray<TObjectPtr<AActor>>& Glyphs(AAsdaLoginGameMode* GM) { return GM->GlyphActors; }
	static const TArray<TObjectPtr<AActor>>& Chars(AAsdaLoginGameMode* GM) { return GM->CharacterActors; }
	static float FadeTime(AAsdaLoginGameMode* GM) { return GM->PreviewFadeTime; }
};

namespace AsdaDedupW2C
{
	static const TCHAR* kP = TEXT("САМОТЕСТ-В2В");   // префикс строк лога

	// Счёт проверок прогона.
	struct FTally
	{
		int32 Ok = 0, Fail = 0;
		void Expect(bool b, const FString& What)
		{
			(b ? Ok : Fail) += 1;
			UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s %s %s"), kP, b ? TEXT("ok") : TEXT("FAIL"), *What);
		}
	};

	// Игровой мир и GI (консольный мир бывает редакторным — берём игровой контекст).
	static UWorld* GameWorld()
	{
		if (!GEngine) { return nullptr; }
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.OwningGameInstance && Ctx.World() && Ctx.World()->IsGameWorld()) { return Ctx.World(); }
		}
		return nullptr;
	}

	// Разбор хвоста команды: `quit` — флаг, прочие — по порядку.
	static TArray<FString> Parse(const TArray<FString>& Args, bool& bQuit)
	{
		TArray<FString> Out;
		bQuit = false;
		for (const FString& A : Args) { if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; } else { Out.Add(A); } }
		return Out;
	}

	static void Finish(const TCHAR* Name, const FTally& T, bool bQuit, UWorld* W)
	{
		UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s %s ИТОГ: ok=%d fail=%d"), kP, Name, T.Ok, T.Fail);
		// ★ Ревизия В2 дедупа №2 (11.10.2026): Glyph подменяет `asda.Login.GlyphFade` — вернуть значение и приоритет (с 11.10
		//   умолчание 1 по решению владельца; прежде самотест оставлял 0 «из консоли» до конца процесса).
		if (FCString::Strcmp(Name, TEXT("Glyph")) == 0) { AsdaCVarSave::RestoreAll(TEXT("asda.Dedup.W2C.Glyph")); }
		if (bQuit) { AsdaSelfTestBatch::Quit(W); }
	}

	// Шаги по времени (реальные секунды) — тикер ядра; каждый шаг — лямбда.
	struct FSeq : public TSharedFromThis<FSeq>
	{
		TArray<TPair<double, TFunction<void()>>> Steps;   // (время от старта, действие)
		double T0 = 0.0;
		int32 Idx = 0;
		void Add(double At, TFunction<void()> Fn) { Steps.Add({ At, MoveTemp(Fn) }); }
		void Run()
		{
			Steps.StableSort([](const TPair<double, TFunction<void()>>& A, const TPair<double, TFunction<void()>>& B) { return A.Key < B.Key; });
			T0 = 0.0;
			TSharedRef<FSeq> Self = AsShared();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self](float Dt) -> bool
			{
				// Время шагов — сумма шагов тикера с потолком 0,1 с на кадр: рывок загрузки (спавн десятков эффектов
				//   в одном кадре) не «съедает» паузы между шагами (иначе мир не успевал прожить кадры между ними).
				Self->T0 += FMath::Min((double)Dt, 0.1);
				const double Now = Self->T0;
				while (Self->Steps.IsValidIndex(Self->Idx) && Now >= Self->Steps[Self->Idx].Key)
				{
					TFunction<void()> Fn = MoveTemp(Self->Steps[Self->Idx].Value);
					++Self->Idx;
					Fn();
				}
				return Self->Steps.IsValidIndex(Self->Idx);
			}));
		}
	};

	// Ловушка новых эффект-акторов мира на время проверки.
	struct FSpawnCatch
	{
		TWeakObjectPtr<UWorld> W;
		FDelegateHandle H;
		TArray<TWeakObjectPtr<AActor>> Got;
		void Arm(UWorld* InW)
		{
			W = InW;
			H = InW->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* A) { Got.Add(A); }));
		}
		void Disarm() { if (UWorld* X = W.Get()) { X->RemoveOnActorSpawnedHandler(H); } H.Reset(); }
		// Последний пойманный актор, удовлетворяющий условию.
		AActor* Last(TFunctionRef<bool(AActor*)> Pred) const
		{
			for (int32 i = Got.Num() - 1; i >= 0; --i) { if (AActor* A = Got[i].Get()) { if (Pred(A)) { return A; } } }
			return nullptr;
		}
	};

	// Свой павн и тайл (офлайн-мир alpen его даёт; иначе — штатный крюк, как у самотеста 1А).
	static FIntPoint EnsureSelf(UWorld* W, UAsdaWorldSubsystem* WS)
	{
		if (!::IsValid(WS->LocalPlayerActor.Get()) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
		}
		FIntPoint MyTile(256, 256);
		if (const AActor* Me = WS->LocalPlayerActor.Get()) { MyTile = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(Me->GetActorLocation())); }
		return MyTile;
	}

	// ── F2: срок жизни эффекта, как его видит рантайм ──
	struct FLife
	{
		float Emit = 0.f;      // max(задержка + период) эмиттеров
		float Tail = 0.f;      // + жизнь частицы (база + разброс) у частичных эмиттеров — когда гаснет последняя частица
		float CDur = 0.f;      // контейнер DT_FxLoop
		int32 CEnd = -1;       // режим конца контейнера (−1 — нет строки/ключ выключен)
		float Natural = 0.f;   // AAsdaEffectActor::NaturalLifeSpan()
	};
	static FLife Measure(const AAsdaEffectActor* Fx)
	{
		FLife L;
		for (const FAsdaFxEmitter& E : Fx->Emitters)
		{
			const float End = E.StartDelay + E.Period;
			L.Emit = FMath::Max(L.Emit, End);
			const bool bParticle = !(E.bMeshEmitter || E.bSingleQuad || E.bRing);
			L.Tail = FMath::Max(L.Tail, bParticle ? End + E.LifeBase + E.LifeVar : End);
		}
		if (!Fx->PeekLoopContainer(L.CDur, L.CEnd)) { L.CDur = 0.f; L.CEnd = -1; }
		L.Natural = Fx->NaturalLifeSpan();
		return L;
	}
	static FString Fmt(const FLife& L)
	{
		return FString::Printf(TEXT("эмиттеры %.2f, хвост частиц %.2f, контейнер %.2f/%d, natural %.2f"), L.Emit, L.Tail, L.CDur, L.CEnd, L.Natural);
	}

	static void RunLife(const TArray<FString>& Args)
	{
		bool bQuit = false; Parse(Args, bQuit);
		UWorld* W = GameWorld();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!WS) { FTally T; T.Expect(false, TEXT("Life: нет игрового мира")); Finish(TEXT("Life"), T, bQuit, W); return; }
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(GI, TEXT("asda.Dedup.W2C.Life"), KitArgs)) { return; }
		TSharedRef<FTally> T = MakeShared<FTally>();

		// ── 1) СНИМОК: жёсткие сроки мест вызова против того, что даёт эффект сам ──
		const FVector Far(0.f, 0.f, -200000.f);   // далеко под картой: не виден, не светит
		struct FSite { const TCHAR* Site; FString Name; bool bBp; float Hard; };
		TArray<FSite> Sites = {
			{ TEXT("удар соул-линка (сердце→импакт)"), TEXT("eff_1_char_99_soullink03-a_00"), false, 1.5f },
			{ TEXT("церемония бонда 5203"),            TEXT("eff_1_char_99_soulmatec-a_01"),  false, 3.5f },
			{ TEXT("столб входа 4056 (контроль)"),    kAsdaEntryFxJeb,                       false, 5.0f },
			{ TEXT("импакт моба"),                    TEXT("BP_ImpactMob"),                  true,  1.2f },
			{ TEXT("импакт удара 01"),                TEXT("BP_ImpactBattk01"),              true,  1.2f },
			{ TEXT("импакт удара 02"),                TEXT("BP_ImpactBattk02"),              true,  1.2f },
			{ TEXT("импакт удара 03"),                TEXT("BP_ImpactBattk03"),              true,  1.2f },
			{ TEXT("импакт удара 04"),                TEXT("BP_ImpactBattk04"),              true,  1.2f },
			{ TEXT("метка клика"),                    AsdaFxBp::kClickMarker,                true,  1.1f },
		};
		for (const FSite& S : Sites)
		{
			AAsdaEffectActor* Fx = S.bBp ? AAsdaEffectActor::SpawnByBPName(W, S.Name, Far, FRotator::ZeroRotator, nullptr)
			                             : AAsdaEffectActor::SpawnByJebName(W, S.Name, Far, FRotator::ZeroRotator, nullptr);
			if (!Fx) { T->Expect(false, FString::Printf(TEXT("LIFE снимок %s: эффект %s не заспавнен"), S.Site, *S.Name)); continue; }
			const FLife L = Measure(Fx);
			UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s LIFE снимок «%s» %s: жёстко %.2f | %s | natural−жёстко %+.2f"),
				kP, S.Site, *S.Name, S.Hard, *Fmt(L), L.Natural - S.Hard);
			Fx->Destroy();
		}
		// Эффекты УДАРА навыков (FxHit0..2 всех строк DT_SkillClips), жёсткий срок 4,0 с.
		{
			int32 NRows = 0, NFx = 0, NLonger = 0, NShorter = 0, NEqual = 0, NMissing = 0, NTailBeyond = 0;
			float MaxCut = 0.f, MaxShort = 0.f;
			FString MaxCutName, MaxShortName;
			TSet<FString> Seen;
			if (const UDataTable* Dt = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/DT_SkillClips.DT_SkillClips")))
			{
				for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
				{
					const FAsdaSkillClipRow* R = reinterpret_cast<const FAsdaSkillClipRow*>(It.Value);
					if (!R) { continue; }
					++NRows;
					for (const FString* Cell : { &R->FxHit0, &R->FxHit1, &R->FxHit2 })
					{
						TArray<FAsdaSkillFxSpawn> L;
						UAsdaSkillFxSubsystem::ParseFxList(*Cell, L);
						for (const FAsdaSkillFxSpawn& S : L)
						{
							if (S.Anchor == FName(TEXT("projectile")) || Seen.Contains(S.Jeb)) { continue; }
							Seen.Add(S.Jeb);
							AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnByJebName(W, S.Jeb, Far, FRotator::ZeroRotator, nullptr);
							if (!Fx) { ++NMissing; continue; }
							++NFx;
							const FLife Lf = Measure(Fx);
							const float D = Lf.Natural - 4.0f;
							if (FMath::Abs(D) < 0.005f) { ++NEqual; }
							else if (D > 0.f) { ++NLonger; if (D > MaxCut) { MaxCut = D; MaxCutName = S.Jeb; } }
							else { ++NShorter; if (-D > MaxShort) { MaxShort = -D; MaxShortName = S.Jeb; } }
							if (Lf.Tail > Lf.Natural + 0.01f) { ++NTailBeyond; }
							UE_LOG(LogAsdaDedupW2C, Log, TEXT("%s LIFE удар %s: жёстко 4.00 | %s"), kP, *S.Jeb, *Fmt(Lf));
							Fx->Destroy();
						}
					}
				}
			}
			UE_LOG(LogAsdaDedupW2C, Display,
				TEXT("%s LIFE снимок ударов навыков: строк %d, эффектов %d (нет BP %d); natural > 4.0 (сейчас обрезаются) %d (макс +%.2f у %s); ")
				TEXT("natural < 4.0 %d (макс −%.2f у %s); равно %d; хвост частиц длиннее natural %d"),
				kP, NRows, NFx, NMissing, NLonger, MaxCut, *MaxCutName, NShorter, MaxShort, *MaxShortName, NEqual, NTailBeyond);
			T->Expect(NFx > 0, FString::Printf(TEXT("LIFE снимок ударов навыков: эффекты найдены (%d)"), NFx));
		}

		// ── 2) РЕАЛЬНЫЙ ПУТЬ: срок, который ставит игра, против `NaturalLifeSpan` того же актора ──
		const FIntPoint MyTile = EnsureSelf(W, WS);
		const FString PeerName = TEXT("В2Впир");
		WS->DebugSpawnFakePeer(7301, PeerName, AsdaCoord::TileCenterGame(MyTile.X + 2, MyTile.Y));
		WS->DebugSpawnFakePeer(7302, TEXT("В2Впара1"), AsdaCoord::TileCenterGame(MyTile.X - 2, MyTile.Y + 2));
		WS->DebugSpawnFakePeer(7303, TEXT("В2Впара2"), AsdaCoord::TileCenterGame(MyTile.X - 2, MyTile.Y + 5));
		TSharedRef<FSpawnCatch> Catch = MakeShared<FSpawnCatch>();
		Catch->Arm(W);
		auto Check = [T](const TCHAR* What, AActor* A, float Hard)
		{
			AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(A);
			if (!Fx) { T->Expect(false, FString::Printf(TEXT("LIFE путь «%s»: эффект не пойман"), What)); return; }
			const float Natural = Fx->NaturalLifeSpan();
			const float Set = Fx->InitialLifeSpan;
			UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s LIFE путь «%s» %s: поставлен срок %.2f (жёсткий был %.2f, natural %.2f)"),
				kP, What, *Fx->GetClass()->GetName(), Set, Hard, Natural);
			T->Expect(FMath::IsNearlyEqual(Set, Natural, 0.01f),
				FString::Printf(TEXT("LIFE путь «%s»: срок = NaturalLifeSpan (%.2f против %.2f)"), What, Set, Natural));
		};
		TSharedRef<FSeq> Q = MakeShared<FSeq>();
		TWeakObjectPtr<UWorld> WW = W;
		TWeakObjectPtr<UAsdaWorldSubsystem> WWS = WS;
		Q->Add(1.0, [WW, WWS, Catch, Check, PeerName]()
		{
			UWorld* X = WW.Get(); UAsdaWorldSubsystem* S = WWS.Get();
			if (!X || !S) { return; }
			AActor* Peer = S->FindPlayerActorByName(PeerName);
			const FVector At = Peer ? Peer->GetActorLocation() : FVector::ZeroVector;
			// Импакты обычного удара/моба — штатная `SpawnImpact`.
			for (const TCHAR* Bp : { TEXT("BP_ImpactMob"), TEXT("BP_ImpactBattk01"), TEXT("BP_ImpactBattk02"), TEXT("BP_ImpactBattk03"), TEXT("BP_ImpactBattk04") })
			{
				S->SpawnImpact(Bp, At);
				const FString Want = FString(Bp) + TEXT("_C");
				Check(Bp, Catch->Last([&Want](AActor* A) { return A->GetClass()->GetName() == Want; }), 1.2f);
			}
			// Столб входа соседа — штатная `PlayEntryFx` (контроль: срок 5.0 = 4.5 + 0.5).
			if (Peer)
			{
				FAsdaDedupW2CTestAccess::EntryFx(S, Peer, false);
				Check(TEXT("столб входа (контроль)"), Catch->Last([](AActor* A) { return A->ActorHasTag(FName(AsdaCharFxTags::kEntry)); }), 5.0f);
				// Церемония бонда 5203 — штатный приёмник (у своего и у партнёра).
				FAsdaDedupW2CTestAccess::Bond(S, PeerName);
				Check(TEXT("церемония бонда"), Catch->Last([](AActor* A) { return A->ActorHasTag(FName(TEXT("SoulBondFx"))); }), 3.5f);
			}
			else { Check(TEXT("столб входа (контроль)"), nullptr, 5.0f); }
			// Метка клика — штатная точка контроллера.
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(X->GetFirstPlayerController()))
			{
				PC->SpawnClickMarker(At);
				const FString Want = FString(AsdaFxBp::kClickMarker) + TEXT("_C");
				Check(TEXT("метка клика"), Catch->Last([&Want](AActor* A) { return A->GetClass()->GetName() == Want; }), 1.1f);
			}
			// Сердце пары 7302→7303 (3 тайла ≈ 1 с полёта) — штатный тик мира; импакт ловится по тегу ниже.
			FAsdaDedupW2CTestAccess::SoulState(S, 7302, 1, 7303);
			FAsdaDedupW2CTestAccess::Souls(S).FindChecked(7302).NextHeartS = 0.0;
			FAsdaDedupW2CTestAccess::TickHearts(S);
		});
		// Импакт сердца живёт (до правки) лишь 1,5 с — срок снимается, пока актор жив: опрос каждые 0,05 с.
		TSharedRef<TPair<float, float>> HeartRec = MakeShared<TPair<float, float>>(-1.f, -1.f);   // (поставленный срок, natural)
		for (double At = 1.05; At < 4.0; At += 0.05)
		{
			Q->Add(At, [Catch, HeartRec]()
			{
				if (HeartRec->Key >= 0.f) { return; }
				if (AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(Catch->Last([](AActor* A) { return A->ActorHasTag(FName(TEXT("SoulImpactFx"))); })))
				{
					HeartRec->Key = Fx->InitialLifeSpan;
					HeartRec->Value = Fx->NaturalLifeSpan();
				}
			});
		}
		Q->Add(4.0, [WW, WWS, Catch, T, bQuit, HeartRec]()
		{
			UAsdaWorldSubsystem* S = WWS.Get();
			if (S) { FAsdaDedupW2CTestAccess::SoulState(S, 7302, -1, -1); }   // сердца больше не нужны
			if (HeartRec->Key < 0.f) { T->Expect(false, TEXT("LIFE путь «импакт сердца соул-линка»: эффект не пойман")); }
			else
			{
				UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s LIFE путь «импакт сердца соул-линка»: поставлен срок %.2f (жёсткий был 1.50, natural %.2f)"),
					kP, HeartRec->Key, HeartRec->Value);
				T->Expect(FMath::IsNearlyEqual(HeartRec->Key, HeartRec->Value, 0.01f),
					FString::Printf(TEXT("LIFE путь «импакт сердца соул-линка»: срок = NaturalLifeSpan (%.2f против %.2f)"), HeartRec->Key, HeartRec->Value));
			}
			Catch->Disarm();
			if (S) { for (int32 Rid : { 7301, 7302, 7303 }) { FAsdaDedupW2CTestAccess::Despawn(S, Rid); } }
			Finish(TEXT("Life"), *T, bQuit, WW.Get());
		});
		Q->Run();
	}

	// ── F1/F9: фейды соседа ──
	static int32 CountFades(AActor* A)
	{
		TArray<UAsdaFadeComponent*> F;
		if (A) { A->GetComponents<UAsdaFadeComponent>(F); }
		return F.Num();
	}
	// Слоты базового тела, которые система сборки находит по имени материала (тот же предикат `GetName().Contains`).
	static int32 CountNamedSections(AAsdaPlayerCharacter* C, FString& Names)
	{
		int32 N = 0;
		USkeletalMeshComponent* Sk = C ? C->BaseSkel.Get() : nullptr;
		if (!Sk) { return -1; }
		for (int32 i = 0; i < Sk->GetNumMaterials(); ++i)
		{
			UMaterialInterface* M = Sk->GetMaterial(i);
			const FString Nm = M ? M->GetName() : TEXT("-");
			Names += FString::Printf(TEXT("%s[%d]=%s"), Names.IsEmpty() ? TEXT("") : TEXT(" "), i, *Nm);
			for (const TCHAR* Key : { TEXT("face"), TEXT("hair"), TEXT("chst"), TEXT("pant"), TEXT("shoe") })
			{
				if (Nm.Contains(Key)) { ++N; break; }
			}
		}
		return N;
	}

	static void RunFade(const TArray<FString>& Args)
	{
		bool bQuit = false; Parse(Args, bQuit);
		UWorld* W = GameWorld();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!WS) { FTally T; T.Expect(false, TEXT("Fade: нет игрового мира")); Finish(TEXT("Fade"), T, bQuit, W); return; }
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(GI, TEXT("asda.Dedup.W2C.Fade"), KitArgs)) { return; }
		TSharedRef<FTally> T = MakeShared<FTally>();
		const FIntPoint MyTile = EnsureSelf(W, WS);
		TWeakObjectPtr<UAsdaWorldSubsystem> WWS = WS;
		TWeakObjectPtr<UWorld> WW = W;
		struct FPeer { int32 Rid; float LeaveAt; TWeakObjectPtr<AAsdaPlayerCharacter> A; TSet<UMaterialInterface*> SlotMats; int32 FadesAfterLeave = -1; };
		TSharedRef<TArray<FPeer>> Peers = MakeShared<TArray<FPeer>>();
		Peers->Add({ 7401, 0.5f });   // уход поверх идущего фейд-ина
		Peers->Add({ 7402, 2.0f });   // контроль: фейд-ин закончился
		TSharedRef<FSeq> Q = MakeShared<FSeq>();
		const double Base = 0.5;
		Q->Add(0.0, [WWS, Peers, MyTile]()
		{
			UAsdaWorldSubsystem* S = WWS.Get(); if (!S) { return; }
			int32 k = 0;
			for (FPeer& P : *Peers)
			{
				S->DebugSpawnFakePeer(P.Rid, FString::Printf(TEXT("В2Вфейд%d"), P.Rid), AsdaCoord::TileCenterGame(MyTile.X + 2 + 2 * k++, MyTile.Y + 1));
				P.A = Cast<AAsdaPlayerCharacter>(S->GetPlayerActorByUserNum(P.Rid));
			}
			S->DebugSpawnFakePeer(7403, TEXT("В2Вимена"), AsdaCoord::TileCenterGame(MyTile.X + 2, MyTile.Y + 4));
		});
		// F9: имена слотов во время фейд-ина (0,5 с) и после него (2,2 с) у соседа 7403.
		TSharedRef<FString> DuringNames = MakeShared<FString>();
		TSharedRef<int32> During = MakeShared<int32>(-1);
		Q->Add(0.5, [WWS, During, DuringNames]()
		{
			UAsdaWorldSubsystem* S = WWS.Get(); if (!S) { return; }
			AAsdaPlayerCharacter* C = Cast<AAsdaPlayerCharacter>(S->GetPlayerActorByUserNum(7403));
			*During = CountNamedSections(C, *DuringNames);
			UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s FADE F9 во время фейд-ина (фейд-компонентов %d): найдено секций по имени %d | %s"),
				kP, CountFades(C), *During, **DuringNames);
		});
		for (int32 i = 0; i < Peers->Num(); ++i)
		{
			const float At = (*Peers)[i].LeaveAt;
			// Уход: штатный деспавн 4011 → сразу число фейд-компонентов; затем 8 замеров материала слота 0 по 0,03 с.
			Q->Add(At, [WWS, Peers, i]()
			{
				UAsdaWorldSubsystem* S = WWS.Get(); if (!S) { return; }
				FPeer& P = (*Peers)[i];
				FAsdaDedupW2CTestAccess::Despawn(S, P.Rid);
				P.FadesAfterLeave = CountFades(P.A.Get());
			});
			for (int32 k = 1; k <= 8; ++k)
			{
				Q->Add(At + 0.03 * k, [Peers, i]()
				{
					FPeer& P = (*Peers)[i];
					AAsdaPlayerCharacter* C = P.A.Get();
					USkeletalMeshComponent* Sk = C ? C->BaseSkel.Get() : nullptr;
					if (Sk && Sk->GetNumMaterials() > 0) { P.SlotMats.Add(Sk->GetMaterial(0)); }
				});
			}
		}
		Q->Add(2.6, [WWS, Peers, T, During, DuringNames, bQuit, WW]()
		{
			UAsdaWorldSubsystem* S = WWS.Get();
			for (const FPeer& P : *Peers)
			{
				UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s FADE F1 уход через %.1f с после появления: фейд-компонентов сразу после ухода %d, разных материалов слота 0 за 8 кадров %d"),
					kP, P.LeaveAt, P.FadesAfterLeave, P.SlotMats.Num());
				T->Expect(P.FadesAfterLeave == 1, FString::Printf(TEXT("FADE F1 уход через %.1f с: один фейд-компонент (%d)"), P.LeaveAt, P.FadesAfterLeave));
				T->Expect(P.SlotMats.Num() <= 1, FString::Printf(TEXT("FADE F1 уход через %.1f с: материал слота не пересоздаётся каждый кадр (%d)"), P.LeaveAt, P.SlotMats.Num()));
			}
			AAsdaPlayerCharacter* C = S ? Cast<AAsdaPlayerCharacter>(S->GetPlayerActorByUserNum(7403)) : nullptr;
			FString AfterNames;
			const int32 After = CountNamedSections(C, AfterNames);
			UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s FADE F9 после фейд-ина (фейд-компонентов %d): найдено секций по имени %d | %s"), kP, CountFades(C), After, *AfterNames);
			T->Expect(After > 0, FString::Printf(TEXT("FADE F9 контроль: после фейда секции по имени находятся (%d)"), After));
			T->Expect(*During == After, FString::Printf(TEXT("FADE F9 во время фейд-ина секции находятся так же, как после (%d против %d)"), *During, After));
			if (S) { FAsdaDedupW2CTestAccess::Despawn(S, 7403); }
			Finish(TEXT("Fade"), *T, bQuit, WW.Get());
		});
		(void)Base;
		Q->Run();
	}

	// ── F13: звук чужого транспорта ──
	static FString AttDesc(UAudioComponent* AC, float Dist, float& OutGain)
	{
		OutGain = -1.f;
		if (!AC) { return TEXT("компонента нет"); }
		const FSoundAttenuationSettings* S = AC->GetAttenuationSettingsToApply();
		if (!S || !S->bAttenuate) { OutGain = 1.f; return FString::Printf(TEXT("затухания НЕТ (играет=%d)"), AC->IsPlaying() ? 1 : 0); }
		const float Mn = S->AttenuationShapeExtents.X;
		const float Fall = S->FalloffDistance;
		const float t = Fall > 0.f ? FMath::Clamp((Dist - Mn) / Fall, 0.f, 1.f) : 0.f;
		OutGain = (Dist <= Mn) ? 1.f : S->CustomAttenuationCurve.GetRichCurveConst()->Eval(t);
		return FString::Printf(TEXT("затухание: мин %.0f, спад %.0f, алгоритм %d, усиление на %.0f см = %.4f (играет=%d)"),
			Mn, Fall, (int32)S->DistanceAlgorithm, Dist, OutGain, AC->IsPlaying() ? 1 : 0);
	}

	static void RunRide(const TArray<FString>& Args)
	{
		bool bQuit = false; Parse(Args, bQuit);
		UWorld* W = GameWorld();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
		if (!WS || !Snd) { FTally T; T.Expect(false, TEXT("Ride: нет мира/звука")); Finish(TEXT("Ride"), T, bQuit, W); return; }
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(GI, TEXT("asda.Dedup.W2C.Ride"), KitArgs)) { return; }
		TSharedRef<FTally> T = MakeShared<FTally>();
		EnsureSelf(W, WS);
		TSharedRef<FSeq> Q = MakeShared<FSeq>();
		TWeakObjectPtr<UAsdaWorldSubsystem> WWS = WS;
		TWeakObjectPtr<UAsdaSoundSubsystem> WSnd = Snd;
		TWeakObjectPtr<UWorld> WW = W;
		// ★ 15.10.2026 (волна mobpos_swing, разбор регрессии): точку слуха читаем ПОСЛЕ кадра игры. Слушатель «в персонаже»
		//   (`asda.Sound.ListenerAtPlayer`) ставит `AAsdaPlayerController::PlayerTick`; если `EnsureSelf` только что заспавнил
		//   своего персонажа (самотест первым в пакете), тика ещё не было и `GetAudioListenerPosition` отдавал КАМЕРУ — соседи
		//   вставали на 5/30/50 м от камеры, а мерились 30.0/42.1/58.1 м (камера в 29.6 м). Вторым в том же пакете тест
		//   проходил 5/0 — дефект самотеста, не порта.
		TSharedRef<FVector> EarRef = MakeShared<FVector>(FVector::ZeroVector);
		Q->Add(0.3, [WWS, WW, EarRef]()
		{
			UWorld* Wd = WW.Get(); UAsdaWorldSubsystem* S = WWS.Get();
			APlayerController* PC = Wd ? Wd->GetFirstPlayerController() : nullptr;
			if (!S) { return; }
			FVector Ear = FVector::ZeroVector;
			if (PC && !PC->GetAudioListenerAttenuationOverridePosition(Ear)) { FVector F, R; PC->GetAudioListenerPosition(Ear, F, R); }
			*EarRef = Ear;
			const float Meters[] = { 5.f, 30.f, 50.f };
			for (int32 i = 0; i < 3; ++i)
			{
				const int32 Rid = 7501 + i;
				S->DebugSpawnFakePeer(Rid, FString::Printf(TEXT("В2Вконь%d"), Rid), AsdaCoord::UeToGame2D(Ear + FVector(Meters[i] * 100.f, 0.f, 0.f)));
			}
		});
		Q->Add(1.3, [WWS, WSnd, WW, T, EarRef, bQuit]()
		{
			UAsdaWorldSubsystem* S = WWS.Get(); UAsdaSoundSubsystem* Sn = WSnd.Get();
			if (!S || !Sn) { return; }
			const FVector Ear = *EarRef;
			const FString Name = kAsdaEntrySound;   // любой импортированный звук: проверяется канал, а не запись
			for (int32 i = 0; i < 3; ++i)
			{
				AAsdaPlayerCharacter* C = Cast<AAsdaPlayerCharacter>(S->GetPlayerActorByUserNum(7501 + i));
				if (!C) { T->Expect(false, FString::Printf(TEXT("RIDE сосед %d не заспавнен"), 7501 + i)); continue; }
				TObjectPtr<UAudioComponent>& MA = FAsdaDedupW2CTestAccess::MountAudio(C);
				if (!MA)
				{
					// Канал создаётся при посадке (`SetMount`) — здесь тем же набором полей; под проверкой только проигрыш.
					MA = NewObject<UAudioComponent>(C, TEXT("MountAudio"));
					MA->SetupAttachment(C->GetRootComponent());
					MA->bAutoActivate = false;
					MA->bStopWhenOwnerDestroyed = true;
					MA->SetVolumeMultiplier(0.6f);
					MA->RegisterComponent();
				}
				MA->SetSound(nullptr);   // «канал заведён» = звук поставлен (без аудиоустройства `-nosound` IsPlaying не показателен)
				FAsdaDedupW2CTestAccess::PlayRide(C, Name);
				const float Dist = FVector::Dist(Ear, C->GetActorLocation());
				float GRide = 0.f;
				const FString DRide = AttDesc(MA, Dist, GRide);
				// НЕЗАВИСИМЫЙ ОРАКУЛ (не код порта): FMOD3 inverse rolloff 0.7, min/max 100/10000 (эталон `CSoundEx_ctor`), max
				//   ограничен куполом `asda.Sound.HearingRadiusM`; за куполом при `SilenceBeyondMax 1` источник не заводится.
				const IConsoleVariable* CvR = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Sound.HearingRadiusM"));
				const IConsoleVariable* CvS = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Sound.SilenceBeyondMax"));
				// ★ 30.10.2026 (3D-звук по классам): у транспорта свой радиус — громкость источника 75 дБ(А) на 1 м до фона 45 дБ(А)
				//   по закону −6 дБ на удвоение: 10^(30/20) = 31,6 м; внутренний 1 м; ед./м — рост модели / 1,72 м (замер самотеста
				//   asda.SelfTest.Sound3D). Купол сверху. При `asda.Sound.Classes 0` — прежний единый закон 100/купол.
				const IConsoleVariable* CvC = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Sound.Classes"));
				const bool bClasses = !CvC || CvC->GetInt() != 0;
				const float Upm = UAsdaSoundSubsystem::UnitsPerMeter();
				const float Mn = bClasses ? Upm : 100.f;
				const float Dome = (CvR && CvR->GetFloat() > 0.f) ? CvR->GetFloat() * Upm : 1e9f;
				const float Mx = bClasses ? FMath::Max(Mn + 1.f, FMath::Min(FMath::Pow(10.f, 30.f / 20.f) * Upm, Dome))
				                          : FMath::Min(10000.f, FMath::Max(Mn + 1.f, Dome));
				const bool bSil = CvS && CvS->GetInt() != 0;
				const bool bWantStart = !(bSil && Dist > Mx);
				const float WantGain = Dist <= Mn ? 1.f : Mn / (Mn + 0.7f * (Dist - Mn));
				const bool bStarted = MA->Sound != nullptr;
				UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s RIDE %.1f м: транспорт — %s, заведён=%d | эталон FMOD3: купол %.0f см, заводить=%d, усиление %.4f"),
					kP, Dist / 100.f, *DRide, bStarted ? 1 : 0, Mx, bWantStart ? 1 : 0, WantGain);
				T->Expect(bStarted == bWantStart, FString::Printf(TEXT("RIDE %.1f м: канал заведён/не заведён как у 3D-звука с куполом (%d, ждали %d)"), Dist / 100.f, bStarted ? 1 : 0, bWantStart ? 1 : 0));
				if (bWantStart && Dist < 0.9f * Mx)
				{
					// Допуск 3 % — кусочно-линейная кривая из 25 ключей (≤0,2 дБ по разбору PlaySound3D).
					T->Expect(GRide >= 0.f && FMath::Abs(GRide - WantGain) <= 0.03f * WantGain + 1e-4f,
						FString::Printf(TEXT("RIDE %.1f м: усиление по закону FMOD3 (%.4f, ждали %.4f)"), Dist / 100.f, GRide, WantGain));
				}
				MA->Stop();
			}
			for (int32 i = 0; i < 3; ++i) { FAsdaDedupW2CTestAccess::Despawn(S, 7501 + i); }
			Finish(TEXT("Ride"), *T, bQuit, WW.Get());
		});
		Q->Run();
	}

	// ── F3: удержание DT_FxLoop ──
	static void RunFxLoop(const TArray<FString>& Args)
	{
		bool bQuit = false; Parse(Args, bQuit);
		UWorld* W = GameWorld();
		FTally T;
		if (!W) { T.Expect(false, TEXT("FxLoop: нет игрового мира")); Finish(TEXT("FxLoop"), T, bQuit, W); return; }
		static const TCHAR* Path = TEXT("/Game/AsdaAssets/Data/DT_FxLoop.DT_FxLoop");
		// Эффект — штатный потребитель таблицы (контейнер читается при спавне).
		if (AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnByJebName(W, TEXT("eff_1_char_99_soulmatec-a_01"), FVector(0, 0, -200000), FRotator::ZeroRotator, nullptr))
		{
			float D = 0.f; int32 E = 0;
			Fx->PeekLoopContainer(D, E);
			Fx->Destroy();
		}
		UDataTable* Dt = FindObject<UDataTable>(nullptr, Path);
		const bool bRooted = Dt && Dt->IsRooted();
		UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s FXLOOP таблица в памяти=%d, в корне (AddToRoot)=%d"), kP, Dt ? 1 : 0, bRooted ? 1 : 0);
		const TWeakObjectPtr<UDataTable> Before = Dt;
		GEngine->Exec(W, TEXT("obj gc"));
		UDataTable* After = FindObject<UDataTable>(nullptr, Path);
		const bool bSame = Before.IsValid() && Before.Get() == After;
		UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s FXLOOP после obj gc: в памяти=%d, тот же объект=%d"), kP, After ? 1 : 0, bSame ? 1 : 0);
		T.Expect(Dt != nullptr, TEXT("FXLOOP таблица загружена эффектом"));
		T.Expect(!bRooted, TEXT("FXLOOP таблица НЕ в корне (запрет AsdaDataTableCache.h: переимпорт в редакторе обязан доезжать)"));
		T.Expect(bSame, TEXT("FXLOOP таблица пережила obj gc (удержание на сессию)"));
		Finish(TEXT("FxLoop"), T, bQuit, W);
	}

	// ── F17-хвост: уровень качества ──
	static void RunQuality(const TArray<FString>& Args)
	{
		bool bQuit = false; Parse(Args, bQuit);
		FTally T;
		struct FKey { const TCHAR* G; const TCHAR* K; };
		const FKey Keys[] = {
			{ TEXT("AsdaFxLight"), TEXT("Light.FxBudget") }, { TEXT("AsdaFxLight"), TEXT("Light.FxScreenBudget") },
			{ TEXT("AsdaFxLight"), TEXT("Light.FxRadiusCap") }, { TEXT("AsdaFxLight"), TEXT("Light.FxPerEffect") },
			{ TEXT("AsdaShadow"), TEXT("Light.ShadowBudget") }, { TEXT("AsdaShadow"), TEXT("Light.FxShadowResScale") },
			{ TEXT("AsdaQuality"), TEXT("Normals.Enable") }, { TEXT("ShadowQuality"), TEXT("r.Shadow.DistanceScale") },
			{ TEXT("ShadowQuality"), TEXT("r.LightMaxDrawDistanceScale") },
		};
		int32 N = 0, Bad = 0;
		for (const FKey& K : Keys)
		{
			for (int32 L = -3; L <= 8; ++L)
			{
				const FString A = UAsdaRenderQualitySubsystemBase::ScalabilityLevelValue(K.G, L, K.K, TEXT("<нет>"));
				const FString B = UAsdaRenderQualitySubsystemBase::ScalabilityLevelValue(K.G, FMath::Clamp(L, 0, 4), K.K, TEXT("<нет>"));
				++N;
				if (A != B) { ++Bad; UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s QUALITY [%s@%d] %s: «%s» ≠ зажатому «%s»"), kP, K.G, L, K.K, *A, *B); }
			}
		}
		T.Expect(Bad == 0, FString::Printf(TEXT("QUALITY секция уровня не зависит от зажима 0..4 (%d сравнений, расхождений %d)"), N, Bad));
		IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.EffectsQuality"));
		IConsoleVariable* Cs = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.ShadowQuality"));
		const int32 Fx = UAsdaRenderQualitySubsystemBase::EffectsQualityLevel();
		const int32 Sh = UAsdaRenderQualitySubsystemBase::ShadowQualityLevel();
		UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s QUALITY база: эффекты %d (sg %d), тени %d (sg %d)"), kP, Fx, Cv ? Cv->GetInt() : -9, Sh, Cs ? Cs->GetInt() : -9);
		T.Expect(Fx >= 0 && Fx <= 4 && Sh >= 0 && Sh <= 4, TEXT("QUALITY уровни базы в 0..4"));
		Finish(TEXT("Quality"), T, bQuit, GameWorld());
	}

	// ── F14: прогон сердец ──
	// Число таймеров менеджера мира — по строке «N Total Timers» штатного `ListTimers` (своего счётчика у движка нет).
	class FTimerCountSink : public FOutputDevice
	{
	public:
		int32 Total = -1, Active = -1;
		virtual bool CanBeUsedOnAnyThread() const override { return true; }   // строки любого потока (вместе со строкой ниже)
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
		{
			if (!IsInGameThread()) { return; }   // ★ 05.11.2026: устройство синхронное (CanBeUsedOnMultipleThreads) — ловим только строки игрового потока: их пишет проверяемый код, читает самотест, замка у полей нет
			if (!V) { return; }
			// Строки вида «------- 12 Total Timers -------»: без дефисов и пробелов по краям число стоит первым.
			const FString S(V);
			if (S.Contains(TEXT("Total Timers")))       { Total  = FCString::Atoi(*S.Replace(TEXT("-"), TEXT("")).TrimStartAndEnd()); }
			else if (S.Contains(TEXT("Active Timers"))) { Active = FCString::Atoi(*S.Replace(TEXT("-"), TEXT("")).TrimStartAndEnd()); }
		}
	};
	static void CountTimers(UWorld* W, int32& OutActive, int32& OutTotal)
	{
		FTimerCountSink Sink;
		GLog->AddOutputDevice(&Sink);
		W->GetTimerManager().ListTimers();
		GLog->Flush();
		GLog->RemoveOutputDevice(&Sink);
		OutActive = Sink.Active; OutTotal = Sink.Total;
	}

	struct FSoak
	{
		TWeakObjectPtr<UWorld> W;
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;
		double Dur = 600.0, Window = 30.0, T0 = 0.0, NextSample = 0.0, NextHeartTick = 0.0;   // NextHeartTick — следующий тик сердец
		int32 Pairs = 10;
		bool bQuit = false;
		int64 Hearts = 0, Impacts = 0;                 // выпущено сердец / импактов с начала
		double GtSum = 0.0, FrSum = 0.0; int32 Frames = 0;   // игровой поток и кадр за окно
		TArray<FString> Rows;
		TArray<int32> HeartTimerSeries, TotalSeries;
		TArray<double> GtSeries;
		FDelegateHandle SpawnH;
		TSet<TWeakObjectPtr<AActor>> LiveHearts;       // сердца в полёте (для контроля)
	};

	static void RunHeartSoak(const TArray<FString>& Args)
	{
		bool bQuit = false;
		const TArray<FString> A = Parse(Args, bQuit);
		UWorld* W = GameWorld();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!WS) { FTally T; T.Expect(false, TEXT("HeartSoak: нет игрового мира")); Finish(TEXT("HeartSoak"), T, bQuit, W); return; }
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(GI, TEXT("asda.Dedup.W2C.HeartSoak"), KitArgs)) { return; }
		TSharedRef<FSoak> S = MakeShared<FSoak>();
		S->W = W; S->WS = WS; S->bQuit = bQuit;
		if (A.Num() > 0) { S->Dur = FMath::Max(30.0, FCString::Atod(*A[0])); }
		if (A.Num() > 1) { S->Pairs = FMath::Clamp(FCString::Atoi(*A[1]), 1, 40); }
		GEngine->Exec(W, TEXT("scalability 0"));   // пресет Low (замер на Low — правило кампании производительности)
		const FIntPoint MyTile = EnsureSelf(W, WS);
		for (int32 p = 0; p < S->Pairs; ++p)
		{
			const int32 Ua = 7600 + 2 * p, Ub = Ua + 1;
			const int32 Dx = -6 + (p % 7) * 2, Dy = -6 + (p / 7) * 4;
			WS->DebugSpawnFakePeer(Ua, FString::Printf(TEXT("В2Всерд%d"), Ua), AsdaCoord::TileCenterGame(MyTile.X + Dx, MyTile.Y + Dy));
			WS->DebugSpawnFakePeer(Ub, FString::Printf(TEXT("В2Всерд%d"), Ub), AsdaCoord::TileCenterGame(MyTile.X + Dx, MyTile.Y + Dy + 3));
			FAsdaDedupW2CTestAccess::SoulState(WS, Ua, 1, Ub);
			FAsdaDedupW2CTestAccess::SoulState(WS, Ub, 1, Ua);
		}
		S->SpawnH = W->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([SW = TWeakPtr<FSoak>(S)](AActor* Act)
		{
			TSharedPtr<FSoak> X = SW.Pin();
			const AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(Act);
			if (!X || !Fx) { return; }
			if (Fx->EffectName.Contains(TEXT("soullink02"))) { ++X->Hearts; X->LiveHearts.Add(const_cast<AAsdaEffectActor*>(Fx)); }
			else if (Fx->EffectName.Contains(TEXT("soullink03")) || Act->ActorHasTag(FName(TEXT("SoulImpactFx")))) { ++X->Impacts; }
		}));
		S->T0 = FPlatformTime::Seconds();
		S->NextSample = S->Window;
		UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s SOAK старт: пар %d, длительность %.0f с, окно %.0f с, таймеров сердец на старте %d"),
			kP, S->Pairs, S->Dur, S->Window, FAsdaDedupW2CTestAccess::HeartTimers(WS));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float Dt) -> bool
		{
			UWorld* X = S->W.Get(); UAsdaWorldSubsystem* WS2 = S->WS.Get();
			if (!X || !WS2) { return false; }
			S->GtSum += FPlatformTime::ToMilliseconds(GGameThreadTime);
			S->FrSum += Dt * 1000.0;
			++S->Frames;
			const double Now = FPlatformTime::Seconds() - S->T0;
			// Тик сердец раз в секунду — как штатный таймер `TickAoiDespawn` (офлайн он не взведён).
			if (Now >= S->NextHeartTick) { S->NextHeartTick = Now + 1.0; FAsdaDedupW2CTestAccess::TickHearts(WS2); }
			if (Now < S->NextSample) { return true; }
			S->NextSample += S->Window;
			int32 Act = 0, Tot = 0;
			CountTimers(X, Act, Tot);
			int32 Live = 0;
			for (auto It = S->LiveHearts.CreateIterator(); It; ++It) { if (It->IsValid()) { ++Live; } else { It.RemoveCurrent(); } }
			const int32 HT = FAsdaDedupW2CTestAccess::HeartTimers(WS2);
			const double Gt = S->Frames ? S->GtSum / S->Frames : 0.0, Fr = S->Frames ? S->FrSum / S->Frames : 0.0;
			S->HeartTimerSeries.Add(HT); S->TotalSeries.Add(Tot); S->GtSeries.Add(Gt);
			UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s SOAK t=%4.0f с: таймеров сердец %d, сердец в полёте %d, таймеров мира активных %d / всего %d, сердец выпущено %lld, импактов %lld, игровой поток %.2f мс, кадр %.2f мс (%d кадров)"),
				kP, Now, HT, Live, Act, Tot, S->Hearts, S->Impacts, Gt, Fr, S->Frames);
			S->GtSum = S->FrSum = 0.0; S->Frames = 0;
			if (Now + 0.01 < S->Dur) { return true; }
			// Итог: рост таймеров за прогон против числа выпущенных сердец (при старой утечке каждое сердце = +1 таймер навсегда).
			FTally T;
			const int32 N = S->TotalSeries.Num();
			int32 MaxHT = 0; for (int32 v : S->HeartTimerSeries) { MaxHT = FMath::Max(MaxHT, v); }
			const int32 Grow = N > 1 ? S->TotalSeries.Last() - S->TotalSeries[0] : 0;
			int32 MaxTot = 0, MinTot = MAX_int32; for (int32 v : S->TotalSeries) { MaxTot = FMath::Max(MaxTot, v); MinTot = FMath::Min(MinTot, v); }
			UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s SOAK итог: сердец выпущено %lld, импактов %lld; таймеров сердец макс %d; таймеров мира мин %d / макс %d, рост первое→последнее окно %+d"),
				kP, S->Hearts, S->Impacts, MaxHT, MinTot, MaxTot, Grow);
			T.Expect(S->Hearts > 100, FString::Printf(TEXT("SOAK сердца реально летали (%lld) — проверка не слепа"), S->Hearts));
			T.Expect(MaxHT <= 2 * S->Pairs * 4, FString::Printf(TEXT("SOAK таймеров сердец не больше сердец, которые могут быть в полёте (%d ≤ %d)"), MaxHT, 2 * S->Pairs * 4));
			T.Expect(MaxTot - MinTot <= 2 * S->Pairs * 4, FString::Printf(TEXT("SOAK всего таймеров мира — константа с точностью до сердец в полёте (размах %d при %lld выпущенных)"), MaxTot - MinTot, S->Hearts));
			if (S->GtSeries.Num() >= 4)
			{
				const double First = (S->GtSeries[0] + S->GtSeries[1]) / 2.0;
				const double Last = (S->GtSeries[S->GtSeries.Num() - 1] + S->GtSeries[S->GtSeries.Num() - 2]) / 2.0;
				UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s SOAK игровой поток (Low): первые два окна %.2f мс, последние два %.2f мс, разница %+.2f мс (шум машины 0,7 мс)"),
					kP, First, Last, Last - First);
				T.Expect(Last - First < 0.7, FString::Printf(TEXT("SOAK игровой поток не растёт за прогон (%+.2f мс < 0,7)"), Last - First));
			}
			X->RemoveOnActorSpawnedHandler(S->SpawnH);
			for (int32 p = 0; p < S->Pairs; ++p)
			{
				const int32 Ua = 7600 + 2 * p;
				FAsdaDedupW2CTestAccess::SoulState(WS2, Ua, -1, -1);
				FAsdaDedupW2CTestAccess::SoulState(WS2, Ua + 1, -1, -1);
				FAsdaDedupW2CTestAccess::Despawn(WS2, Ua);
				FAsdaDedupW2CTestAccess::Despawn(WS2, Ua + 1);
			}
			Finish(TEXT("HeartSoak"), T, S->bQuit, X);
			return false;
		}));
	}

	// ── F9/F11 на карте логина ──
	static void RunGlyph(const TArray<FString>& Args)
	{
		bool bQuit = false;
		const TArray<FString> A = Parse(Args, bQuit);
		const FString Tag = A.Num() > 0 ? A[0] : TEXT("probe");
		// Второй аргумент `fade1` — прогон с ключом `asda.Login.GlyphFade 1` (снимок варианта «глиф проявляется вместе с превью»).
		const bool bKey1 = A.Num() > 1 && A[1].Equals(TEXT("fade1"), ESearchCase::IgnoreCase);
		UWorld* W = GameWorld();
		// ★ Ревизия В2 дедупа №2 (11.10.2026): подмена ключа — через `AsdaCVarSave` (возврат в `Finish`, сторож ухода мира).
		//   Второй аргумент `default` — ключ НЕ трогается: проверяется умолчание сборки (с 11.10 — 1, решение владельца «плавное»).
		const bool bKeepDefault = A.Num() > 1 && A[1].Equals(TEXT("default"), ESearchCase::IgnoreCase);
		if (!bKeepDefault)
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Login.GlyphFade"), bKey1 ? 1 : 0);
			AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Dedup.W2C.Glyph"));
		}
		AAsdaLoginGameMode* GM = W ? Cast<AAsdaLoginGameMode>(W->GetAuthGameMode()) : nullptr;
		if (!GM) { FTally T; T.Expect(false, TEXT("Glyph: нет AAsdaLoginGameMode (карта логина?)")); Finish(TEXT("Glyph"), T, bQuit, W); return; }
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Dedup.W2C.Glyph"), KitArgs)) { return; }
		TSharedRef<FTally> T = MakeShared<FTally>();
		// Третий аргумент — слот впереди (0 — персонаж в кадре, 1 — глиф пустого слота в кадре).
		const int32 Front = A.Num() > 2 ? FCString::Atoi(*A[2]) : 0;
		FAsdaDedupW2CTestAccess::StartCharSelectFlow(GM, Front);
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../../_re/reports/dedup_audit2/w2C_shots/"));
		TWeakObjectPtr<AAsdaLoginGameMode> WGM = GM;
		TSharedRef<FSeq> Q = MakeShared<FSeq>();
		TSharedRef<TArray<float>> Eff = MakeShared<TArray<float>>();   // эффективная альфа глифа по замерам
		const double Times[] = { 0.15, 0.5, 0.85, 2.0 };
		for (double At : Times)
		{
			Q->Add(At, [WGM, Dir, Tag, At, Eff]()
			{
				AAsdaLoginGameMode* G = WGM.Get(); if (!G) { return; }
				float MinEff = 1.f; int32 NVis = 0;
				for (AActor* Gl : FAsdaDedupW2CTestAccess::Glyphs(G))
				{
					AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(Gl);
					if (!Fx || Fx->IsHidden()) { continue; }
					++NVis;
					// Секционные материалы глифа: стоит ли MID и какой у него FadeIn (по умолчанию мастера — 1).
					TArray<UMeshComponent*> Comps; Fx->GetComponents<UMeshComponent>(Comps);
					int32 NMid = 0, NSec = 0; float MinFade = 1.f;
					for (UMeshComponent* Cm : Comps)
					{
						for (int32 s = 0; s < Cm->GetNumMaterials(); ++s)
						{
							UMaterialInterface* M = Cm->GetMaterial(s);
							if (!M) { continue; }
							++NSec;
							NMid += M->IsA<UMaterialInstanceDynamic>() ? 1 : 0;
							float V = 1.f;
							if (M->GetScalarParameterValue(FName(AsdaMat::kParamFadeIn), V)) { MinFade = FMath::Min(MinFade, V); }
						}
					}
					const float E = Fx->FxAlphaScale * MinFade;
					MinEff = FMath::Min(MinEff, E);
					UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s GLYPH t=%.2f %s: FxAlphaScale %.2f, секций %d (MID %d), FadeIn секций мин %.2f → эффективная альфа %.2f"),
						kP, At, *Fx->EffectName, Fx->FxAlphaScale, NSec, NMid, MinFade, E);
				}
				Eff->Add(NVis ? MinEff : -1.f);
				// Имена MID превью персонажа (F9): поиск секций сборки по имени материала во время фейда.
				for (AActor* Ch : FAsdaDedupW2CTestAccess::Chars(G))
				{
					if (!Ch) { continue; }
					TArray<UMeshComponent*> Comps; Ch->GetComponents<UMeshComponent>(Comps);
					FString Names; int32 NFaceLike = 0;
					for (UMeshComponent* Cm : Comps)
					{
						for (int32 s = 0; s < Cm->GetNumMaterials(); ++s)
						{
							const UMaterialInterface* M = Cm->GetMaterial(s);
							const FString Nm = M ? M->GetName() : TEXT("-");
							if (Names.Len() < 400) { Names += Nm + TEXT(" "); }
							for (const TCHAR* K : { TEXT("face"), TEXT("hair"), TEXT("chst"), TEXT("pant"), TEXT("shoe") }) { if (Nm.Contains(K)) { ++NFaceLike; break; } }
						}
					}
					UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s GLYPH t=%.2f превью %s: фейд %.2f, секций по имени %d | %s"),
						kP, At, *Ch->GetName(), FAsdaDedupW2CTestAccess::FadeTime(G), NFaceLike, *Names);
				}
				const FString File = Dir / FString::Printf(TEXT("w2c_glyph_%s_t%03d.png"), *Tag, (int32)(At * 100.0 + 0.5));
				FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
				UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s GLYPH кадр заказан → %s"), kP, *File);
			});
		}
		Q->Add(3.0, [T, Eff, bQuit, WGM]()
		{
			const int32 Key = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Login.GlyphFade"))
				? IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Login.GlyphFade"))->GetInt() : -1;
			FString S; for (float v : *Eff) { S += FString::Printf(TEXT("%.2f "), v); }
			UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s GLYPH эффективная альфа глифов на 0.15/0.5/0.85/2.0 с: %s (ключ asda.Login.GlyphFade=%d)"), kP, *S, Key);
			T->Expect(Eff->Num() == 4 && (*Eff)[0] >= 0.f, TEXT("GLYPH глифы пустых слотов видимы"));
			if (Eff->Num() == 4 && (*Eff)[0] >= 0.f)
			{
				// Эталон (`channelSelectRender3D@0xF07BE0`, ветка пустого слота): глиф кладётся в очередь без множителя альфы —
				//   фейда у глифа нет. Ключ 1 — фейд вместе с превью (FxAlphaScale), должен идти по возрастанию.
				if (Key == 1) { T->Expect((*Eff)[0] < 0.5f && (*Eff)[1] < (*Eff)[2] && (*Eff)[3] > 0.99f, TEXT("GLYPH ключ 1: глиф проявляется вместе с превью")); }
				else          { T->Expect((*Eff)[0] > 0.99f && (*Eff)[3] > 0.99f, TEXT("GLYPH эталон: глиф без фейда (полная альфа с первого кадра)")); }
			}
			Finish(TEXT("Glyph"), *T, bQuit, WGM.IsValid() ? WGM->GetWorld() : nullptr);
		});
		// Отсчёт — от СТАРТА фейда превью (перелёт и открытие шкатулки занимают секунды): ждём `PreviewFadeTime >= 0`.
		TSharedRef<double> Waited = MakeShared<double>(0.0);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WGM, Q, Waited, T, bQuit](float Dt) -> bool
		{
			AAsdaLoginGameMode* G = WGM.Get();
			if (!G) { return false; }
			*Waited += Dt;
			if (FAsdaDedupW2CTestAccess::FadeTime(G) >= 0.f)
			{
				UE_LOG(LogAsdaDedupW2C, Display, TEXT("%s GLYPH фейд превью стартовал через %.1f с после char-list"), kP, *Waited);
				Q->Run();
				return false;
			}
			if (*Waited > 60.0) { T->Expect(false, TEXT("GLYPH фейд превью не стартовал за 60 с")); Finish(TEXT("Glyph"), *T, bQuit, G->GetWorld()); return false; }
			return true;
		}));
	}
}

static FAutoConsoleCommand GAsdaDedupW2CLife(TEXT("asda.Dedup.W2C.Life"),
	TEXT("[САМОТЕСТ В2-C, F2] сроки жизни одноразовых эффектов: снимок мест вызова и ударов навыков + реальный путь [quit]."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaDedupW2C::RunLife));
static FAutoConsoleCommand GAsdaDedupW2CFade(TEXT("asda.Dedup.W2C.Fade"),
	TEXT("[САМОТЕСТ В2-C, F1/F9] уход соседа поверх фейд-ина (контроль 2 с) и имена слотов во время фейда [quit]."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaDedupW2C::RunFade));
static FAutoConsoleCommand GAsdaDedupW2CRide(TEXT("asda.Dedup.W2C.Ride"),
	TEXT("[САМОТЕСТ В2-C, F13] звук чужого транспорта на 5/30/50 м против PlaySound3D [quit]."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaDedupW2C::RunRide));
static FAutoConsoleCommand GAsdaDedupW2CFxLoop(TEXT("asda.Dedup.W2C.FxLoop"),
	TEXT("[САМОТЕСТ В2-C, F3] DT_FxLoop: в корне? переживает obj gc? [quit]."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaDedupW2C::RunFxLoop));
static FAutoConsoleCommand GAsdaDedupW2CQuality(TEXT("asda.Dedup.W2C.Quality"),
	TEXT("[САМОТЕСТ В2-C, F17] уровень качества базы 0..4 и независимость секции [Группа@N] от зажима [quit]."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaDedupW2C::RunQuality));
static FAutoConsoleCommand GAsdaDedupW2CHeartSoak(TEXT("asda.Dedup.W2C.HeartSoak"),
	TEXT("[САМОТЕСТ В2-C, F14] пары соулмейтов: таймеры сердец и игровой поток на Low по окнам 30 с: [сек=600] [пар=10] [quit]."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaDedupW2C::RunHeartSoak));
static FAutoConsoleCommand GAsdaDedupW2CGlyph(TEXT("asda.Dedup.W2C.Glyph"),
	TEXT("[САМОТЕСТ В2-C, F9/F11] карта логина: превью выбора персонажа офлайн — альфа глифов, имена MID превью, кадры: [метка] [quit]."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaDedupW2C::RunGlyph));

#endif // !UE_BUILD_SHIPPING
