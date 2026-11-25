// Copyright (c) 2026 @sitxovski (Discord)
#include "UI/AsdaGachaSceneActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Components/SceneCaptureComponent2D.h"
#include "UI/AsdaCaptureGradeIsolation.h"   // ★ 05.11.2026: закрепление насыщенности захвата (ч/б смерти не ложится на гачу)
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"   // проверка r.PostProcessing.PropagateAlpha: без неё альфа кадра мертва
#include "World/AsdaCoord.h"   // ★ единый перевод осей игра↔UE (аудит дублей №3)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGachaScene, Log, All);

FVector AAsdaGachaSceneActor::OrigToUe(const FVector& O)
{
	return AsdaCoord::GameToUe3D(O);
}

namespace
{
	// Путь импорта автоматов (Tools/asda_import_gachya_models.py кладёт меш и клипы в SkeletalMeshes/).
	const TCHAR* kSkelRoot = AsdaAssetPaths::kSkelRootGachya;   // ★ волна 2 п. 7: корень — реестр путей
	const TCHAR* kMachineNames[5] = { TEXT("md_gachya_a_00"), TEXT("md_gachya_b_00"),
	                                  TEXT("md_gachya_c_00"), TEXT("md_gachya_d_00"),
	                                  TEXT("md_gachya_e_00") };

	/** `<корень>/<модель>/SkeletalMeshes/<модель>` — реальная раскладка ассетов на диске. */
	FString MeshPath(int32 Index)
	{
		return FString::Printf(TEXT("%s/%s/SkeletalMeshes/%s"), kSkelRoot, kMachineNames[Index], kMachineNames[Index]);
	}

	/** `<корень>/<модель>/SkeletalMeshes/<модель>anim_0N` — импортёр не ставит разделитель. */
	FString ClipPath(int32 Index, int32 Slot)
	{
		return FString::Printf(TEXT("%s/%s/SkeletalMeshes/%sanim_%02d"),
			kSkelRoot, kMachineNames[Index], kMachineNames[Index], Slot);
	}
}

AAsdaGachaSceneActor::AAsdaGachaSceneActor()
{
	// ★ ВОЛНА 9: корень и капчер создаёт БАЗОВЫЙ `AAsdaPreviewSceneActor` — настройки капчера
	//   (PRM_UseShowOnlyList, выключенные атмосфера/туман, ближняя плоскость) у обеих сцен
	//   эталона одинаковы. Здесь остаётся только то, что у гачи своё.

	// Углы веера: 0 · +0.47123852 рад · −0.47123852 · +0.94247705 · −0.94247705 (uiBuildPortraitCameraMatrices).
	FanAnglesDeg = { 0.f, 27.f, -27.f, 54.f, -54.f };
	// Глаз (Y,Z) по числу автоматов — пять веток `if (pScene[148] == N)` в PetWindow_RenderToTarget.
	EyeYZByCount = { FVector2D(150.f, 100.f),   // N = 1
	                 FVector2D(250.f, 150.f),   // N = 2
	                 FVector2D(250.f, 150.f),   // N = 3
	                 FVector2D(250.f, 500.f),   // N = 4
	                 FVector2D(250.f, 500.f) }; // N = 5

	// Капчер и его общие настройки — в базовом классе. Прежний второй капчер-маска
	// (`SCS_SceneColorHDR`) убран ещё раньше: замер показал его альфу БИТ В БИТ равной альфе
	// цветного RT (`Reports/gacha_alpha_composite.md` §0.1).

	// ★★★ СОБСТВЕННОГО СВЕТА У СЦЕНЫ НЕТ — и в эталоне его тоже нет.
	//   Раньше здесь создавались два `UDirectionalLightComponent` (ключевой 3.0 + заполняющий 1.0)
	//   как приближение — именно они давали пересвет («лутбоксы ярковатые»).
	//   Освещения нет и в материале: подсетки автоматов идут НЕосвещённым вершинным шейдером
	//   оригинала (`mr+0x004 & 0x8 == 0` ⇒ `g_pVtxShaderB/C`, `mov oD0.xyz, c95.w` = 1.0),
	//   поэтому мастера — Unlit с `Emissive = Tex`. Подробности — в шапке заголовка.
}

// ───────────────────────────── камера и render-target ─────────────────────────────

FVector AAsdaGachaSceneActor::EyeOrigForCount(int32 Count) const
{
	// Эталон: eyeY/eyeZ инициализированы 150/150 и переопределяются пятью `if`-ами по числу объектов.
	FVector2D YZ = EyeYZDefault;
	if (EyeYZByCount.IsValidIndex(Count - 1))
	{
		YZ = EyeYZByCount[Count - 1];
	}
	return FVector(0.f, YZ.X, YZ.Y);
}

