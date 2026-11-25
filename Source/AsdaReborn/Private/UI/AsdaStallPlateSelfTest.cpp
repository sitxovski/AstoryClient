// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВЫВЕСКИ ЛИЧНОЙ ЛАВКИ НАД ПЕРСОНАЖЕМ — `asda.Trade.SelfTestStallPlate [delay=N] [shot] [quit]` (баг владельца
//   08.10.2026 п.3, отчёт `Reports/owner_1008/ui_bugs.md`). ПУТЬ ИГРОКА: 6304 через ШТАТНЫЙ `DebugFeedPacket` → очередь →
//   прилавки → вывески (`Chat_RenderHeadBubble`), свой персонаж и чужой (userNum 55), с титулом и без; после отрисовки снимается
//   ЖИВАЯ геометрия (`GetCachedGeometry`): рамка вывески, строка названия, строки плашки (титул/имя/HP/бейдж).
//   Проверки:
//     • строка названия по центру рамки по вертикали (центр строки − центр внутренности рамки, |Δ| ≤ 1 px);
//     • вывеска НАД плашкой, зазор до верхней строки плашки как у эталона: низ рамки = точка − (сдвиг + 62) + 25, верхняя строка
//       плашки (титул при титуле, иначе имя) — эталонная раскладка MAN1 → ожидаемый зазор 3 px (титул) / 13 px (без титула и клана);
//     • свой = чужой (закон self-nameplate-same-as-others).
//   Строки — «ВЫВЕСКА-ПЛАШКА», итог — «ВЫВЕСКА-ПЛАШКА ИТОГ: ok=… fail=…». Только офлайн.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaStallSignSubsystem.h"
#include "UI/AsdaSelfStoreSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaPlayerCharacter.h"         // свой персонаж садится (фазы «сидит»)
#include "World/AsdaKingCrownSubsystem.h"      // эталонное состояние действия (8 — сидит)
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "Pet/AsdaPetSubsystem.h"              // перехват исходящих (публичный вход)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaStallPlateTest, Log, All);

namespace AsdaStallPlateLocal
{
	constexpr int32 kSelfCharId = 424242;   // свой charId
	constexpr int32 kSelfUserNum = 17;      // свой userNum
	constexpr int32 kOther = 55;            // чужой продавец, charId 900055 (`DebugSpawnFakePeer`)

	// Тело 6304 (56 б): [0] открыта, [1] премиум, @2 charId, @6 название[50].
	static TArray<uint8> Make6304(uint8 Open, int32 CharId, const FString& Title)
	{
		TArray<uint8> B; B.SetNumZeroed(56);
		B[0] = Open; B[1] = 0;
		AsdaBytes::WrI32(B.GetData() + 2, CharId);
		FAsdaLoginData::EncodeCp1251(Title, B.GetData() + 6, 49);
		return B;
	}

	// Живой прямоугольник виджета (абсолютные единицы Slate). false — нет/не виден/нет геометрии.
	static bool Rect(const UWidget* W, FVector2D& Mn, FVector2D& Mx)
	{
		if (!W || !W->IsVisible()) { return false; }
		return AsdaUi::WidgetAbsRect(W, Mn, Mx) && Mx.Y > Mn.Y;
	}

	// Контекст прогона.
	struct FCtx
	{
		// ★ 11.10.2026 (В3 S1, N1(г)): «свой» (charId 424242, номер 17, уровень 30) прежде НЕ возвращался — RAII каркаса.
		FAsdaNetSelfScope SelfScope;
		int32 Ok = 0, Fail = 0;
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false, bShot = false;
		FString ShotTag = TEXT("run");     // метка снимков (до/после)
		int32 Phase = 0;                   // 0/2 — с титулом, 1/3 — без титула и клана; 2/3 — оба сидят (продавец в лавке сидит)
		int32 SitPolls = 0;                // ожидание состояния 8 у обоих
		uint32 Gen = 0;                    // перехват исходящих
		TMap<int32, float> GapByUser;      // зазор «верхняя строка плашки − низ вывески» по фазе (свой/чужой)
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaStallPlateTest, Log, TEXT("ВЫВЕСКА-ПЛАШКА ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaStallPlateTest, Error, TEXT("ВЫВЕСКА-ПЛАШКА FAIL %s"), *What); }
		}
	};

	using AsdaSelfTestCtx::ByFn::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	static UGameInstance* GIOf(const TSharedRef<FCtx>& C) { return C->W.Get() ? C->W.Get()->GetGameInstance() : nullptr; }

