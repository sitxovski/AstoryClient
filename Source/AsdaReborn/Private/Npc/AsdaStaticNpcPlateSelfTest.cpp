// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВЫСОТЫ ПЛАШКИ СТАТИЧНЫХ NPC — `asda.SelfTest.StaticNpcPlate [npc=<id>] [shot] [delay=N] [quit]` (баг владельца
//   29.10.2026: имя и «!» над доской объявлений висели «оооочень высоко», за кадром; разбор — шапка `AAsdaNpcActor::ResolvePlateHeight`).
//   Офлайн (`-AsdaNoNet`), реальными путями (закон diagnostic-must-use-real-path):
//     A. ПЕРЕПИСЬ карты. Каждый запечённый NPC (кроме отключённых): высота якоря плашки над землёй при ключе
//        `asda.Npc.StaticPlateOnModel` 0 (прежнее) и 1 (новое) — штатный `ApplyNameplateHeight`; рядом верх модели (`MeshTopZ`),
//        верх капсулы клика, верх короба костей и `npc_list +0x68`.
//          A1 статичный (класс 1/3/5): при ключе 1 якорь = верх модели (±0.5);
//          A2 нестатичный (класс 0/2/4): ключ ничего не меняет (Δ ≤ 0.01).
//     B. ПУТЬ ИГРОКА (доска — NpcId из `npc=`, иначе первая доска класса 5, иначе модель «…board…»): NPC «удочеряется» штатным
//        пакетом 4067 (`DebugFeedPacket`), свой персонаж (офлайн-спавн 4003) ставится перед ним, камера — обычная (стрела 700,
//        наклон −20°, как при входе), над доской — жёлтый «!» (раздача меток `PushOverheadMarks` в диагностическом режиме
//        «метка всем» — без сервера задания доски не приходят). Замер после отрисовки —
//        ЖИВАЯ геометрия строки имени (`GetCachedGeometry`) и проекции верха модели и метки:
//          B1 строка имени целиком в кадре;            B2 имя НАД верхом доски, зазор 0…60 пунктов;
//          B3 метка целиком в кадре;                   B4 низ метки над верхом строки имени (≥ −2 пункта);
//          B5 ближняя (400) и дальняя (1270) стрела: имя по-прежнему над верхом доски (зазор 0…60) и в кадре, если над верхом
//             доски в кадре есть место под имя и метку (≥ kRoomPt; на ближней стреле перед доской верх модели уходит к краю или за
//             край — тогда проверяется только зазор);
//          B6 ключ 0 — прежнее поведение: якорь там же, где до правки (= верх капсулы + NameplateTopOffset) — замер «до».
//   Строки — «ПЛАШКА-СТАТИКА», итог — «ПЛАШКА-СТАТИКА ИТОГ: ok=… fail=…». `shot` — кадры в Reports/bugs_2026-10-29/board_plate/
//   (снимаются в конце, когда сошли эффект появления и название зоны); `dir=<град>` — сторона, с которой встать (от курса NPC;
//   без него — из 8 сторон та, где земля ровнее).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Npc/AsdaNpcActor.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaPlayerCharacter.h"          // свой персонаж и стрела камеры
#include "World/AsdaPlayerController.h"         // SetOrbitAnglesForBench — угол стрелы (тот же путь, что ПКМ-драг)
#include "World/AsdaGroundTrace.h"              // высота .TER под точкой постановки персонажа
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaBytes.h"                          // запись полей тела 4067
#include "AsdaCVarSave.h"                       // ключ на время прогона (вернётся в итоге)
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — офлайн повторно на отложенном старте   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaDataTableCache.h"                 // AsdaDt::RowByInt — строка npc_list (карта, высота)
#include "Quest/AsdaQuestTypes.h"               // FAsdaNpcQuestNpcRow
#include "Quest/AsdaQuestMarkerSubsystem.h"     // метка «!» штатной раздачей меток (PushOverheadMarks)
#include "Test/AsdaSelfTestKit.h"               // каркас: аргументы, отказ, итог, выход
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"      // GetViewportScale — DPI плашки
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"                       // FScreenshotRequest — кадры
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaStaticPlateTest, Log, All);