void AAsdaGachaSceneActor::EnsureRenderTarget()
{
	// В оригинале RT = размер бэкбуфера (`sub_93632B`×`sub_91EBD1` в PetWindow_RenderToTarget),
	// формат 21 = D3DFMT_A8R8G8B8. Здесь — размер игрового вьюпорта, RTF_RGBA8.
	FIntPoint Want = RenderTargetFallback;
	if (const UWorld* W = GetWorld())
	{
		if (UGameViewportClient* VP = W->GetGameViewport())
		{
			FVector2D V(0.f, 0.f);
			VP->GetViewportSize(V);
			if (V.X >= 16.f && V.Y >= 16.f)
			{
				Want = FIntPoint(FMath::RoundToInt(V.X), FMath::RoundToInt(V.Y));
			}
		}
	}

	// ★ Супер-сэмплинг: RT крупнее экрана, кисть ужимает его обратно (честный SSAA).
	//   Проекция контролов при этом продолжает считать ПИКСЕЛИ ЭКРАНА — см. ProjectWorldToRender.
	const float SS = FMath::Clamp(RenderSupersample, 1.f, 4.f);
	const FIntPoint WantRt(FMath::RoundToInt(Want.X * SS), FMath::RoundToInt(Want.Y * SS));

	// ★ ВОЛНА 9: само создание буфера, проверка альфа-канала и кисть — ОБЩЕЕ ЯДРО базового класса.
	//   Гаче принадлежит только ВЫБОР РАЗМЕРА (вьюпорт × супер-сэмплинг): у превью питомца он
	//   фиксированный 240×170, и в этом вся разница между двумя render-target'ами оригинала.
	EnsureRenderTargetSized(WantRt, Want);
}

bool AAsdaGachaSceneActor::SyncRenderTargetToViewport()
{
	// Порт `premiumGatya_CreateRenderTargets@0xB6CA40`: RT всегда размером бэкбуфера. Здесь —
	// размером игрового вьюпорта (× супер-сэмплинг); `EnsureRenderTarget` сам выйдет, если ничего
	// не поменялось.
	const FIntPoint Was = RtSize;
	EnsureRenderTarget();
	if (RtSize == Was)
	{
		return false;
	}
	// Аспект (а с ним и горизонтальный FOV) считается от размера кадра — пересчитать.
	ApplyCamera();
	UE_LOG(LogAsdaGachaScene, Log, TEXT("[гача-сцена] RT пересоздан под вьюпорт: %dx%d → %dx%d"),
		Was.X, Was.Y, RtSize.X, RtSize.Y);
	return true;
}

