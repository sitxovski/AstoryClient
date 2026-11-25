// Copyright (c) 2026 @sitxovski (Discord)
// Реализация значка «откат навыка истёк». Устройство, модель и исправления — в шапке `Public/UI/AsdaCooldownReadyIconSubsystem.h`,
//   разбор эталона — `Reports/skill_cooldown_ready_icon/01_etalon.md`.
#include "UI/AsdaCooldownReadyIconSubsystem.h"
#include "UI/AsdaUiVisibilitySubsystem.h"   // ★ 21.10.2026: Alt+Z — флаг «интерфейс скрыт» (владелец)

#include "AsdaNetSubsystem.h"
#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaAssetPaths.h"
#include "Resources/AsdaGameUserSettings.h"
#include "World/AsdaWorldKind.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Images/SImage.h"
#include "Styling/SlateBrush.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCdReady, Log, All);

namespace AsdaCdReady
{
	// Константы `0xB4FA20` / `0xB4F180` (байты `.rdata`, разбор §2.4–2.5).
	constexpr float kStartSize = 256.f;     // `0x43800000` при добавлении
	constexpr float kHoldSize = 32.f;       // порог фазы 0 → 1 (`@0x13CA108`)
	constexpr float kShrinkPerSec = 600.f;  // фаза 0: 20.0 × (30 / FPS) за кадр
	constexpr float kFadePerSec = 180.f;    // фаза 2: 6.0 × (30 / FPS) за кадр
	constexpr float kAlphaK = 255.f / 224.f;   // 1.1383928 (`@0x13CA134`)
	constexpr float kAlphaMax = 100.f;      // потолок альфы (`@0x1396D8C`), из 255
	constexpr float kHoldMs = 500.f;        // фаза 1 → 2: `timeGetTime() − t > 0x1F4`
	constexpr float kCenterDy = 100.f;      // центр квада ниже середины кадра на 100 px (`fadd [100.0]`, 0xB4F292)
	// Слой: над табличками/облачками (UMG z −1 → оверлей 9) и под окнами (z ≥ 0 → оверлей ≥ 10) — эталон рисует значки в
	//   блоке бита 128 ПОСЛЕ мировых окон и ДО окон. [ПОРТ] цифры урона порта (z 90) остаются выше — у эталона они ниже значка.
	constexpr int32 kCanvasZ = 10;
	// UI-материал с линейным сэмплером: эталон перед квадом ставит MIN/MAG = LINEAR (`MiniMap_RenderBigMap`), а текстуры иконок
	//   JUI импортированы с фильтром NEAREST (1:1 для окон) — растянутая до 256 px иконка без него была бы ступенчатой.
	static const TCHAR* kIconMaterial = TEXT("/Game/AsdaAssets/UI/Materials/M_UI_CooldownReadyIcon.M_UI_CooldownReadyIcon");
}

void UAsdaCooldownReadyIconSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		HReady = Net->OnSkillCooldownReady.AddUObject(this, &UAsdaCooldownReadyIconSubsystem::OnCooldownReady);
	}
}

void UAsdaCooldownReadyIconSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		if (HReady.IsValid()) { Net->OnSkillCooldownReady.Remove(HReady); }
	}
	HReady.Reset();
	// Сначала канвас с вьюпорта, потом записи: картинки держат сырые указатели на кисти записей (ревизия М).
	if (Canvas.IsValid())
	{
		if (UGameViewportClient* VP = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr)
		{
			VP->RemoveViewportWidgetContent(Canvas.ToSharedRef());
		}
		Canvas->ClearChildren();
		Canvas.Reset();
	}
	Entries.Reset();
	HeldMids.Reset();
	Super::Deinitialize();
}

void UAsdaCooldownReadyIconSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// ★ [ИСПРАВЛЕНИЕ] у эталона список живёт до конца процесса. Смена карты — не трогаем (записи живут ~1 с, как у оригинала).
	if (!AsdaIsFullSessionReset(Reason)) { return; }
	if (Canvas.IsValid()) { Canvas->ClearChildren(); }
	Entries.Reset();
	HeldMids.Reset();
}

bool UAsdaCooldownReadyIconSubsystem::IsOptionOn()
{
	// Байт 0x15CB4E6 эталона (`NewConfig.ini [QuickSlot] Show_CoolTimeEndEffect`, нет ключа ⇒ вкл) = профиль порта,
	//   меняется по «ОК» окна настроек (`SaveStateToConfig`). Нет профиля/поля — умолчание эталона: включено.
	const UAsdaGameUserSettings* GUS = UAsdaGameUserSettings::Get();
	return !GUS || !GUS->InterfaceOther.IsValidIndex(8) || GUS->InterfaceOther[8] != 0;
}

