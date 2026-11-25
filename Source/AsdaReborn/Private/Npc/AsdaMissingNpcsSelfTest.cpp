// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ SELFTEST: ВЕРНУВШИЕСЯ «ПРОПАВШИЕ» NPC — `asda.SelfTest.MissingNpcs map=<id> name=<имя своего персонажа> [shot] [quit]`
//   (09.11.2026, пачка 20, баг 7; решения владельца: Agnes (NPC 7) — всем и без префикса; остальные вернувшиеся — только персонажу
//   GM_ и с красным «[MISSING]»; NPC без модели — модель стражника). Разбор — Public/Npc/AsdaNpcGmOnly.h, Public/Npc/AsdaNpcMissing.h.
//   Офлайн (`-AsdaNoNet`), на запечённом уровне карты `map`; имя своего персонажа — штатным офлайн-спавном `Asda.World.FakeSelfSpawn`.
//   Для каждого NPC карты из списка `AsdaNpcLists::kGmOnly` и NPC 7: запечённый актор есть → настоящая запись 4067 (`DebugFeedPacket`) →
//     ВИДИМОСТЬ: Agnes и (при имени GM_) NPC списка — показан, коллизия, модель есть; иначе — скрыт, без коллизии;
//     ЩЕЛЧОК: настоящее событие мыши Slate (или `OnLeftClick` с экранной точкой, если вьюпорт позицию не принял — путь в журнале)
//       по центру модели со своей стороны: видимого выбирает, скрытого — нет;
//     ПОДПИСЬ: у NPC списка kMissingPrefix над головой красный «[MISSING]» (разметка <FC=FFFF2020>) и имя; у Agnes префикса нет;
//     ОКНО РАЗГОВОРА (только видимые): заголовок с тем же префиксом / без него;
//     МОДЕЛЬ-ЗАМЕНА: у одного актора модель временно снимается → `ApplyModelFallback` ставит стражника, затем модель возвращается;
//       список `AsdaNpcLists::kModelFallback` содержит 6/11/44.
//   `shot` — кадры в `Reports/batch20/flammio_npc/` (<ru|en>_<карта>_npc<id>_<gm|player>[_talk].png).
//   Строки «MISSING-NPC ok/FAIL», итог «MISSING-NPC ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "Npc/AsdaNpcGmOnly.h"
#include "Npc/AsdaNpcMissing.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Text/AsdaLang.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaSkillCastLock.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Components/CapsuleComponent.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "AsdaNpcLists.inl"   // встроенный список «только для GM_»

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMissingNpcs, Log, All);