void AAsdaGachaSceneActor::ApplyCamera()
{
	if (!Capture)
	{
		return;
	}
	const int32 Count = FMath::Max(1, Machines.Num());
	const FVector EyeLocal = OrigToUe(EyeOrigForCount(Count));
	const FVector AtLocal  = OrigToUe(CameraTargetOrig);
	// Верх оригинала (0,1,0) → в UE это +Z, то есть мировой верх ⇒ Roll = 0 (`Rotation()` его и даёт).
	const FRotator Look = (AtLocal - EyeLocal).Rotation();

	// FOVAngle у SceneCapture — ГОРИЗОНТАЛЬНЫЙ. В оригинале задан ВЕРТИКАЛЬНЫЙ 45°, а
	// tan(fovX/2) = tan(fovY/2)·W/H (та же формула, что в FPerspectiveMatrix UE).
	const float Aspect = (ScreenSize.Y > 0) ? (static_cast<float>(ScreenSize.X) / static_cast<float>(ScreenSize.Y)) : 1.f;
	const float TanY = FMath::Tan(FMath::DegreesToRadians(FovYDeg) * 0.5f);
	const float FovX = FMath::Clamp(FMath::RadiansToDegrees(2.f * FMath::Atan(TanY * Aspect)), 5.f, 170.f);

	if (USceneCaptureComponent2D* C = Capture.Get())
	{
		C->SetRelativeLocation(EyeLocal);
		C->SetRelativeRotation(Look);
		C->FOVAngle = FovX;
		C->bOverride_CustomNearClippingPlane = true;
		C->CustomNearClippingPlane = NearClip;

		// ★ Экспозиция фиксируется: кадр почти весь пустой, и адаптация выжигает автоматы в белое
		//   (проверено кадром `Reports/gacha_3d/`). Заодно глушим блум/виньетку/смаз — их
		//   в оригинале нет вовсе, там обычный forward-проход без пост-обработки.
		FPostProcessSettings& PP = C->PostProcessSettings;
		PP.bOverride_AutoExposureMinBrightness = true;
		PP.AutoExposureMinBrightness = FixedExposure;
		PP.bOverride_AutoExposureMaxBrightness = true;
		PP.AutoExposureMaxBrightness = FixedExposure;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = 0.f;
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 0.f;
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 0.f;
		PP.bOverride_MotionBlurAmount = true;
		PP.MotionBlurAmount = 0.f;
		// ★★ ТОН-КРИВАЯ. У оригинала тонмаппинга нет: `PShade` кладёт `Tex.xyz · Diffuse.xyz`
		//   прямо в A8R8G8B8. Плёночная кривая UE поднимает средние тона и гасит насыщенность,
		//   то есть ровно то, на что жалуется владелец. С `ToneCurveAmount = 0` выход капчера
		//   равен sRGB-кодированному линейному цвету — точно как у D3D9-конвейера оригинала.
		PP.bOverride_ToneCurveAmount = true;
		PP.ToneCurveAmount = FMath::Clamp(ToneCurveAmount, 0.f, 1.f);
		PP.bOverride_ExpandGamut = true;
		PP.ExpandGamut = 0.f;
		// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Death.DesatWorldOnly`): насыщенность окна не берётся из объёмов мира — иначе ч/б смерти
		//   (unbound-объём, приоритет 10000) серил сцену гачи. Разбор — `UI/AsdaCaptureGradeIsolation.h`.
		AsdaCaptureGrade::PinSaturation(C);

		// ★★ РЕЗКОСТЬ. Временной фильтр в капчере копит историю, которой у скиннованных автоматов
		//   нет (векторы движения скелеталок в SceneCapture недостоверны) — именно он даёт
		//   «мыло» на движущемся автомате. Сглаживание берём супер-сэмплингом (RenderSupersample).
		C->ShowFlags.SetTemporalAA(bTemporalAAInCapture);
		C->ShowFlags.SetAntiAliasing(bTemporalAAInCapture);
		// Смаз/эффекты, которых в оригинале нет вовсе и которые только мылят кадр.
		C->ShowFlags.SetMotionBlur(false);
		C->ShowFlags.SetDepthOfField(false);
	}
	Capture->CaptureSource = CaptureSourceMode;
}

// ───────────────────────────── сборка сцены ─────────────────────────────

void AAsdaGachaSceneActor::BuildMachines(int32 Count)
{
	Count = FMath::Clamp(Count, 1, 5);
	EnsureRenderTarget();

	for (USkeletalMeshComponent* M : Machines)
	{
		if (M)
		{
			M->DestroyComponent();
		}
	}
	Machines.Reset();
	if (Capture) { Capture->ShowOnlyComponents.Reset(); }

	for (int32 i = 0; i < Count; ++i)
	{
		const FString Path = MeshPath(i);
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Mesh)
		{
			UE_LOG(LogAsdaGachaScene, Warning, TEXT("[гача-сцена] нет меша %s — автомат %d пропущен"), *Path, i);
			continue;
		}
		USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(this);
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
		C->SetSkinnedAssetAndUpdate(Mesh);
		// Именно ОТНОСИТЕЛЬНЫЙ масштаб: сцену целиком можно двигать/масштабировать актором.
		C->SetRelativeScale3D(FVector(MeshScale));
		C->SetCastShadow(false);
		// Кадр снимается капчером, а не игроком: поза должна считаться, даже если актор
		// вне видимости основной камеры, иначе Bone19 «замерзает» и проекция врёт.
		C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		C->SetComponentTickEnabled(true);
		// ★ Автоматы видны ТОЛЬКО капчеру, поэтому стриминг текстур не видит их в основном виде и
		//   может оставить резидентным мелкий мип — кадр выходит мыльным. Держим полный мип.
		C->bForceMipStreaming = bForceFullTextureMips;

		// Мировая матрица автомата: T(0,0,−150)·RotY(θ) в координатах оригинала
		// ⇒ позиция (−R·sinθ, 0, −R·cosθ). Поворот RotY(θ) после перевода осей `GameToUe3D`
		// становится UE-yaw = −θ (выведено подстановкой x=u.y, z=−u.x), плюс доворот меша.
		const float Deg = FanAnglesDeg.IsValidIndex(i) ? FanAnglesDeg[i] : 0.f;
		const float Rad = FMath::DegreesToRadians(Deg);
		const FVector PosOrig(-MachineRadius * FMath::Sin(Rad), 0.f, -MachineRadius * FMath::Cos(Rad));
		C->SetRelativeLocation(OrigToUe(PosOrig));
		C->SetRelativeRotation(FRotator(0.f, -Deg + MeshYawOffset, 0.f));

		// ★ Параметров света в материалы БОЛЬШЕ НЕ ПИШЕМ: у оригинала множитель к текстуре
		//   тождественно 1.0 (неосвещённый шейдер `g_pVtxShaderB/C`, `mov oD0.xyz, c95.w`),
		//   и у мастеров гачи параметров `Ambient`/`LightColor`/`LightDir` нет вовсе.

		Machines.Add(C);
		if (Capture) { Capture->ShowOnlyComponents.Add(C); }
	}

	ApplyCamera();

	const FVector Eye = EyeOrigForCount(FMath::Max(1, Machines.Num()));
	UE_LOG(LogAsdaGachaScene, Log,
		TEXT("[гача-сцена] автоматов %d из %d · RT %dx%d · глаз ориг.(0, %.0f, %.0f) · цель (0, %.0f, %.0f) · FOVx %.2f°"),
		Machines.Num(), Count, RtSize.X, RtSize.Y, Eye.Y, Eye.Z, CameraTargetOrig.Y, CameraTargetOrig.Z,
		Capture ? Capture->FOVAngle : 0.f);
}