void UAsdaCooldownReadyIconSubsystem::OnCooldownReady(int32 SkillId)
{
	// `pktHandlerCharFxStop`: `if (byte[0x15CB4E6]) CMap::AddCdIcon(id)`; проверок id, состояния и дублей у эталона нет.
	if (!IsOptionOn())
	{
		UE_LOG(LogAsdaCdReady, Verbose, TEXT("[откат] 5274 навык %d: опция 30479 выключена — значка нет"), SkillId);
		return;
	}
	AddIcon(SkillId);
}

void UAsdaCooldownReadyIconSubsystem::AddIcon(int32 SkillId)
{
	FEntry E;
	E.SkillId = SkillId;
	// Иконка: запись навыка по индексу (+36) → `<имя>.tex` целиком. Пустое имя или нет текстуры — у эталона ничего не рисуется.
	const UAsdaSkillDatabase* Db = GetGameInstance()->GetSubsystem<UAsdaSkillDatabase>();
	const FAsdaSkillDef* D = Db ? Db->Get(SkillId) : nullptr;
	UTexture2D* Tex = nullptr;
	if (D && !D->IconName.IsEmpty())
	{
		if (UAsdaTextureSubsystem* TexMgr = AsdaTexMgr()) { Tex = TexMgr->LoadTextureCached(AsdaAssetPaths::JuiTextureObject(D->IconName)); }
	}
	EnsureCanvas();
	if (Tex && Canvas.IsValid())
	{
		E.Brush = MakeShared<FSlateBrush>();
		// Кисть — материал с линейным сэмплером и текстурой иконки; нет материала — сама текстура (с предупреждением один раз).
		UMaterialInstanceDynamic* Mid = nullptr;
		if (UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, AsdaCdReady::kIconMaterial))
		{
			Mid = UMaterialInstanceDynamic::Create(Master, this);
			Mid->SetTextureParameterValue(TEXT("Tex"), Tex);
		}
		else
		{
			static bool bWarned = false;
			if (!bWarned) { bWarned = true; UE_LOG(LogAsdaCdReady, Warning, TEXT("[откат] нет материала %s — значок без линейного фильтра"), AsdaCdReady::kIconMaterial); }
		}
		E.Mid = Mid;
		if (Mid) { HeldMids.Add(Mid); E.Brush->SetResourceObject(Mid); } else { E.Brush->SetResourceObject(Tex); }
		E.Brush->DrawAs = ESlateBrushDrawType::Image;
		E.Brush->ImageSize = FVector2D(AsdaCdReady::kStartSize, AsdaCdReady::kStartSize);   // вся текстура, растянута до s×s
		SAssignNew(E.Image, SImage).Image(E.Brush.Get()).Visibility(EVisibility::Collapsed);
		Canvas->AddSlot()
			.Anchors(FAnchors(0.f, 0.f))
			.AutoSize(false)
			.Offset(FMargin(0.f, 0.f, AsdaCdReady::kStartSize, AsdaCdReady::kStartSize))
			.Expose(E.Slot)
			[ E.Image.ToSharedRef() ];   // слоты в порядке прихода: последняя запись рисуется сверху, как список эталона
	}
	UE_LOG(LogAsdaCdReady, Log, TEXT("[откат] навык %d: значок «откат истёк» (иконка «%s» %s), записей %d"), SkillId,
		D ? *D->IconName : TEXT(""), Tex ? TEXT("есть") : TEXT("НЕТ — не рисуется"), Entries.Num() + 1);
	Entries.Add(MoveTemp(E));
	SyncWidgets();
}

void UAsdaCooldownReadyIconSubsystem::Tick(float DeltaTime)
{
	StepAll(DeltaTime * 1000.f);
	SyncWidgets();
}

void UAsdaCooldownReadyIconSubsystem::StepAll(float DeltaMs)
{
	using namespace AsdaCdReady;
	const float Dt = DeltaMs / 1000.f;
	for (int32 i = 0; i < Entries.Num(); )
	{
		FEntry& E = Entries[i];
		// Порядок проверок за кадр — как у `0xB4FA20`: блок фазы 0, затем проверка фазы 1, затем шаг фазы 2.
		bool bEnteredHold = false;
		if (E.Phase == 0)
		{
			E.Size -= kShrinkPerSec * Dt;
			E.Alpha = FMath::Abs((E.Size - kHoldSize) * kAlphaK - 255.f);
			if (E.Alpha > kAlphaMax) { E.Alpha = kAlphaMax; }
			if (E.Alpha < 0.f) { E.Alpha = 0.f; }
			if (E.Size < kHoldSize) { E.Size = kHoldSize; E.Phase = 1; E.HoldMs = 0.f; bEnteredHold = true; }
		}
		if (E.Phase == 1)
		{
			// `timeGetTime() − t > 500`: в кадре входа разность 0 — переход не раньше следующих кадров.
			if (!bEnteredHold) { E.HoldMs += DeltaMs; }
			if (E.HoldMs > kHoldMs) { E.Phase = 2; }
		}
		if (E.Phase == 2)
		{
			E.Size -= kFadePerSec * Dt;   // альфа не пересчитывается
			if (E.Size < 0.f)
			{
				if (E.Image.IsValid() && Canvas.IsValid()) { Canvas->RemoveSlot(E.Image.ToSharedRef()); }
				if (E.Mid) { HeldMids.Remove(E.Mid); }
				Entries.RemoveAt(i);
				continue;   // кадр удаления не рисуется
			}
		}
		++i;
	}
}