namespace AsdaMissingNpcsTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.MissingNpcs");   // команда
	static const TCHAR* const kPrefix = TEXT("MISSING-NPC");              // префикс журнала
	static constexpr int32 kAgnes = 7;                                   // NPC 7 «Agnes» — всем, без префикса
	// Стоковые серверные NPC, у которых в клиенте порта не было акторов (запечены 09.11): видны всем, без префикса. Forgotten Canyon
	//   21, 108–115, 177; Night Valley 22; шахта 202–204 (решение владельца «yes, but make sure you are not duplicating them»).
	static const int32 kReturnedForAll[] = { 21, 108, 109, 110, 111, 112, 113, 114, 115, 177, 22, 202, 203, 204 };
	static constexpr int32 kRidBase = 8800;                              // renderId удочерений самотеста
	static constexpr float kStand = 350.f;                               // свой персонаж от NPC, ед.

	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;                         // мир
		bool bQuit = false, bShot = false;                // выход / кадры
		int32 MapId = -1;                                 // карта
		FString SelfName;                                 // имя своего персонажа на прогон
		bool bGm = false;                                 // имя на GM_
		int32 Ok = 0, Fail = 0;                           // счётчики
		TArray<TWeakObjectPtr<AAsdaNpcActor>> Npcs;       // проверяемые NPC карты
		int32 Next = 0;                                   // следующий
		TSet<int32> Buried;                               // NPC целиком под мешем карты (высота по .TER, как у эталона)
		TMap<int32, float> FloorZ;                        // меш карты над NPC выше центра модели: Z его поверхности (целимся выше)
		FVector2D StartGame = FVector2D::ZeroVector;      // своя точка

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaMissingNpcs, Log, TEXT("%s ok   %s"), kPrefix, *What); }
			else { ++Fail; UE_LOG(LogAsdaMissingNpcs, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		void Info(const FString& What) const { UE_LOG(LogAsdaMissingNpcs, Log, TEXT("%s info %s"), kPrefix, *What); }
		template <class T> T* Sub() const { return W.IsValid() && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<T>() : nullptr; }
		AAsdaPlayerController* Pc() const { return W.IsValid() ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = Pc(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	static FString Tag(const AAsdaNpcActor* A) { return A ? FString::Printf(TEXT("NPC %d «%s»"), A->NpcId, *A->NpcName.Left(28)) : FString(TEXT("-")); }
	static bool ExpectVisible(const FCtxRef& C, int32 NpcId) { return NpcId == kAgnes || !AsdaNpcGmOnly::IsGmOnly(NpcId, C->MapId) || C->bGm; }

	// Текст контрола имени над головой (TextBlock или rich после префикса).
	static FString PlateName(const AAsdaNpcActor* A)
	{
		const UAsdaNameplateComponent* P = A ? A->Nameplate.Get() : nullptr;
		UUserWidget* W = P ? (P->DebugScreenWidget() ? P->DebugScreenWidget() : P->GetUserWidgetObject()) : nullptr;
		UWidget* Cn = W ? W->GetWidgetFromName(TEXT("TARGETMARK_MANNAME1")) : nullptr;
		if (const UTextBlock* T = Cast<UTextBlock>(Cn)) { return T->GetText().ToString(); }
		if (const URichTextBlock* R = Cast<URichTextBlock>(Cn)) { return R->GetText().ToString(); }
		return FString();
	}

	// Свой персонаж рядом с NPC, камера на него: перебор сторон (8) и длины руки камеры (700, 450, 250) до прямой видимости центра
	//   модели (луч видимости от камеры до центра не упирается в карту) — у развалин Инфериона рука 700 уходила в стену.
	static bool Place(const FCtxRef& C, const AAsdaNpcActor* A)
	{
		AAsdaPlayerCharacter* Ch = C->Ch();
		AAsdaPlayerController* P = C->Pc();
		if (!Ch || !P || !A) { return false; }
		const FVector Target = (A->Mesh && A->Mesh->GetSkeletalMeshAsset())
			? A->Mesh->GetSkeletalMeshAsset()->GetBounds().GetBox().TransformBy(A->Mesh->GetComponentTransform()).GetCenter() : A->GetActorLocation() + FVector(0, 0, 80);
		bool bAny = false;
		for (const float Arm : { 700.f, 450.f, 250.f })
		{
			for (int32 i = 0; i < 8; ++i)
			{
				const float Yaw = 45.f * i;
				const FVector Pt = A->GetActorLocation() + FRotator(0.f, Yaw, 0.f).Vector() * kStand;
				const float G = AsdaGround::GroundZ(C->W.Get(), (float)Pt.X, (float)Pt.Y, NAN);
				if (FMath::IsNaN(G)) { continue; }
				Ch->StopMove();
				P->CancelRoute();
				Ch->SetActorLocation(FVector(Pt.X, Pt.Y, G), false, nullptr, ETeleportType::TeleportPhysics);
				P->SetOrbitAnglesForBench(FRotator::NormalizeAxis(Yaw + 180.f - (float)Ch->GetActorRotation().Yaw), -20.f);
				if (Ch->CameraArm) { Ch->CameraArm->TargetArmLength = Arm; }
				if (P->PlayerCameraManager) { P->PlayerCameraManager->UpdateCamera(0.f); }
				bAny = true;
				FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaMissingNpcsLos), true);
				Q.AddIgnoredActor(Ch);
				Q.AddIgnoredActor(A);
				FHitResult H;
				const FVector Cam = P->PlayerCameraManager ? P->PlayerCameraManager->GetCameraLocation() : Pt;
				if (!C->W->LineTraceSingleByChannel(H, Cam, Target, ECC_Visibility, Q)) { return true; }
			}
		}
		return bAny;
	}

	static TSharedPtr<SViewport> GameVp() { return (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->GetGameViewportWidget() : nullptr; }
	// Настоящее событие мыши Slate (0 — нажатие, 1 — отпускание, 3 — движение).
	static void Mouse(const FVector2D& Px, int32 Kind)
	{
		const TSharedPtr<SViewport> Vp = GameVp();
		if (!Vp.IsValid()) { return; }
		const FGeometry& G = Vp->GetCachedGeometry();
		const FVector2D Abs = FVector2D(G.LocalToAbsolute(FVector2f(Px / FMath::Max(G.Scale, 0.01f))));
		TSet<FKey> Pressed;
		if (Kind == 0) { Pressed.Add(EKeys::LeftMouseButton); }
		const FPointerEvent Ev(0, FSlateApplicationBase::CursorPointerIndex, Abs, Abs, Pressed, Kind == 3 ? EKeys::Invalid : EKeys::LeftMouseButton, 0.f, FModifierKeysState());
		if (Kind == 3) { FSlateApplication::Get().SetCursorPos(Abs); FSlateApplication::Get().ProcessMouseMoveEvent(Ev); Vp->OnMouseMove(G, Ev); }
		else if (Kind == 0) { Vp->OnMouseButtonDown(G, Ev); }
		else { Vp->OnMouseButtonUp(G, Ev); }
	}

	static void Shot(const FCtxRef& C, const AAsdaNpcActor* A, const TCHAR* Suffix)
	{
		if (!C->bShot || !C->W.IsValid() || !A) { return; }
		const FString Map = C->W->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT(""));
		const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / FString::Printf(TEXT("../Reports/batch20/flammio_npc/%s_%s_npc%d_%s%s.png"),
			FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"), *Map, A->NpcId, C->bGm ? TEXT("gm") : TEXT("player"), Suffix));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		C->Info(FString::Printf(TEXT("кадр %s"), *File));
	}

	static void Finish(const FCtxRef& C)
	{
		AsdaSkillCastLock::DebugSetClickPoint(C->Pc(), nullptr);   // R5-fix8: подменённую точку клика не оставлять контроллеру (обрыв прогона)
		if (UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>()) { if (D->IsDialogOpen()) { D->Close(); } }
		if (AAsdaPlayerController* P = C->Pc()) { P->ClearTarget(); }
		const FString Tail = FString::Printf(TEXT(" map=%d name=%s npcs=%d"), C->MapId, *C->SelfName, C->Npcs.Num());
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaMissingNpcs, kPrefix, C->Ok, C->Fail, *Tail);
	}

	static void NextNpc(const FCtxRef& C);

	// Проверки одного NPC: видимость, подпись, щелчок, окно разговора.
	static void RunNpcAfterPlace(const FCtxRef& C, AAsdaNpcActor* A);
	static void RunNpc(const FCtxRef& C, AAsdaNpcActor* A)
	{
		C->Expect(A->bNetSpawned, FString::Printf(TEXT("%s: удочерён пакетом 4067"), *Tag(A)));
		C->Expect(A->Mesh && A->Mesh->GetSkeletalMeshAsset(), FString::Printf(TEXT("%s: модель %s"), *Tag(A),
			(A->Mesh && A->Mesh->GetSkeletalMeshAsset()) ? *A->Mesh->GetSkeletalMeshAsset()->GetName() : TEXT("НЕТ")));
		{
			// Диагностика высоты: актор стоит на .TER (как все NPC порта); земля трассой (с мешами карты) в той же точке.
			const FVector L = A->GetActorLocation();
			const float G = AsdaGround::GroundZ(C->W.Get(), (float)L.X, (float)L.Y, NAN);
			FHitResult H;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaMissingNpcsZ), true);
			Q.AddIgnoredActor(A);
			const bool bHit = C->W->LineTraceSingleByChannel(H, L + FVector(0, 0, 3000), L - FVector(0, 0, 3000), ECC_Visibility, Q);
			C->Info(FString::Printf(TEXT("%s: Z актора %.0f, земля (AsdaGround) %.0f, первое попадание сверху %s %.0f"), *Tag(A), L.Z, G,
				bHit && H.GetActor() ? *H.GetActor()->GetName() : TEXT("-"), bHit ? H.ImpactPoint.Z : 0.f));
			// Модель целиком под мешем карты: высота NPC — по .TER (эталон `worldEntityInitByType` → `terrainGetCellByPos`,
			//   entity_char.c:58714), меш пола выше верха модели — щелчок до неё не доходит и у оригинала. Щелчок не проверяем.
			const float Top = (A->Mesh && A->Mesh->GetSkeletalMeshAsset())
				? (float)A->Mesh->GetSkeletalMeshAsset()->GetBounds().GetBox().TransformBy(A->Mesh->GetComponentTransform()).Max.Z : (float)L.Z;
			const float Mid = (A->Mesh && A->Mesh->GetSkeletalMeshAsset())
				? (float)A->Mesh->GetSkeletalMeshAsset()->GetBounds().GetBox().TransformBy(A->Mesh->GetComponentTransform()).GetCenter().Z : (float)L.Z;
			if (bHit && H.GetActor() && H.GetActor() != A && H.ImpactPoint.Z > Mid && H.ImpactPoint.Z <= Top + 1.f)
			{
				C->FloorZ.Add(A->NpcId, (float)H.ImpactPoint.Z);
				if (Top - H.ImpactPoint.Z < 30.f) { C->Buried.Add(A->NpcId); }   // над полом меньше 30 ед. — щелчок не проверяется (в отчёт)
				C->Info(FString::Printf(TEXT("%s: модель частично под мешем карты %s (%.0f), над полом %.0f ед. — щелчок в видимую часть"),
					*Tag(A), *H.GetActor()->GetName(), H.ImpactPoint.Z, Top - H.ImpactPoint.Z));
			}
			if (bHit && H.GetActor() && H.GetActor() != A && H.ImpactPoint.Z > Top + 1.f)
			{
				C->Buried.Add(A->NpcId);
				C->Info(FString::Printf(TEXT("%s: модель (верх %.0f) целиком под мешем карты %s (%.0f) — высота по .TER, как у эталона; щелчок не проверяется"),
					*Tag(A), Top, *H.GetActor()->GetName(), H.ImpactPoint.Z));
			}
		}
		// Сначала свой персонаж рядом: дальность обзора (`UAsdaViewRangeSubsystem`) прячет далёких NPC — проверка видимости
		//   должна видеть только GM-фильтр, а не дистанцию.
		if (!Place(C, A)) { C->Expect(false, FString::Printf(TEXT("%s: нет точки для своего персонажа"), *Tag(A))); ++C->Next; NextNpc(C); return; }
		TWeakObjectPtr<AAsdaNpcActor> WP(A);
		At(C, 1.2f, [C, WP]() { if (AAsdaNpcActor* A1 = WP.Get()) { RunNpcAfterPlace(C, A1); } else { ++C->Next; NextNpc(C); } });
	}

	static void RunNpcAfterPlace(const FCtxRef& C, AAsdaNpcActor* A)
	{
		const bool bVis = ExpectVisible(C, A->NpcId);
		const bool bPrefix = A->NpcId != kAgnes && AsdaNpcMissing::HasPrefix(A, A->NpcId);
		C->Expect(bVis ? (!A->IsHidden() && A->GetActorEnableCollision()) : (A->IsHidden() && !A->GetActorEnableCollision()),
			FString::Printf(TEXT("%s: %s (скрыт %d, коллизия %d) при имени «%s», свой персонаж в %.0f ед."), *Tag(A), bVis ? TEXT("показан") : TEXT("скрыт и без коллизии"),
				A->IsHidden() ? 1 : 0, A->GetActorEnableCollision() ? 1 : 0, *C->SelfName, C->Ch() ? (float)FVector::Dist2D(C->Ch()->GetActorLocation(), A->GetActorLocation()) : -1.f));
		if (bVis)
		{
			// Разметка после общей точки: «<FC=FFFF2020>» превращается в «<a c="FF2020">» rich-текста.
			const FString Pn = PlateName(A);
			const bool bHasPrefix = Pn.Contains(TEXT("[MISSING]"));
			const bool bRed = Pn.Contains(TEXT("<a c=\"FF2020\">[MISSING]</>"));
			C->Expect(bPrefix ? (bHasPrefix && bRed) : !bHasPrefix, FString::Printf(TEXT("%s: подпись над головой «%s» — %s"), *Tag(A), *Pn,
				bPrefix ? TEXT("красный префикс [MISSING] (FF2020), имя своим цветом") : TEXT("без префикса")));
		}
		AAsdaPlayerController* P = C->Pc();
		P->ClearTarget();
		FVector2D Px;
		const FBox Geo = A->Mesh && A->Mesh->GetSkeletalMeshAsset()
			? A->Mesh->GetSkeletalMeshAsset()->GetBounds().GetBox().TransformBy(A->Mesh->GetComponentTransform()) : FBox(A->GetActorLocation(), A->GetActorLocation() + FVector(0, 0, 100));
		FVector Aim = Geo.GetCenter();
		if (const float* Fz = C->FloorZ.Find(A->NpcId)) { Aim.Z = (*Fz + Geo.Max.Z) * 0.5f; }   // видимая часть над полом
		if (!P->ProjectWorldLocationToScreen(Aim, Px)) { C->Expect(false, FString::Printf(TEXT("%s: не на экране"), *Tag(A))); ++C->Next; NextNpc(C); return; }
		TWeakObjectPtr<AAsdaNpcActor> WA(A);
		At(C, 0.2f, [C, WA, Px, bVis, bPrefix]()
		{
			AAsdaPlayerController* P2 = C->Pc();
			AAsdaNpcActor* A2 = WA.Get();
			if (!P2 || !A2) { ++C->Next; NextNpc(C); return; }
			if (AAsdaPlayerCharacter* Me = C->Ch()) { Me->SetActorEnableCollision(false); }
			Mouse(Px, 3);
			At(C, 0.1f, [C, WA, Px, bVis, bPrefix]()
			{
				AAsdaPlayerController* P3 = C->Pc();
				float MX = -1.f, MY = -1.f;
				P3->GetMousePosition(MX, MY);
				const bool bSlate = FVector2D::Distance(FVector2D(MX, MY), Px) <= 2.0;
				// ★ R5-fix8 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА, шов гейта мирового клика]: щелчок событиями Slate исполняется в следующем
				//   опросе ввода; до него пик и гейт «над окном» держатся за точку щелчка, а не за курсор ОС (мышь пользователя).
				if (bSlate) { AsdaSkillCastLock::DebugSetClickPoint(P3, &Px); Mouse(Px, 0); Mouse(Px, 1); }
				else { AsdaSkillCastLock::DebugWorldClick(P3, Px, /*bDouble=*/false); }
				At(C, 0.3f, [C, WA, Px, bVis, bPrefix, bSlate]()
				{
					AAsdaPlayerController* P4 = C->Pc();
					AAsdaNpcActor* A4 = WA.Get();
					AsdaSkillCastLock::DebugSetClickPoint(P4, nullptr);   // R5-fix8: щелчок исполнен — подмену точки снять
					if (!P4 || !A4) { ++C->Next; NextNpc(C); return; }
					const bool bSel = P4->TargetActor.Get() == A4;
					if (bVis && C->Buried.Contains(A4->NpcId))
					{
						C->Info(FString::Printf(TEXT("%s: щелчок не проверяется — модель под мешем карты (выбран %d)"), *Tag(A4), bSel ? 1 : 0));
					}
					else C->Expect(bVis ? bSel : !bSel, FString::Printf(TEXT("%s: щелчок по модели (%.0f,%.0f, путь %s) — %s"), *Tag(A4), Px.X, Px.Y,
						bSlate ? TEXT("событие мыши Slate") : TEXT("OnLeftClick с экранной точкой"), bVis ? TEXT("выбран") : TEXT("не выбран")));
					Shot(C, A4, TEXT(""));
					if (AAsdaPlayerCharacter* Me = C->Ch()) { Me->SetActorEnableCollision(true); }
					if (!bVis) { At(C, 0.4f, [C]() { ++C->Next; NextNpc(C); }); return; }
					// Окно разговора: заголовок.
					At(C, 0.4f, [C, WA, bPrefix]()
					{
						UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>();
						AAsdaNpcActor* A5 = WA.Get();
						if (!D || !A5) { ++C->Next; NextNpc(C); return; }
						D->OpenForNpc(A5);
						At(C, 0.5f, [C, WA, bPrefix]()
						{
							UAsdaNpcDialogSubsystem* D2 = C->Sub<UAsdaNpcDialogSubsystem>();
							AAsdaNpcActor* A6 = WA.Get();
							FString Title;
							if (D2 && D2->IsDialogOpen())
							{
								// Заголовок NPC_TNAME (TextBlock или rich после префикса) — ищем в окнах вьюпорта по имени.
								for (TObjectIterator<UUserWidget> It; It; ++It)
								{
									if (!It->IsInViewport() && !It->GetParent()) { continue; }
									if (UWidget* Wd = It->GetWidgetFromName(TEXT("NPC_TNAME")))
									{
										if (!Wd->IsVisible()) { continue; }
										if (const URichTextBlock* R = Cast<URichTextBlock>(Wd)) { Title = R->GetText().ToString(); }
										else if (const UTextBlock* T = Cast<UTextBlock>(Wd)) { Title = T->GetText().ToString(); }
										if (!Title.IsEmpty()) { break; }
									}
								}
							}
							if (!D2 || !D2->IsDialogOpen())
							{
								// Сценарий при открытии не выбрал ни одной записи (условия заданий офлайн не выполнены) — окно закрывается
								//   само, как у эталона (`npcDialog`: «подошедших нет»); заголовка нет — проверять нечего.
								C->Info(FString::Printf(TEXT("%s: окно разговора не открылось — сценарий не выбрал записи (эталон)"), *Tag(A6)));
								At(C, 0.2f, [C]() { ++C->Next; NextNpc(C); });
								return;
							}
							C->Expect(bPrefix ? Title.Contains(TEXT("<a c=\"FF2020\">[MISSING]</>")) : (!Title.IsEmpty() && !Title.Contains(TEXT("[MISSING]"))),
								FString::Printf(TEXT("%s: окно разговора открыто, заголовок «%s» — %s"), *Tag(A6), *Title,
									bPrefix ? TEXT("красный префикс [MISSING]") : TEXT("без префикса")));
							Shot(C, A6, TEXT("_talk"));
							At(C, 0.4f, [C]()
							{
								if (UAsdaNpcDialogSubsystem* D3 = C->Sub<UAsdaNpcDialogSubsystem>()) { if (D3->IsDialogOpen()) { D3->Close(); } }
								++C->Next;
								NextNpc(C);
							});
						});
					});
				});
			});
		});
	}

	// Модель-замена: у одного актора модель временно снимается.
	static void RunFallback(const FCtxRef& C)
	{
		C->Expect(AsdaNpcMissing::IsModelFallbackId(6) && AsdaNpcMissing::IsModelFallbackId(11) && AsdaNpcMissing::IsModelFallbackId(44),
			TEXT("список модели-замены kModelFallback содержит 6/11/44 (моделей нет ни у нас, ни у доноров)"));
		AAsdaNpcActor* A = C->Npcs.Num() ? C->Npcs[0].Get() : nullptr;
		if (A && A->Mesh)
		{
			USkeletalMesh* Was = A->Mesh->GetSkeletalMeshAsset();
			A->Mesh->SetSkeletalMeshAsset(nullptr);
			const bool bApplied = AsdaNpcMissing::ApplyModelFallback(A);
			const USkeletalMesh* Now = A->Mesh->GetSkeletalMeshAsset();
			C->Expect(bApplied && Now && Now->GetName() == TEXT("npc_m_h_soldier-a_00"),
				FString::Printf(TEXT("модель-замена: у %s без модели — %s"), *Tag(A), Now ? *Now->GetName() : TEXT("НЕТ")));
			if (C->bShot && !A->IsHidden())
			{
				Place(C, A);
				TWeakObjectPtr<AAsdaNpcActor> WA(A);
				TWeakObjectPtr<USkeletalMesh> WWas(Was);
				At(C, 0.3f, [C, WA, WWas]()
				{
					Shot(C, WA.Get(), TEXT("_fallback"));
					At(C, 0.4f, [C, WA, WWas]() { if (WA.IsValid() && WA->Mesh) { WA->Mesh->SetSkeletalMeshAsset(WWas.Get()); } Finish(C); });
				});
				return;
			}
			A->Mesh->SetSkeletalMeshAsset(Was);
		}
		Finish(C);
	}

	static void NextNpc(const FCtxRef& C)
	{
		if (C->Next >= C->Npcs.Num()) { RunFallback(C); return; }
		AAsdaNpcActor* A = C->Npcs[C->Next].Get();
		if (!A) { ++C->Next; NextNpc(C); return; }
		RunNpc(C, A);
	}

	static void Run(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		UAsdaWorldSubsystem* WS = C->Sub<UAsdaWorldSubsystem>();
		if (!W || !WS || !C->Pc() || !C->Ch()) { C->Expect(false, TEXT("мир, подсистема мира, контроллер и свой персонаж есть")); Finish(C); return; }
		C->Expect(AsdaNpcGmOnly::SelfIsGm(W->GetGameInstance()) == C->bGm, FString::Printf(TEXT("своё имя «%s» — %s"), *C->SelfName, C->bGm ? TEXT("GM_") : TEXT("обычный игрок")));
		// Ожидаемые NPC карты: Agnes и NPC списка «только для GM_» с картой этой карты (npc_list в DT_NpcQuestNpc).
		TSet<int32> Want;
		for (const AsdaNpcLists::FNpcOnMap& E : AsdaNpcLists::kGmOnly) { if (E.MapId == C->MapId) { Want.Add(E.NpcId); } }
		for (const int32 Id : kReturnedForAll) { Want.Add(Id); }
		Want.Add(kAgnes);
		int32 Idx = 0;
		TSet<int32> Found;
		for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
		{
			AAsdaNpcActor* A = *It;
			if (!A || A->bNetSpawned || !Want.Contains(A->NpcId)) { continue; }
			Found.Add(A->NpcId);
			C->Npcs.Add(A);
			if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>())
			{
				TArray<uint8> Body; Body.SetNumZeroed(108);
				AsdaBytes::WrI16(Body.GetData() + 0, (int16)(kRidBase + Idx++));
				AsdaBytes::WrI16(Body.GetData() + 2, (int16)A->NpcId);
				AsdaBytes::WrI16(Body.GetData() + 8, (int16)A->TileX);
				AsdaBytes::WrI16(Body.GetData() + 10, (int16)A->TileZ);
				Net->DebugFeedPacket(4067, Body);
			}
		}
		FString List;
		for (int32 Id : Found) { List += FString::Printf(TEXT(" %d"), Id); }
		C->Expect(C->Npcs.Num() > 0, FString::Printf(TEXT("карта %d: запечённых вернувшихся NPC %d —%s"), C->MapId, C->Npcs.Num(), *List));
		At(C, 1.2f, [C]() { NextNpc(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMissingNpcs(
	AsdaMissingNpcsTest::kCmd,
	TEXT("asda.SelfTest.MissingNpcs map=<id> name=<self name> [shot] [quit] — returned NPC (Agnes for all; others GM_ only, red [MISSING]): ")
	TEXT("visibility, click, nameplate, talk title, guard model fallback. Offline, baked level of `map`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaMissingNpcsTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		int32 MapId = -1;
		FString Name = TEXT("Tester");
		for (const FString& A : Args)
		{
			if (A.StartsWith(TEXT("map="))) { MapId = FCString::Atoi(*A.Mid(4)); }
			if (A.StartsWith(TEXT("name="))) { Name = A.Mid(5); }
		}
		if (MapId < 0) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("map=<id> is required")); return; }
		// Свой персонаж с заданным именем — штатный офлайн-крюк (имя кладётся в общую точку «своё имя» мира).
		if (!KA.Has(TEXT("selfready")) && W && GEngine)
		{
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), MapId));
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), *Name));
			const FString Again = FString::Printf(TEXT("%s map=%d name=%s selfready%s%s"), kCmd, MapId, *Name, KA.Has(TEXT("shot")) ? TEXT(" shot") : TEXT(""), KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		C->bShot = KA.Has(TEXT("shot"));
		C->MapId = MapId;
		C->SelfName = Name;
		C->bGm = Name.StartsWith(TEXT("GM_"), ESearchCase::IgnoreCase);
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.5f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