void AAsdaGachaSceneActor::PlayClip(int32 MachineIndex, int32 ClipSlot, bool bLoop)
{
	if (!Machines.IsValidIndex(MachineIndex) || !Machines[MachineIndex])
	{
		return;
	}
	const FString Path = ClipPath(FMath::Clamp(MachineIndex, 0, 4), FMath::Clamp(ClipSlot, 0, 4));
	UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Clip)
	{
		UE_LOG(LogAsdaGachaScene, Warning, TEXT("[гача-сцена] нет клипа %s"), *Path);
		return;
	}
	Machines[MachineIndex]->PlayAnimation(Clip, bLoop);
	if (MachineClip.Num() < Machines.Num()) { MachineClip.Init(-1, Machines.Num()); }   // Init, а не Reset: длина фиксирована
	MachineClip[MachineIndex] = ClipSlot;
}

int32 AAsdaGachaSceneActor::GetPlayingClip(int32 MachineIndex) const
{
	return MachineClip.IsValidIndex(MachineIndex) ? MachineClip[MachineIndex] : -1;
}

void AAsdaGachaSceneActor::PlayClipAll(int32 ClipSlot, bool bLoop)
{
	for (int32 i = 0; i < Machines.Num(); ++i)
	{
		PlayClip(i, ClipSlot, bLoop);
	}
}

float AAsdaGachaSceneActor::GetClipLength(int32 MachineIndex, int32 ClipSlot) const
{
	// ★ Длительность берётся ИЗ АССЕТА, а не из константы: в оригинале конец клипа —
	//   это `slot[13] >= slot[11]` (`Game_IsPausedOrMenu@0x99ED60`), то есть сравнение с длительностью
	//   из заголовка `.jbb` (@0x30, миллисекунды), а скорость воспроизведения ровно 1.0.
	const FString Path = ClipPath(FMath::Clamp(MachineIndex, 0, 4), FMath::Clamp(ClipSlot, 0, 4));
	const UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	return Clip ? Clip->GetPlayLength() : 0.f;
}

bool AAsdaGachaSceneActor::IsClipFinished(int32 MachineIndex) const
{
	// Порт `Game_IsPausedOrMenu@0x99ED60`: `текущее время >= длительность`. У незацикленного
	//   одиночного клипа UE сам останавливается на последнем кадре — то же самое делает
	//   зажим `v10 >= keyCount - 1` в `animSampleTransformAtTime@0x11D7D30`.
	if (!Machines.IsValidIndex(MachineIndex) || !Machines[MachineIndex])
	{
		return false;
	}
	const USkeletalMeshComponent* C = Machines[MachineIndex];
	// GetLength() у UAnimSingleNodeInstance не const — берём неконстантный указатель на инстанс.
	UAnimSingleNodeInstance* Inst = Cast<UAnimSingleNodeInstance>(C->GetAnimInstance());
	if (!Inst || !Inst->GetAnimationAsset())
	{
		return false;
	}
	const float Len = Inst->GetLength();
	return Len > 0.f && Inst->GetCurrentTime() >= Len - KINDA_SMALL_NUMBER;
}

