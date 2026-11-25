// Copyright (c) 2026 @sitxovski (Discord)
// Реализация AAsdaLootActor — см. AsdaLootActor.h (реверс: Reports/loot_system_re.md).
#include "World/AsdaLootActor.h"
#include "Npc/AsdaMobFxMaterial.h"   // ★ 01.11.2026 (mob_fx_audit): Custom Depth подсеток-эффектов лута
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)

#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "World/AsdaNameplateComponent.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaGradeColor.h"        // ★ единая палитра грейдов (общая с плашками)
#include "AsdaBytes.h"                   // ★ единая читалка hex-колонки DataHex (не-hex ⇒ −1, а не «тихий 0»)
#include "Resources/AsdaTextureSubsystem.h"
#include "AsdaEffectActor.h"             // ★ 13.09.2026: код предмета на эффекте блёсток (конфигуратор свечения)
#include "AsdaFxNames.h"                 // ★ дедуп №2 В1-C (F5): имя BP блёсток лута — одна константа с конфигуратором
#include "Resources/AsdaMaterialNames.h" // ★ дедуп №2 В1-C (F10): имя параметра диффуза

DEFINE_LOG_CATEGORY_STATIC(LogAsdaLoot, Log, All);

AAsdaLootActor::AAsdaLootActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	// Вращающийся узел: эталон крутит модель вокруг Y непрерывно (billboardList_Update@0xF2F2E0).
	Spin = CreateDefaultSubobject<USceneComponent>(TEXT("Spin"));
	Spin->SetupAttachment(Root);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Spin);
	// Масштаб/зеркало как у всех моделей пайплайна (glb метры → см + компенсация лишнего флипа конвертера).
	Mesh->SetRelativeScale3D(FVector(0.01f, -0.01f, 0.01f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(true);

	// Хитбокс пикинга: ловит ТОЛЬКО трейсы видимости (курсор «рука» + клик-подбор).
	PickHitbox = CreateDefaultSubobject<UCapsuleComponent>(TEXT("PickHitbox"));
	PickHitbox->SetupAttachment(Root);
	PickHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickHitbox->SetCollisionObjectType(ECC_WorldDynamic);
	PickHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickHitbox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PickHitbox->SetGenerateOverlapEvents(false);
	PickHitbox->SetCapsuleSize(30.f, 30.f);
	PickHitbox->SetRelativeLocation(FVector(0.f, 0.f, 25.f));

	// Имя предмета: Kind=Npc = только имя (без HP/гильдии/титула); цвет — по грейду (InitFromItemCode).
	//   ЭТАЛОН показывает имя только при hover; ПОРТ по просьбе юзера — ВСЕГДА.
	Nameplate = CreateDefaultSubobject<UAsdaNameplateComponent>(TEXT("Nameplate"));
	Nameplate->SetupAttachment(Root);
	Nameplate->Kind = EAsdaNameplateKind::Npc;
	Nameplate->bShowHp = false;
	Nameplate->bAlwaysShowName = true;
	Nameplate->WorldHeight = 55.f;   // чуть выше модельки (сундук ~30 юнитов)
}

// Таблица цветов грейдов 1:1 (CItemManager_GetDropColor@0x9C45C0; live.dmp @0x2907A28).
//   Сама таблица — в Items/AsdaGradeColor.h (одна на весь порт: лут + плашки/титулы).
FLinearColor AAsdaLootActor::GradeColor(int32 InGrade)
{
	return FAsdaGradeColor::Get(InGrade);
}

// Загрузка StaticMesh дроп-модели по имени (раскладка импорта Items).
static UStaticMesh* LoadDropMesh(const FString& Name)
{
	if (Name.IsEmpty()) { return nullptr; }
	return LoadObject<UStaticMesh>(nullptr,
		*AsdaAssetPaths::LootMeshObject(Name),   // ★ волна 2 п. 7: правило в реестре путей
		nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void AAsdaLootActor::InitFromItemCode(int32 InItemCode, int32 InCount, const FIntPoint& InTile)
{
	ItemCode = InItemCode; Count = InCount; Tile = InTile;
	SpawnedAtS = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Items) { return; }

	FString ModelName;            // .jbm-база модели дропа
	bool bCustomTexture = false;  // типы 0-4/8/9-оружие красятся текстурой <модель>.TEX
	FString DisplayName;
	// Текстура дропа. ЭТАЛОН CEffectMgr_SpawnModelWithItemTexture@0xF2E040 (effects_particles.c:57452-57780):
	//   useCustomTexture!=0 && textureMode(=tooltipFlag200)<=2 → Data\item\texture\<имя модели>.TEX;
	//   useCustomTexture==0 → предмет: imgRecord+36 (TexMale, при g_ItemImgOptionVariant==1) / +100 (TexFemale);
	//                          совел : sowelRecord+0x89.
	// Геометрия грузится через CModel_LoadGeometry@0x119CA00, который текстуры НЕ читает вообще
	//   (слот +496 каждого сабмеша обнуляется) ⇒ строка материала внутри .jbm оригиналом НЕ используется
	//   НИКОГДА. Порт брал текстуру из запечённого MIC (= имя материала .jbm) — отсюда «чужие» текстуры.
	FString TexName;
	int32   TextureMode = 0;      // itemRecord.tooltipFlag200

	if (const FAsdaItemRecord* R = Items->GetItem(ItemCode))
	{
		// ── Обычный предмет (реверс §3): модель по itemType ──
		const int32 V = FMath::Clamp(R->UsageCategory / 10, 0, 2);   // вариант _00/_01/_02 (НЕ грейд!)
		switch (R->ItemType)
		{
		case 0: ModelName = FString::Printf(TEXT("Itm1_n_00_hcap_%02d_00"), V); bCustomTexture = true; break;
		case 1: ModelName = FString::Printf(TEXT("Itm1_n_00_chst_%02d_00"), V); bCustomTexture = true; break;
		case 2: ModelName = FString::Printf(TEXT("Itm1_n_00_pant_%02d_00"), V); bCustomTexture = true; break;
		case 3: ModelName = FString::Printf(TEXT("Itm1_n_00_shoe_%02d_00"), V); bCustomTexture = true; break;
		case 4: ModelName = FString::Printf(TEXT("Itm1_n_00_grov_%02d_00"), V); bCustomTexture = true; break;
		case 8: ModelName = TEXT("Itm1_n_00_shld_00_00"); bCustomTexture = true; break;
		case 9:
		{
			const TCHAR* W = nullptr;
			switch (R->ItemSubType)
			{
			case 1:   W = TEXT("swrd"); break;
			case 2:   W = TEXT("spir"); break;
			case 3:   W = TEXT("thnd"); break;
			case 5:   W = TEXT("stap"); break;
			case 9:   W = TEXT("cbow"); break;
			case 0xA: W = TEXT("lbow"); break;
			case 0xB: W = TEXT("bbow"); break;
			default: break;
			}
			if (W) { ModelName = FString::Printf(TEXT("Itm1_n_00_%s_00_00"), W); bCustomTexture = true; }
			break;
		}
		default: break;
		}
		// default-ветка (вкл. золото 20551 и оружие прочих subType): модель из img-записи.
		if (ModelName.IsEmpty())
		{
			ModelName = Items->GetItemMeshName(ItemCode, /*bMale*/true);   // imgRecord.MeshMale (+4)
		}
		TextureMode = R->TooltipFlag200;
		Grade = R->ItemGrade;
		DisplayName = Items->GetItemName(ItemCode);   // имя из DT_item_list (RU + восстановленные)
		// ★ Категория звука — общей точкой (`GetDropSoundCat`): ею же пользуется выброс из сумки,
		//   иначе у одного предмета на земле и в руках был бы разный звук.
		DropSoundCat = Items->GetDropSoundCat(ItemCode);
	}
	else
	{
		// ── Совел (itemCode вне item-диапазона): модель sowelRecord.MeshEntry2 (+0x69), имя из sowell_list,
		//   цвет dropColorIndex (+0xA9), звук field_AA[0] (+0xAA, из DataHex). ──
		FAsdaSowellRow S;
		if (Items->GetSowell(ItemCode, S))
		{
			ModelName   = S.MeshEntry2;
			// MeshEntry (+0x89) — на деле ИМЯ ТЕКСТУРЫ совела (эталон: CPackPath_SetEntryName(&v34,
			//   sowelRecord->field_89) + ".TEX"); имя поля историческое, менять нельзя — оно завязано
			//   на заголовок CSV DataTable.
			TexName     = S.MeshEntry;
			Grade       = S.GradeColorIndex;
			DisplayName = S.Name;
			// Категория звука самоцвета (+0xAA) — той же общей точкой, что и у обычных предметов.
			const int32 SndCat = Items->GetDropSoundCat(ItemCode);
			if (SndCat >= 0) { DropSoundCat = SndCat; }
		}
		else
		{
			UE_LOG(LogAsdaLoot, Warning, TEXT("[лут] itemCode=%d не найден ни в item_list, ни в sowell_list"), ItemCode);
			DisplayName = FString::Printf(TEXT("item %d"), ItemCode);
		}
	}

	// Меш: основной → фолбэк img-модель → фолбэк сундук (ассет мог быть не импортирован — лог).
	UStaticMesh* M = LoadDropMesh(ModelName);
	if (!M)
	{
		const FString ImgMesh = Items->GetItemMeshName(ItemCode, true);
		M = LoadDropMesh(ImgMesh);
		if (M) { ModelName = ImgMesh; bCustomTexture = false; }
	}
	if (!M)
	{
		UE_LOG(LogAsdaLoot, Warning, TEXT("[лут] модель '%s' (itemCode=%d) не импортирована — фолбэк сундук"), *ModelName, ItemCode);
		ModelName = TEXT("Itm1_n_00_chst_00_00");
		M = LoadDropMesh(ModelName);
		bCustomTexture = false;
	}
	if (M) { Mesh->SetStaticMesh(M); }
	// ★ 01.11.2026 (mob_fx_audit) [ИСПРАВЛЕНИЕ] блики/ленты модели с битом очереди — материал `__mobfx` в ассете меша; здесь только
	//   Custom Depth для «записи глубины». ДО MID текстуры ниже: по имени материала слота (у MID оно другое).
	AsdaMobFx::ApplyDepth(Mesh);

	// ── Текстура дропа (эталон, см. комментарий у объявления TexName) ────────────────────────────
	// Ветка useCustomTexture: имя текстуры == имя модели, но ТОЛЬКО при textureMode<=2.
	if (bCustomTexture)
	{
		TexName = (TextureMode <= 2) ? ModelName : FString();
	}
	// Ветка img-модели: TexName уже взят из img-записи/совела. Если модель подменил фолбэк —
	// оставляем эталонную текстуру предмета (она и красит подставленный меш).
	if (!bCustomTexture && TexName.IsEmpty())
	{
		TexName = Items->GetItemTexName(ItemCode, /*bMale*/true);   // imgRecord.TexMale (+36)
	}

	if (!TexName.IsEmpty())
	{
		const FString TexPath = AsdaAssetPaths::LootTextureObject(TexName);   // ★ волна 2 п. 7
		UAsdaTextureSubsystem* TexMgr = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
		UTexture2D* Tex = TexMgr ? TexMgr->LoadTextureCached(TexPath) : nullptr;
		if (Tex)
		{
			for (int32 mi = 0; mi < Mesh->GetNumMaterials(); ++mi)
			{
				if (UMaterialInterface* Mat = Mesh->GetMaterial(mi))
				{
					if (UMaterialInstanceDynamic* MID = Mesh->CreateDynamicMaterialInstance(mi, Mat))
					{
						MID->SetTextureParameterValue(AsdaMat::kParamTex, Tex);   // ★ В1-C F10
					}
				}
			}
		}
		else
		{
			UE_LOG(LogAsdaLoot, Warning,
				TEXT("[лут] текстура '%s' (itemCode=%d, модель '%s') не импортирована"),
				*TexName, ItemCode, *ModelName);
		}
	}

	// Имя над лутом: ВСЕГДА видно (порт-решение юзера), цвет по грейду.
	Nameplate->DisplayName = DisplayName;
	Nameplate->NameColor = GradeColor(Grade);
	Nameplate->RefreshNameplate();

	// ★ БЛЁСТКИ (эталон: эффект живёт вместе с предметом, гасится при его удалении — НЕ импакт c LifeSpan).
	if (!DropFx)
	{
		// ★ Дедуп №2 В1-C (F5): спавн общей точкой `SpawnByBPName` (тот же путь класса, AlwaysSpawn, владелец — лут).
		//   Родитель BP — `AAsdaEffectActor` (проверено по ассету), поэтому прежний `Cast` больше не нужен.
		if (AAsdaEffectActor* E = AAsdaEffectActor::SpawnByBPName(GetWorld(), AsdaFxBp::kLootDrop, GetActorLocation(), FRotator::ZeroRotator, this))
		{
			DropFx = E;
			DropFx->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
			// ★ 13.09.2026 (конфигуратор свечения): эффект знает свой предмет — переопределение
			//   «блёстки именно этого предмета» ищется по паре (.jeb, код). [ПОРТ]
			E->SetSourceItemId(ItemCode);
		}
		else { UE_LOG(LogAsdaLoot, Warning, TEXT("[лут] BP_LootDropFx не найден — предмет без блёсток")); }
	}

	UE_LOG(LogAsdaLoot, Log, TEXT("[лут] тайл(%d,%d) itemCode=%d ×%d '%s' модель=%s грейд=%d зв.кат=%d"),
		Tile.X, Tile.Y, ItemCode, Count, *DisplayName, *ModelName, Grade, DropSoundCat);
}

void AAsdaLootActor::EndPlay(const EEndPlayReason::Type Reason)
{
	// Гасим блёстки вместе с предметом (эталон CEffectMgr_StopByOwner по хэндлу биллборда +624).
	if (DropFx) { DropFx->Destroy(); DropFx = nullptr; }
	Super::EndPlay(Reason);
}

void AAsdaLootActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Вращение 0.0005 рад/мс = 0.5 рад/с (~28.6°/с), wrap 2π — эталон billboardList_Update@0xF2F2E0.
	SpinAngleRad += 0.5f * DeltaSeconds;
	if (SpinAngleRad > 2.f * PI) { SpinAngleRad -= 2.f * PI; }
	Spin->SetRelativeRotation(FRotator(0.f, FMath::RadiansToDegrees(SpinAngleRad), 0.f));
}