	// Поставить актёра перед камерой: вперёд Fwd, вбок Side.
	static void PlaceInFront(UWorld* W, AActor* A, float Fwd, float Side)
	{
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		APawn* Self = PC ? PC->GetPawn() : nullptr;
		if (!A || !Self || !PC->PlayerCameraManager || A == Self) { return; }
		const float Yaw = PC->PlayerCameraManager->GetCameraRotation().Yaw;
		const FVector F = FRotator(0.f, Yaw, 0.f).Vector();
		const FVector R = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
		A->SetActorLocation(Self->GetActorLocation() + F * Fwd + R * Side, false, nullptr, ETeleportType::TeleportPhysics);
	}

	// Титул/клан плашки фазы и пересборка раскладки (признак лавки ставит сама подсистема вывесок).
	static void SetPhasePlate(AActor* A, int32 Phase)
	{
		UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		if (!NP) { return; }
		const bool bTitle = (Phase % 2) == 0;
		NP->TitleId0 = bTitle ? 1 : -1;
		NP->TitleName = bTitle ? FString(TEXT("Собиратель")) : FString();
		NP->ClanId = -1;
		// [САМОТЕСТ] свой павн офлайн-спавна (`DebugSpawnSelfAt`) идёт мимо `FillNameplate` боевого входа — включаем плашку так же,
		//   как боевой спавн (AsdaWorldSubsystem.cpp: `bHideForLocalPlayer = false`, `bShowHp = true`; решение владельца).
		NP->bHideForLocalPlayer = false;
		NP->bAlwaysShowName = true;
		if (NP->DisplayName.IsEmpty()) { NP->DisplayName = TEXT("GM_sitxovski"); }
		NP->RefreshNameplate();
	}