namespace AsdaStaticPlateTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.StaticNpcPlate");   // команда
	static const TCHAR* const kPrefix = TEXT("ПЛАШКА-СТАТИКА");              // префикс строк лога
	static const TCHAR* const kKey = TEXT("asda.Npc.StaticPlateOnModel");    // проверяемый ключ
	static constexpr int32 kRid = 7731;                                      // renderId «удочерённой» доски
	static constexpr float kStandDist = 450.f;                               // от доски до своего персонажа, мировые ед.
	static constexpr float kMaxGapPt = 60.f;                                 // B2: наибольший зазор «низ имени — верх доски», пункты
	static constexpr float kRoomPt = 100.f;                                  // B5: место над верхом доски под имя и метку, пункты

	// Состояние прогона.
	struct FState
	{
		int32 Ok = 0, Fail = 0;                      // счётчики проверок
		bool bQuit = false;                          // выйти по итогу
		bool bShot = false;                          // снимать кадры
		int32 WantNpc = 0;                           // NpcId из `npc=` (0 — выбрать доску самому)
		float WantDir = NAN;                         // `dir=` — сторона постановки от курса NPC, градусы (NaN — выбрать самому)
		FAsdaNetSelfScope SelfScope;                 // «свой» персонаж сети — вернуть как до прогона (офлайн-спавн его меняет)
		TWeakObjectPtr<UWorld> World;                // мир прогона
		TWeakObjectPtr<AAsdaNpcActor> Board;         // NPC пути игрока
		FVector Stand = FVector::ZeroVector;         // где стоит свой персонаж
		float LookYaw = 0.f;                         // мировой курс взгляда камеры (от персонажа на доску)
		float OldAnchor = 0.f;                       // якорь доски при ключе 0 (перепись)
		float NewAnchor = 0.f;                       // якорь доски при ключе 1 (перепись)

		// Отметка проверки.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaStaticPlateTest, Log, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaStaticPlateTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
	};

	// Шаг через Delay секунд (мир мог уйти — тогда ничего).
	static void After(const TWeakObjectPtr<UWorld>& WW, float Delay, TFunction<void()> Fn)
	{
		UWorld* W = WW.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), FMath::Max(0.01f, Delay), false);
	}

	// Строка npc_list (DT_NpcQuestNpc) этого NPC (nullptr — нет).
	static const FAsdaNpcQuestNpcRow* RowOf(const AAsdaNpcActor* A)
	{
		return (A && A->NpcId > 0)
			? AsdaDt::RowByInt<FAsdaNpcQuestNpcRow>(AAsdaNpcActor::kNpcQuestNpcPath, A->NpcId, TEXT("StaticNpcPlateTest")) : nullptr;
	}

	// Якорь плашки над началом координат актора (мировые ед.) — точка, которую проецирует экранная плашка.
	static float AnchorZ(const AAsdaNpcActor* A)
	{
		return (A && A->Nameplate) ? (float)(A->Nameplate->GetPlateWorldPoint().Z - A->GetActorLocation().Z) : 0.f;
	}

	// Верх капсулы клика над началом координат актора.
	static float CapsuleTopZ(const AAsdaNpcActor* A)
	{
		if (!A || !A->PickHitbox) { return 0.f; }
		return (float)(A->PickHitbox->GetComponentLocation().Z + A->PickHitbox->GetScaledCapsuleHalfHeight() - A->GetActorLocation().Z);
	}

	// Пересчитать плашку (и метку, если висит) под текущее значение ключа — штатными точками актора.
	static void Reapply(AAsdaNpcActor* A)
	{
		if (!A) { return; }
		A->ApplyNameplateHeight();
		if (A->QuestMarkKind != 0) { A->ApplyQuestMarkHeight(); }
	}

	// ── A. Перепись ────────────────────────────────────────────────────────────────────────────────────────────────────────
	static void Census(FState& S)
	{
		UWorld* W = S.World.Get();
		if (!W) { return; }
		int32 NStatic = 0, NOther = 0;
		float MaxDrop = 0.f;
		FString MaxDropWho;
		for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
		{
			AAsdaNpcActor* A = *It;
			if (!A || A->bDisabled || A->NpcId <= 0 || !A->Nameplate) { continue; }
			const int32 Cls = A->GetEntityClass();
			const bool bStatic = A->IsStaticPlateType();
			AsdaCVarSave::SetForTest(kKey, 0); Reapply(A);
			const float P0 = AnchorZ(A);
			AsdaCVarSave::SetForTest(kKey, 1); Reapply(A);
			const float P1 = AnchorZ(A);
			const float MeshTop = A->MeshTopZ();
			FBox Pick;
			const float BonesTop = A->GetPickBoxWorld(Pick) ? (float)(Pick.Max.Z - A->GetActorLocation().Z) : -1.f;
			const float BoundsTop = A->Mesh ? (float)(A->Mesh->Bounds.GetBox().Max.Z - A->GetActorLocation().Z) : -1.f;
			const FString Model = (A->Mesh && A->Mesh->GetSkeletalMeshAsset()) ? A->Mesh->GetSkeletalMeshAsset()->GetName() : FString(TEXT("—"));
			UE_LOG(LogAsdaStaticPlateTest, Log,
				TEXT("%s перепись npc %-4d класс %d %-26s h(+0x68) %6.0f | якорь: ключ0 %7.1f ключ1 %7.1f | верх модели %7.1f, габарит позы %7.1f, кости+h %7.1f, капсула %7.1f"),
				kPrefix, A->NpcId, Cls, *Model, A->GetNpcListHeight(), P0, P1, MeshTop, BoundsTop, BonesTop, CapsuleTopZ(A));
			if (bStatic)
			{
				++NStatic;
				S.Expect(P1 > 1.f && FMath::Abs(P1 - MeshTop) <= 0.5f,
					FString::Printf(TEXT("A1 npc %d (класс %d, %s): якорь при ключе 1 = верх модели (%.1f / %.1f), было %.1f"),
						A->NpcId, Cls, *Model, P1, MeshTop, P0));
				if (P0 - P1 > MaxDrop) { MaxDrop = P0 - P1; MaxDropWho = FString::Printf(TEXT("npc %d %s"), A->NpcId, *Model); }
			}
			else
			{
				++NOther;
				S.Expect(FMath::Abs(P1 - P0) <= 0.01f,
					FString::Printf(TEXT("A2 npc %d (класс %d): ключ не трогает нестатичного (%.1f / %.1f)"), A->NpcId, Cls, P0, P1));
			}
			if (S.Board.Get() == A) { S.OldAnchor = P0; S.NewAnchor = P1; }
		}
		UE_LOG(LogAsdaStaticPlateTest, Log, TEXT("%s перепись: статичных %d, прочих %d; наибольшее опускание %.1f (%s)"),
			kPrefix, NStatic, NOther, MaxDrop, *MaxDropWho);
	}

	// Выбор NPC пути игрока: `npc=`, иначе доска класса 5, иначе модель «…board…».
	static AAsdaNpcActor* PickBoard(FState& S)
	{
		UWorld* W = S.World.Get();
		if (!W) { return nullptr; }
		AAsdaNpcActor* ByModel = nullptr;
		for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
		{
			AAsdaNpcActor* A = *It;
			if (!A || A->bDisabled || A->NpcId <= 0) { continue; }
			if (S.WantNpc > 0) { if (A->NpcId == S.WantNpc) { return A; } continue; }
			if (A->GetEntityClass() == 5) { return A; }
			const USkeletalMesh* SK = A->Mesh ? A->Mesh->GetSkeletalMeshAsset() : nullptr;
			if (!ByModel && SK && SK->GetName().Contains(TEXT("board"))) { ByModel = A; }
		}
		return ByModel;
	}

	// Свой персонаж и стрела.
	static AAsdaPlayerController* Pc(const FState& S)
	{
		return S.World.IsValid() ? Cast<AAsdaPlayerController>(S.World->GetFirstPlayerController()) : nullptr;
	}
	static AAsdaPlayerCharacter* Ch(const FState& S)
	{
		AAsdaPlayerController* P = Pc(S);
		return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr;
	}

	// Поставить персонажа перед NPC и камеру «обычно» (стрела Len, наклон −20°, взгляд на NPC).
	static void PlaceCamera(FState& S, float Len)
	{
		AAsdaPlayerCharacter* C = Ch(S);
		AAsdaPlayerController* P = Pc(S);
		if (!C || !P) { return; }
		C->SetActorLocation(S.Stand, false, nullptr, ETeleportType::TeleportPhysics);
		P->SetOrbitAnglesForBench(FRotator::NormalizeAxis(S.LookYaw - (float)C->GetActorRotation().Yaw), -20.f);
		if (C->CameraArm) { C->CameraArm->TargetArmLength = Len; }
	}

	// Точка стояния: из 8 направлений на расстоянии kStandDist — где есть земля и перепад к основанию NPC меньше.
	static bool ChooseStand(FState& S, const AAsdaNpcActor* A)
	{
		UWorld* W = S.World.Get();
		if (!W || !A) { return false; }
		const FVector O = A->GetActorLocation();
		float BestD = TNumericLimits<float>::Max();
		const int32 N = FMath::IsNaN(S.WantDir) ? 8 : 1;
		for (int32 i = 0; i < N; ++i)
		{
			const float Yaw = A->GetActorRotation().Yaw + (FMath::IsNaN(S.WantDir) ? 45.f * i : S.WantDir);
			const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
			const FVector P = O + Dir * kStandDist;
			const float G = AsdaGround::GroundZ(W, (float)P.X, (float)P.Y, NAN);
			if (FMath::IsNaN(G)) { continue; }
			const float D = FMath::Abs(G - (float)O.Z);
			if (D < BestD) { BestD = D; S.Stand = FVector(P.X, P.Y, G); S.LookYaw = Yaw + 180.f; }
		}
		return BestD < TNumericLimits<float>::Max();
	}

	// Замер в кадре (пункты = пиксели вьюпорта / DPI). Tag — подпись строки; bCheck — проверять B1…B4 (иначе только лог).
	static void Measure(FState& S, const FString& Tag, bool bCheck, bool bFull)
	{
		UWorld* W = S.World.Get();
		AAsdaNpcActor* A = S.Board.Get();
		AAsdaPlayerController* P = Pc(S);
		if (!W || !A || !P || !A->Nameplate)
		{
			S.Expect(!bCheck, FString::Printf(TEXT("%s: нет мира/NPC/контроллера"), *Tag));
			return;
		}
		const UAsdaNameplateComponent* NP = A->Nameplate;
		const float DPI = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(W), 0.01f);
		FVector2D VpAbs(0.f, 0.f), VpSize(0.f, 0.f);
		if (GEngine && GEngine->GameViewport)
		{
			if (TSharedPtr<SViewport> VW = GEngine->GameViewport->GetGameViewportWidget())
			{
				VpAbs = VW->GetCachedGeometry().GetAbsolutePosition();
				VpSize = VW->GetCachedGeometry().GetAbsoluteSize();
			}
		}
		// Живая строка имени (как её нарисовал Slate) → пиксели вьюпорта.
		FVector2D NPos, NSize;
		const UUserWidget* SW = NP->DebugScreenWidget();
		const bool bName = NP->GetNameAbsRect(NPos, NSize) && SW && SW->IsVisible() && NSize.Y > 0.5;
		const FVector2D NameTL = NPos - VpAbs;
		const FVector2D NameBR = NameTL + NSize;
		// Проекции: якорь плашки, верх модели, метка (габарит меша метки).
		const FVector Base = A->GetActorLocation();
		FVector2D SA, ST;
		const bool bA = P->ProjectWorldLocationToScreen(NP->GetPlateWorldPoint(), SA, true);
		const bool bT = P->ProjectWorldLocationToScreen(Base + FVector(0.f, 0.f, A->MeshTopZ()), ST, true);
		bool bM = false;
		FVector2D MTop(0.f, 0.f), MBot(0.f, 0.f);
		if (A->QuestMark && A->QuestMark->IsVisible() && A->QuestMarkKind != 0)
		{
			const FBox MB = A->QuestMark->Bounds.GetBox();
			const FVector Mc = MB.GetCenter();
			bM = P->ProjectWorldLocationToScreen(FVector(Mc.X, Mc.Y, MB.Max.Z), MTop, true)
				&& P->ProjectWorldLocationToScreen(FVector(Mc.X, Mc.Y, MB.Min.Z), MBot, true);
		}
		const float GapPt = bName && bT ? (float)((ST.Y - NameBR.Y) / DPI) : -9999.f;     // низ имени → верх модели
		const float MarkGapPt = bName && bM ? (float)((NameTL.Y - MBot.Y) / DPI) : -9999.f; // низ метки → верх имени
		UE_LOG(LogAsdaStaticPlateTest, Log,
			TEXT("%s %s: вьюпорт %.0fx%.0f DPI %.2f | якорь над землёй %.1f, верх модели %.1f | экран: якорь %s(%.0f,%.0f), верх модели %s(%.0f,%.0f), ")
			TEXT("имя %s%.0f…%.0f × %.0f…%.0f, метка %s%.0f…%.0f | зазор имя→модель %.1f пт, метка→имя %.1f пт"),
			kPrefix, *Tag, VpSize.X, VpSize.Y, DPI, AnchorZ(A), A->MeshTopZ(), bA ? TEXT("") : TEXT("(нет) "), SA.X, SA.Y,
			bT ? TEXT("") : TEXT("(нет) "), ST.X, ST.Y, bName ? TEXT("") : TEXT("(нет) "), NameTL.X, NameBR.X, NameTL.Y, NameBR.Y,
			bM ? TEXT("") : TEXT("(нет) "), MTop.Y, MBot.Y, GapPt, MarkGapPt);
		if (!bCheck) { return; }
		// B5: на ближней стреле верх самой доски у края или за краем кадра — тогда «в кадре» не проверяется, только зазор.
		const bool bTopIn = bT && ST.Y >= kRoomPt * DPI && ST.Y <= VpSize.Y;
		if (!bFull)
		{
			S.Expect(GapPt >= 0.f && GapPt <= kMaxGapPt,
				FString::Printf(TEXT("%s: имя над верхом доски, зазор %.1f пт (0…%.0f)"), *Tag, GapPt, kMaxGapPt));
			if (!bTopIn)
			{
				UE_LOG(LogAsdaStaticPlateTest, Log, TEXT("%s %s: над верхом доски в кадре меньше %.0f пт (y %.0f) — «в кадре» не проверяется"),
					kPrefix, *Tag, kRoomPt, ST.Y);
				return;
			}
		}
		const bool bNameIn = bName && VpSize.Y > 0.0 && NameTL.Y >= 0.0 && NameBR.Y <= VpSize.Y && NameTL.X >= 0.0 && NameBR.X <= VpSize.X;
		const bool bMarkIn = bM && VpSize.Y > 0.0 && MTop.Y >= 0.0 && MBot.Y <= VpSize.Y && MTop.X >= 0.0 && MTop.X <= VpSize.X;
		S.Expect(bNameIn, FString::Printf(TEXT("B1 %s: строка имени целиком в кадре (y %.0f…%.0f из %.0f)"), *Tag, NameTL.Y, NameBR.Y, VpSize.Y));
		S.Expect(bMarkIn, FString::Printf(TEXT("B3 %s: метка «!» целиком в кадре (y %.0f…%.0f)"), *Tag, MTop.Y, MBot.Y));
		if (bFull)
		{
			S.Expect(GapPt >= 0.f && GapPt <= kMaxGapPt,
				FString::Printf(TEXT("B2 %s: имя над верхом доски, зазор %.1f пт (0…%.0f)"), *Tag, GapPt, kMaxGapPt));
			S.Expect(MarkGapPt >= -2.f,
				FString::Printf(TEXT("B4 %s: низ метки над верхом имени (%.1f пт ≥ −2)"), *Tag, MarkGapPt));
		}
	}

	// Кадр в папку отчёта (песочница Saved пакетного прогона удаляется).
	static void Shot(const FState& S, const TCHAR* Suffix)
	{
		if (!S.bShot || !S.World.IsValid()) { return; }
		const FString Map = S.World->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT(""));
		const FString Name = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / FString::Printf(
			TEXT("../Reports/bugs_2026-10-29/board_plate/shot_%s_npc%d_%s.png"), *Map, S.Board.IsValid() ? S.Board->NpcId : 0, Suffix));
		FScreenshotRequest::RequestScreenshot(Name, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaStaticPlateTest, Log, TEXT("%s кадр: %s"), kPrefix, *Name);
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		TSharedRef<FState> S = MakeShared<FState>();
		S->World = W;
		S->bQuit = A.bQuit;
		S->bShot = A.Has(TEXT("shot"));
		for (const FString& P : A.Pos)
		{
			if (P.StartsWith(TEXT("npc="))) { S->WantNpc = FCString::Atoi(*P.Mid(4)); }
			if (P.StartsWith(TEXT("dir="))) { S->WantDir = FCString::Atof(*P.Mid(4)); }
		}
		TWeakObjectPtr<UWorld> WW(W);

		// Финал: ключ — к исходному (и плашки/метки под него), итог, выход.
		auto Finish = [WW, S]()
		{
			UWorld* Wd = WW.Get();
			if (UAsdaQuestMarkerSubsystem* Q = (Wd && Wd->GetGameInstance()) ? Wd->GetGameInstance()->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr)
			{
				Q->SetForcedMarkForAll(-1);   // диагностический режим снят — метки снова по состоянию заданий
			}
			AsdaCVarSave::RestoreAll(kCmd);
			if (Wd) { for (TActorIterator<AAsdaNpcActor> It(Wd); It; ++It) { Reapply(*It); } }
			S->SelfScope.Restore();
			AsdaSelfTestKit::Finish(Wd, kCmd, S->bQuit, LogAsdaStaticPlateTest, kPrefix, S->Ok, S->Fail, TEXT(""));
		};

		After(WW, A.Delay, [WW, Gate, S, Finish]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			S->Board = PickBoard(*S);
			// A. Перепись (до «удочерения» — запечённые шаблоны карты; их плашки те же, что у принятых сервером).
			Census(*S);
			AAsdaNpcActor* B = S->Board.Get();
			if (!B)
			{
				UE_LOG(LogAsdaStaticPlateTest, Log, TEXT("%s: доски на карте нет — путь игрока пропущен (только перепись)"), kPrefix);
				Finish();
				return;
			}
			// Свой персонаж — офлайн-спавн штатным путём 4003 на клетке NPC (карта — из npc_list).
			S->SelfScope.Capture(AsdaSelfTestKit::NetOf(Wd), kCmd);
			if (AsdaSelfTestKit::NeedSelfSpawn(Wd) && GEngine)
			{
				const FAsdaNpcQuestNpcRow* R = RowOf(B);
				GEngine->Exec(Wd, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), R ? FMath::Max(R->MapId, 0) : 0));
				GEngine->Exec(Wd, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn %d %d"), B->TileX, B->TileZ));
			}
			// «Удочерение» доски штатным 4067 (как сервер при входе в зону видимости).
			if (!B->bNetSpawned)
			{
				if (UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(Wd))
				{
					TArray<uint8> Body; Body.SetNumZeroed(108);
					AsdaBytes::WrI16(Body.GetData() + 0, (int16)kRid);
					AsdaBytes::WrI16(Body.GetData() + 2, (int16)B->NpcId);
					AsdaBytes::WrI16(Body.GetData() + 8, (int16)B->TileX);
					AsdaBytes::WrI16(Body.GetData() + 10, (int16)B->TileZ);
					AsdaTestNet::DebugFeedPacket(Net, 4067, Body);
				}
			}
			After(WW, 2.0f, [WW, S, Finish]()
			{
				AAsdaNpcActor* B2 = S->Board.Get();
				const bool bAdopted = B2 && B2->bNetSpawned && !B2->IsHidden();
				S->Expect(bAdopted && Ch(*S) != nullptr, FString::Printf(TEXT("B0 npc %d принят пакетом 4067 и виден, свой персонаж есть"),
					B2 ? B2->NpcId : 0));
				if (!bAdopted || !Ch(*S) || !ChooseStand(*S, B2)) { Finish(); return; }
				// Жёлтый «!» штатной раздачей меток (`PushOverheadMarks` → `SetQuestMark`) в режиме «метка всем».
				if (UAsdaQuestMarkerSubsystem* Q = B2->GetGameInstance() ? B2->GetGameInstance()->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr)
				{
					Q->SetForcedMarkForAll(1);
				}
				PlaceCamera(*S, 700.f);
				After(WW, 1.5f, [WW, S, Finish]()
				{
					PlaceCamera(*S, 700.f);
					Measure(*S, TEXT("ключ 1, стрела 700"), true, true);
					After(WW, 1.0f, [WW, S, Finish]()
					{
						// B6: ключ 0 — прежнее поведение (замер «до»).
						AsdaCVarSave::SetForTest(kKey, 0);
						Reapply(S->Board.Get());
						After(WW, 0.6f, [WW, S, Finish]()
						{
							AAsdaNpcActor* B3 = S->Board.Get();
							PlaceCamera(*S, 700.f);
							const float Old = AnchorZ(B3);
							const float WantOld = B3 && B3->Nameplate ? CapsuleTopZ(B3) + B3->Nameplate->NameplateTopOffset : 0.f;
							Measure(*S, TEXT("ключ 0 (до правки), стрела 700"), false, false);
							S->Expect(B3 && FMath::Abs(Old - WantOld) <= 0.5f,
								FString::Printf(TEXT("B6 ключ 0: якорь %.1f = верх капсулы + сдвиг %.1f (прежнее поведение)"), Old, WantOld));
							After(WW, 1.0f, [WW, S, Finish]()
							{
								AsdaCVarSave::SetForTest(kKey, 1);
								Reapply(S->Board.Get());
								PlaceCamera(*S, 400.f);
								After(WW, 0.8f, [WW, S, Finish]()
								{
									PlaceCamera(*S, 400.f);
									Measure(*S, TEXT("B5 ключ 1, стрела 400"), true, false);
									PlaceCamera(*S, 1270.f);
									After(WW, 0.8f, [WW, S, Finish]()
									{
										PlaceCamera(*S, 1270.f);
										Measure(*S, TEXT("B5 ключ 1, стрела 1270"), true, false);
										if (!S->bShot) { Finish(); return; }
										// Кадры «после»/«до» — когда сошли эффект появления и название зоны.
										PlaceCamera(*S, 700.f);
										After(WW, 4.0f, [WW, S, Finish]()
										{
											PlaceCamera(*S, 700.f);
											Shot(*S, TEXT("key1"));
											After(WW, 1.0f, [WW, S, Finish]()
											{
												AsdaCVarSave::SetForTest(kKey, 0);
												Reapply(S->Board.Get());
												After(WW, 0.6f, [WW, S, Finish]()
												{
													Shot(*S, TEXT("key0"));
													After(WW, 1.0f, [Finish]() { Finish(); });
												});
											});
										});
									});
								});
							});
						});
					});
				});
			});
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaStaticNpcPlateSelfTest(
	AsdaStaticPlateTest::kCmd,
	TEXT("asda.SelfTest.StaticNpcPlate [npc=<id>] [shot] [delay=N] [quit] — высота плашки и метки статичных NPC (доски, гробницы): ")
	TEXT("перепись карты при ключе 0/1 и путь игрока у доски (имя и «!» в кадре, зазор над доской)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsdaStaticPlateTest::Run));

#endif // !UE_BUILD_SHIPPING
