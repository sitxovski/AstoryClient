// Copyright (c) 2026 @sitxovski (Discord)
// Проекция надписей над головой по камере этого же кадра. Разбор оригинала и порядка кадра — в `Public/World/AsdaOverheadLate.h`.
#include "World/AsdaOverheadLate.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "SceneView.h"
#include "UnrealClient.h"

// ★ Рубильник. 1 — [ОРИГИНАЛ] надписи проецируются камерой того кадра, которым рисуются (плашки — в TG_PostUpdateWork,
//   облачка и квестовая «!» — после обновления камеры); 0 — прежний порядок порта (плашки на 2 кадра позади камеры, облачка на 1).
static TAutoConsoleVariable<int32> CVarOverheadLateProject(
	TEXT("asda.Overhead.LateProject"), 1,
	TEXT("1 — плашки имён/HP, облачка чата и квестовые метки считаются по камере текущего кадра (без «плавания» при повороте и ")
	TEXT("наезде камеры); 0 — прежний порядок (отставание на 1–2 кадра)"),
	ECVF_Default);

namespace
{
	// Кэш кадровой проекции: мир, кадр и метка кэша камеры, на которых он собран.
	TWeakObjectPtr<const UWorld> GProjWorld;
	uint64 GProjFrame = MAX_uint64;
	float  GProjCamTime = -1.f;
	bool   GProjOk = false;
	FAsdaFrameProjection GProj;
}

bool AsdaOverheadLate::IsOn()
{
	return CVarOverheadLateProject.GetValueOnGameThread() != 0;
}

const FAsdaFrameProjection* AsdaOverheadLate::FreshProjection(const UWorld* W)
{
	if (!W) { return nullptr; }
	APlayerController* PC = W->GetFirstPlayerController();
	const APlayerCameraManager* PCM = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (!PCM) { return nullptr; }
	// Камера обновлена В ЭТОМ кадре ⇔ метка её кэша = время мира этого кадра (`APlayerCameraManager::FillCameraCache` пишет
	//   `World->TimeSeconds`). До `UpdateCameraManager` метка — прошлого кадра: данные устарели, отдаём nullptr.
	const float CamTime = PCM->GetCameraCacheTime();
	if (CamTime != (float)W->GetTimeSeconds()) { return nullptr; }
	if (GProjWorld.Get() == W && GProjFrame == GFrameCounter && GProjCamTime == CamTime) { return GProjOk ? &GProj : nullptr; }
	GProjWorld = W;
	GProjFrame = GFrameCounter;
	GProjCamTime = CamTime;
	GProjOk = false;
	// Тот же сбор, что в `UAsdaVisibilitySubsystem::Tick` (данные вида, размер вьюпорта, DPI) — только ПОСЛЕ обновления камеры.
	const ULocalPlayer* LP = PC->GetLocalPlayer();
	if (!LP || !LP->ViewportClient || !LP->ViewportClient->Viewport) { return nullptr; }
	FSceneViewProjectionData PD;
	if (!LP->GetProjectionData(LP->ViewportClient->Viewport, PD)) { return nullptr; }
	GProj.ViewProj = PD.ComputeViewProjectionMatrix();
	GProj.ViewRect = PD.GetConstrainedViewRect();
	FVector2D VpPx = FVector2D::ZeroVector;
	if (UGameViewportClient* GVC = W->GetGameViewport()) { GVC->GetViewportSize(VpPx); }
	GProj.ViewportSize = VpPx;
	GProj.DpiScale = (VpPx.X > 1.f && VpPx.Y > 1.f)
		? GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(FMath::TruncToInt32(VpPx.X), FMath::TruncToInt32(VpPx.Y)))
		: 1.f;
	GProj.bValid = true;
	GProjOk = true;
	return &GProj;
}

FDelegateHandle AsdaOverheadLate::AddPostCamera(TFunction<void(UWorld*, float)> Fn)
{
	return FWorldDelegates::OnWorldPostActorTick.AddLambda([Fn = MoveTemp(Fn)](UWorld* W, ELevelTick /*TickType*/, float Dt)
	{
		if (W && W->IsGameWorld() && !W->IsPaused()) { Fn(W, Dt); }
	});
}

void AsdaOverheadLate::RemovePostCamera(FDelegateHandle& H)
{
	if (H.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(H); }
	H.Reset();
}