	// Замер одной пары «вывеска + плашка» и проверки.
	static void Measure(TSharedRef<FCtx> C, UAsdaStallSignSubsystem* G, AActor* A, int32 UserNum, const TCHAR* Who)
	{
		const FString Tag = FString::Printf(TEXT("%s, %s, %s (состояние %d)"), Who, (C->Phase % 2) == 0 ? TEXT("титул есть, клана нет") : TEXT("без титула и клана"),
			C->Phase >= 2 ? TEXT("сидит") : TEXT("стоит"), UAsdaKingCrownSubsystem::EtalonActionStateOf(A));
		UUserWidget* Sign = G->DebugSignWidget(UserNum);
		UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		UUserWidget* Plate = NP ? NP->DebugScreenWidget() : nullptr;
		FVector2D FMn, FMx;
		if (!Sign || !Plate || !G->SignAbsRect(UserNum, FMn, FMx))
		{
			C->Expect(false, FString::Printf(TEXT("%s: нет вывески/плашки/геометрии (вывеска=%d плашка=%d видна=%d)"), *Tag,
				Sign ? 1 : 0, Plate ? 1 : 0, G->DebugSignShown(UserNum) ? 1 : 0));
			return;
		}
		// Масштаб «абсолютные единицы на пиксель базы» — у рамки: высота плана 25 базовых px.
		const float Sc = FMath::Max((float)((FMx.Y - FMn.Y) / 25.0), 0.01f);
		// Строка названия: верх узла + высота строки (желаемый размер текста).
		UTextBlock* Txt = Cast<UTextBlock>(Sign->GetWidgetFromName(TEXT("StreetStall_DisplayTxt")));
		if (!Txt) { Txt = Cast<UTextBlock>(Sign->GetWidgetFromName(TEXT("StreetStall_DisplayTxt1"))); }
		FVector2D TMn = FVector2D::ZeroVector, TMx = FVector2D::ZeroVector;   // ★ R5-fix-c (13.11.2026): начальные значения — C4701 (при отказе геометрии числа уходили в строку проверки неинициализированными)
		const bool bTxt = Txt && Rect(Txt, TMn, TMx);
		const float LineH = Txt ? (float)Txt->GetDesiredSize().Y : 0.f;
		const float LineCenter = bTxt ? (float)(TMn.Y / Sc) + LineH * 0.5f : 0.f;
		const float InnerCenter = (float)((FMn.Y + FMx.Y) * 0.5 / Sc);   // рамка 3 px сверху и снизу — центр внутренности = центр рамки
		// Строки плашки.
		auto R = [Plate](const TCHAR* N, FVector2D& Mn, FVector2D& Mx) { return Rect(Plate->GetWidgetFromName(FName(N)), Mn, Mx); };
		// ★ R5-fix-c (13.11.2026): начальные значения — `NMn` читается ниже без проверки `bName` (при отказе геометрии шло неинициализированное).
		FVector2D NMn = FVector2D::ZeroVector, NMx = FVector2D::ZeroVector, TiMn = FVector2D::ZeroVector, TiMx = FVector2D::ZeroVector, HMn = FVector2D::ZeroVector, HMx = FVector2D::ZeroVector, BMn = FVector2D::ZeroVector, BMx = FVector2D::ZeroVector;
		const bool bName = R(TEXT("TARGETMARK_MANNAME1"), NMn, NMx);
		const bool bTitle = R(TEXT("TargtMark_Man1Title"), TiMn, TiMx);
		const bool bHp = R(TEXT("TARGETMARK_HPBACK1"), HMn, HMx);
		const bool bBadge = R(TEXT("TargetMark_Rank1"), BMn, BMx);
		// Верх ВИДИМОЙ верхней строки плашки: у текстового узла — верх узла (узел = строка).
		const float TopLine = (float)(((C->Phase % 2) == 0 && bTitle) ? TiMn.Y : NMn.Y) / Sc;
		const float SignBottom = (float)(FMx.Y / Sc);
		const float Gap = TopLine - SignBottom;
		FVector2D TL, Anchor;
		G->DebugSignPlacement(UserNum, TL, Anchor);
		// Мировые точки: origin актёра, точка плашки (компонент), «верх AABB» (база вывески) — и их проекции.
		FVector2D PlateScr = FVector2D::ZeroVector;
		if (APlayerController* PC = C->W.Get() ? C->W.Get()->GetFirstPlayerController() : nullptr)
		{
			PC->ProjectWorldLocationToScreen(NP->GetComponentLocation(), PlateScr, false);
		}
		UE_LOG(LogAsdaStallPlateTest, Log,
			TEXT("ВЫВЕСКА-ПЛАШКА мир %s: origin Z %.1f | точка плашки Z %.1f (над origin %.1f) экран (%.1f,%.1f) | верх AABB %.1f (+10 = база вывески %.1f над origin) | фикс. высота %d, сдвиг %.1f"),
			*Tag, A->GetActorLocation().Z, NP->GetComponentLocation().Z, NP->GetComponentLocation().Z - A->GetActorLocation().Z,
			PlateScr.X, PlateScr.Y, NP->GetEntityTopZ(), NP->GetEntityTopZ() + 10.f, NP->bUseFixedWorldHeight ? 1 : 0, NP->NameplateTopOffset);
		UE_LOG(LogAsdaStallPlateTest, Log,
			TEXT("ВЫВЕСКА-ПЛАШКА замер %s: точка (%.1f,%.1f) | рамка y %.1f…%.1f | строка названия верх %.1f высота %.1f центр %.1f, центр рамки %.1f | ")
			TEXT("плашка: титул %s%.1f…%.1f, имя %s%.1f…%.1f, HP %s%.1f…%.1f, бейдж %s%.1f…%.1f | зазор %.1f | масштаб %.3f"),
			*Tag, Anchor.X, Anchor.Y, FMn.Y / Sc, FMx.Y / Sc, bTxt ? TMn.Y / Sc : -1.0, LineH, LineCenter, InnerCenter,
			bTitle ? TEXT("") : TEXT("(нет) "), TiMn.Y / Sc, TiMx.Y / Sc, bName ? TEXT("") : TEXT("(нет) "), NMn.Y / Sc, NMx.Y / Sc,
			bHp ? TEXT("") : TEXT("(нет) "), HMn.Y / Sc, HMx.Y / Sc, bBadge ? TEXT("") : TEXT("(нет) "), BMn.Y / Sc, BMx.Y / Sc, Gap, Sc);
		C->Expect(bTxt && FMath::Abs(LineCenter - InnerCenter) <= 1.0f,
			FString::Printf(TEXT("%s: строка названия по центру рамки по вертикали (центр строки %.1f, центр рамки %.1f, Δ %.1f)"),
				*Tag, LineCenter, InnerCenter, LineCenter - InnerCenter));
		// Эталон: низ рамки = точка − (сдвиг + 62) + 25; верх титула/имени MAN1 — точка − 50 / − 34 ⇒ зазор 3 (титул) / 13 (без титула и клана).
		const float Want = ((C->Phase % 2) == 0) ? 3.f : 13.f;
		// Допуск ±2: узел титула/имени генератора MAN1 лежит на 1 px ниже `.JUI` (−49 вместо −50 — замер 08.10), позиция вывески
		//   округляется вниз до пикселя (`FloorToFloat`, как `CControl_SetField74_75` целыми).
		C->Expect(bName && Gap >= Want - 2.f && Gap <= Want + 2.f,
			FString::Printf(TEXT("%s: вывеска НАД плашкой, зазор до верхней строки %.1f px (эталон %.0f ± 2)"), *Tag, Gap, Want));
		C->GapByUser.Add(UserNum * 10 + C->Phase, Gap);
	}