// ───────────────────────────── проекция (порт deathRespawnUI_LayoutResurrectPanel) ─────────────────────────────

// ★ ВОЛНА 9: `GetRenderSize()` — в базовом `AAsdaPreviewSceneActor` (тело было тем же).

bool AAsdaGachaSceneActor::ProjectWorldToRender(const FVector& World, FVector2D& OutPos) const
{
	OutPos = FVector2D(kOffScreen, kOffScreen);
	if (!Capture || ScreenSize.X <= 0 || ScreenSize.Y <= 0)
	{
		return false;
	}
	// Базис камеры UE: X — вперёд, Y — вправо, Z — вверх. Тот же, которым капчер строит кадр.
	const FTransform CamT = Capture->GetComponentTransform();
	const FVector Fwd = CamT.GetUnitAxis(EAxis::X);
	const FVector Right = CamT.GetUnitAxis(EAxis::Y);
	const FVector Up = CamT.GetUnitAxis(EAxis::Z);
	const FVector Delta = World - CamT.GetLocation();

	const double Z = FVector::DotProduct(Delta, Fwd);
	// Оригинал считает промахом всё, у чего проекционный z вышел за [0..1] — то есть за near/far.
	if (Z <= static_cast<double>(NearClip) || Z >= static_cast<double>(FarClip))
	{
		return false;
	}
	const double X = FVector::DotProduct(Delta, Right);
	const double Y = FVector::DotProduct(Delta, Up);

	const double TanY = FMath::Tan(FMath::DegreesToRadians(static_cast<double>(FovYDeg)) * 0.5);
	// Аспект берём по ЭКРАНУ: супер-сэмплинг множит обе стороны RT одинаково и аспект не меняет.
	const double Aspect = static_cast<double>(ScreenSize.X) / static_cast<double>(ScreenSize.Y);
	const double TanX = TanY * Aspect;

	const double NdcX = (X / Z) / TanX;
	const double NdcY = (Y / Z) / TanY;
	OutPos.X = static_cast<float>((NdcX * 0.5 + 0.5) * ScreenSize.X);
	OutPos.Y = static_cast<float>((0.5 - NdcY * 0.5) * ScreenSize.Y);
	return true;
}

FVector AAsdaGachaSceneActor::GetSlotAnchorWorld(int32 SlotIndex) const
{
	// Вторая матрица объекта: T(0,0,−350)·RotY(θ), затем подъём по вертикали на 200.
	const float Deg = FanAnglesDeg.IsValidIndex(SlotIndex) ? FanAnglesDeg[SlotIndex] : 0.f;
	const float Rad = FMath::DegreesToRadians(Deg);
	const FVector AnchorOrig(-AnchorRadius * FMath::Sin(Rad), AnchorHeight, -AnchorRadius * FMath::Cos(Rad));
	return GetActorTransform().TransformPosition(OrigToUe(AnchorOrig));
}

bool AAsdaGachaSceneActor::GetSlotScreenPos(int32 SlotIndex, FVector2D& OutPos) const
{
	OutPos = FVector2D(kOffScreen, kOffScreen);
	if (SlotIndex < 0 || SlotIndex >= Machines.Num())
	{
		return false;
	}
	return ProjectWorldToRender(GetSlotAnchorWorld(SlotIndex), OutPos);
}

float AAsdaGachaSceneActor::GetSlotScreenYOffset() const
{
	// `yOffset = 0; if (N == 4) yOffset = -40; if (N == 5) yOffset = -50;`
	const int32 N = Machines.Num();
	if (N == 4) { return -40.f; }
	if (N == 5) { return -50.f; }
	return 0.f;
}

bool AAsdaGachaSceneActor::GetBoneScreenPos(int32 MachineIndex, FVector2D& OutPos) const
{
	OutPos = FVector2D(kOffScreen, kOffScreen);
	if (!Machines.IsValidIndex(MachineIndex) || !Machines[MachineIndex])
	{
		return false;
	}
	const USkeletalMeshComponent* C = Machines[MachineIndex];
	const int32 BoneIdx = C->GetBoneIndex(BallBoneName);
	if (BoneIdx == INDEX_NONE)
	{
		// Оригинал при `IndexByName == -1` просто не двигает контролы — здесь то же самое.
		return false;
	}
	// Кость активного клипа × мировая матрица объекта — ровно `CClass_GetItemByIndex` + `D3DXMatrixMultiply`.
	const FVector BoneWorld = C->GetBoneTransform(BoneIdx).GetLocation();
	return ProjectWorldToRender(BoneWorld, OutPos);
}