bool UAsdaCooldownReadyIconSubsystem::IsDrawAllowed() const
{
	// Эталон: `MiniMap_RenderBigMap` зовётся только при `CMap+0x248` (мини-карта развёрнута), в кадре мира.
	const UGameInstance* GI = GetGameInstance();
	const UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (!W || AsdaWorldKind::IsLoginScene(W)) { return false; }
	const UAsdaMinimapSubsystem* Mm = GI->GetSubsystem<UAsdaMinimapSubsystem>();
	// ★ 21.10.2026: и только при показанном интерфейсе (бит 128 `CConfig+0x38`, Alt+Z) — `uiMiniMapRender` стоит в блоке бита.
	return Mm && Mm->IsMinimapVisible() && UAsdaUiVisibilitySubsystem::IsUiShownFor(this);
}

void UAsdaCooldownReadyIconSubsystem::SyncWidgets()
{
	if (!Canvas.IsValid()) { return; }
	const bool bDraw = IsDrawAllowed();
	FVector2D Vp = FVector2D::ZeroVector;
	if (UGameViewportClient* VP = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr) { VP->GetViewportSize(Vp); }
	// `0xB4F180`: x = (W − s)·0.5, y = (H − s)·0.5 + 100 — центр всегда (W/2, H/2 + 100), квад сжимается к центру.
	const float Cx = (float)(int32)Vp.X * 0.5f, Cy = (float)(int32)Vp.Y * 0.5f + AsdaCdReady::kCenterDy;
	for (const FEntry& E : Entries)
	{
		if (!E.Image.IsValid()) { continue; }
		const float S = FMath::Max(0.f, E.Size);
		E.Image->SetVisibility(bDraw ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		E.Image->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, FMath::Clamp(E.Alpha, 0.f, 255.f) / 255.f));   // `(a<<24)|0xFFFFFF`
		E.Brush->ImageSize = FVector2D(S, S);
		if (E.Slot) { E.Slot->SetOffset(FMargin(Cx - S * 0.5f, Cy - S * 0.5f, S, S)); }   // позиция и размер — слот канваса
	}
}

void UAsdaCooldownReadyIconSubsystem::EnsureCanvas()
{
	if (Canvas.IsValid()) { return; }
	UGameViewportClient* VP = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	if (!VP) { return; }
	SAssignNew(Canvas, SConstraintCanvas).Visibility(EVisibility::HitTestInvisible);
	VP->AddViewportWidgetContent(Canvas.ToSharedRef(), AsdaCdReady::kCanvasZ);
}

bool UAsdaCooldownReadyIconSubsystem::DebugEntry(int32 I, float& OutSize, float& OutAlpha, int32& OutPhase, int32& OutSkillId) const
{
	if (!Entries.IsValidIndex(I)) { return false; }
	OutSize = Entries[I].Size; OutAlpha = Entries[I].Alpha; OutPhase = Entries[I].Phase; OutSkillId = Entries[I].SkillId;
	return true;
}

bool UAsdaCooldownReadyIconSubsystem::DebugSlotRect(int32 I, FVector2D& OutPos, FVector2D& OutSize) const
{
	if (!Entries.IsValidIndex(I) || !Entries[I].Slot) { return false; }
	const FMargin M = Entries[I].Slot->GetOffset();
	OutPos = FVector2D(M.Left, M.Top);
	OutSize = FVector2D(M.Right, M.Bottom);
	return true;
}

bool UAsdaCooldownReadyIconSubsystem::DebugHasImage(int32 I) const
{
	return Entries.IsValidIndex(I) && Entries[I].Image.IsValid();
}

bool UAsdaCooldownReadyIconSubsystem::DebugImageVisible(int32 I) const
{
	return DebugHasImage(I) && Entries[I].Image->GetVisibility() == EVisibility::HitTestInvisible;
}