	static void Finish(TSharedRef<FCtx> C)
	{
		UGameInstance* GI = GIOf(C);
		if (C->Gen && GI) { if (UAsdaPetSubsystem* Pet = GI->GetSubsystem<UAsdaPetSubsystem>()) { Pet->EndSendCapture(); } }
		UE_LOG(LogAsdaStallPlateTest, Log, TEXT("ВЫВЕСКА-ПЛАШКА ИТОГ: ok=%d fail=%d"), C->Ok, C->Fail);
		C->SelfScope.Restore();   // ★ В3 S1 (N1(г)): «свой» — как до прогона
		AsdaSelfTestKit::Quit(C->W.Get(), C->bQuit);
	}

	static void RunPhase(TSharedRef<FCtx> C);

	// Ждать состояния 8 у своего и чужого (садятся по клипу), не дольше ~8 с.
	static void WaitSit(TSharedRef<FCtx> C)
	{
		UGameInstance* GI = GIOf(C);
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		const int32 A = WS ? UAsdaKingCrownSubsystem::EtalonActionStateOf(WS->ActorByUserIdx(kSelfUserNum)) : -1;
		const int32 B = WS ? UAsdaKingCrownSubsystem::EtalonActionStateOf(WS->ActorByUserIdx(kOther)) : -1;
		if ((A == 8 && B == 8) || ++C->SitPolls > 40)
		{
			C->Expect(A == 8 && B == 8, FString::Printf(TEXT("фазы «сидит»: состояние своего %d, чужого %d (ждём 8)"), A, B));
			RunPhase(C);
			return;
		}
		After(C, 0.2f, &WaitSit);
	}

	// Замер фазы (после отрисовки) и переход к следующей.
	static void MeasurePhase(TSharedRef<FCtx> C)
	{
		UGameInstance* GI = GIOf(C);
		UAsdaStallSignSubsystem* G = GI ? GI->GetSubsystem<UAsdaStallSignSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!G || !WS) { Finish(C); return; }
		Measure(C, G, WS->ActorByUserIdx(kSelfUserNum), kSelfUserNum, TEXT("свой"));
		Measure(C, G, WS->ActorByUserIdx(kOther), kOther, TEXT("чужой"));
		const float* A = C->GapByUser.Find(kSelfUserNum * 10 + C->Phase);
		const float* B = C->GapByUser.Find(kOther * 10 + C->Phase);
		C->Expect(A && B && FMath::Abs(*A - *B) <= 1.0f,
			FString::Printf(TEXT("фаза %d: свой = чужой (зазор %.1f / %.1f)"), C->Phase, A ? *A : -1.f, B ? *B : -1.f));
		if (C->bShot && GEngine && C->W.Get())
		{
			GEngine->Exec(C->W.Get(), *FString::Printf(TEXT("asda.UI.Shot stallplate_%s_phase%d"), *C->ShotTag, C->Phase));
		}
		After(C, 0.6f, [](TSharedRef<FCtx> C2)
		{
			if (++C2->Phase >= 4) { Finish(C2); return; }
			if (C2->Phase == 2)
			{
				// Фазы «сидит»: чужой — штатный приём 4025/108 (поза лавки), свой — `SetAnimState(Sit)`.
				UGameInstance* GI2 = GIOf(C2);
				UAsdaNetSubsystem* Net2 = GI2 ? AsdaTestNet::Get(GI2) : nullptr;
				UAsdaWorldSubsystem* WS2 = GI2 ? GI2->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
				if (Net2)
				{
					TArray<uint8> B; B.SetNumZeroed(13);
					AsdaBytes::WrI16(B.GetData(), (int16)kOther);
					AsdaBytes::WrI16(B.GetData() + 2, (int16)108);
					AsdaTestNet::DebugFeedPacket(Net2, 4025, B);
				}
				if (AAsdaPlayerCharacter* Me = WS2 ? Cast<AAsdaPlayerCharacter>(WS2->ActorByUserIdx(kSelfUserNum)) : nullptr) { Me->SetAnimState(EAsdaPlayerState::Sit); }
				C2->SitPolls = 0;
				WaitSit(C2);
				return;
			}
			RunPhase(C2);
		});
	}

	// Фаза: титул/клан плашек, проход вывесок, ждать отрисовки.
	static void RunPhase(TSharedRef<FCtx> C)
	{
		UGameInstance* GI = GIOf(C);
		UAsdaStallSignSubsystem* G = GI ? GI->GetSubsystem<UAsdaStallSignSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!G || !WS) { Finish(C); return; }
		SetPhasePlate(WS->ActorByUserIdx(kSelfUserNum), C->Phase);
		SetPhasePlate(WS->ActorByUserIdx(kOther), C->Phase);
		G->UpdateNameStatusList(true);
		G->UpdateStalls(true);
		G->RenderSigns();
		After(C, 0.5f, &MeasurePhase);
	}

	static void Run(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaStallSignSubsystem* G = GI ? GI->GetSubsystem<UAsdaStallSignSubsystem>() : nullptr;
		UAsdaSelfStoreSubsystem* S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!G || !S || !Net || !WS) { C->Expect(false, TEXT("нет подсистем")); Finish(C); return; }
		if (!::IsValid(WS->LocalPlayerActor.Get()) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
		}
		C->SelfScope.Capture(Net, TEXT("asda.Trade.SelfTestStallPlate"));   // ★ В3 S1: снимок «своего» до подмены
		AsdaTestNet::SelectedCharId(Net) = kSelfCharId;
		AsdaTestNet::SetSelfUserNumForDebug(Net, kSelfUserNum);
		AsdaTestNet::SelfLevel(Net) = 30;
		S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		G->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		WS->DebugSpawnFakePeer(kOther, TEXT("Продавец"), FVector2D(258.f, 256.f));
		if (UAsdaPetSubsystem* Pet = GI->GetSubsystem<UAsdaPetSubsystem>())
		{
			C->Gen = Pet->BeginSendCapture([](int32, const TArray<uint8>&, bool, TCHAR) {}, 0) ? 1u : 0u;   // в сеть не уходит ничего
		}
		After(C, 1.0f, [](TSharedRef<FCtx> C2)
		{
			UWorld* W2 = C2->W.Get();
			UGameInstance* GI2 = GIOf(C2);
			UAsdaStallSignSubsystem* G2 = GI2 ? GI2->GetSubsystem<UAsdaStallSignSubsystem>() : nullptr;
			UAsdaNetSubsystem* Net2 = GI2 ? AsdaTestNet::Get(GI2) : nullptr;
			UAsdaWorldSubsystem* WS2 = GI2 ? GI2->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			if (!G2 || !Net2 || !WS2) { Finish(C2); return; }
			PlaceInFront(W2, WS2->ActorByUserIdx(kOther), 350.f, 120.f);
			// Штатный приём 6304: свой и чужой «в лавке» (название — как на снимке владельца).
			AsdaTestNet::DebugFeedPacket(Net2, 6304, Make6304(1, kSelfCharId, TEXT("Магазин 'GM_sitxovski'")));
			AsdaTestNet::DebugFeedPacket(Net2, 6304, Make6304(1, 900000 + kOther, TEXT("Магазин 'Продавец'")));
			G2->PumpPending();
			C2->Expect(WS2->ActorByUserIdx(kSelfUserNum) && WS2->ActorByUserIdx(kOther), TEXT("подготовка: свой (17) и чужой (55) в мире"));
			RunPhase(C2);
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaTradeSelfTestStallPlate(
	TEXT("asda.Trade.SelfTestStallPlate"),
	TEXT("asda.Trade.SelfTestStallPlate [delay=N] [shot] [quit] — ОФЛАЙН: вывеска личной лавки над своим и чужим персонажем (6304 штатным ")
	TEXT("приёмом), живая геометрия: строка названия по центру рамки, зазор вывеска → плашка как у эталона, свой = чужой"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Trade.SelfTestStallPlate"), KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;
		TSharedRef<AsdaStallPlateLocal::FCtx> C = MakeShared<AsdaStallPlateLocal::FCtx>();
		C->W = W;
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { C->bQuit = true; }
			else if (Arg.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { C->bShot = true; }
			else if (Arg.StartsWith(TEXT("tag="))) { C->ShotTag = Arg.Mid(4); }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		if (Delay <= 0.f) { AsdaStallPlateLocal::Run(C); return; }
		AsdaStallPlateLocal::After(C, Delay, [](TSharedRef<AsdaStallPlateLocal::FCtx> C2)
		{
			if (AsdaSelfTestKit::RefuseDeferred(C2->W.Get(), TEXT("asda.Trade.SelfTestStallPlate"), C2->bQuit)) { return; }   // ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте
			AsdaStallPlateLocal::Run(C2);
		});
	}));

#endif // !UE_BUILD_SHIPPING
