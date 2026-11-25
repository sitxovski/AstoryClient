// Copyright (c) 2026 @sitxovski (Discord)
// Реализация AAsdaPetActor — см. AsdaPetActor.h (реверс: Reports/pet_system_re.md).
#include "World/AsdaPetActor.h"
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

#include "World/AsdaNameplateComponent.h"
#include "World/AsdaPlayerCharacter.h"   // ★ 22.10.2026: «хвост» сетевой поправки владельца (GetVisualTail) — питомец идёт за ТЕЛОМ
#include "World/AsdaGroundTrace.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR — дев-ключ вне Shipping

// ★ R3-c чистка 12.11.2026: категория одна на модуль — определение в Pet/AsdaPetSubsystem.cpp (после разреза она с внешним
//   связыванием); прежде здесь стоял второй экземпляр с тем же именем (DEFINE_LOG_CATEGORY_STATIC). Имя и уровень те же.
DECLARE_LOG_CATEGORY_EXTERN(LogAsdaPet, Log, All);

// ★★★ [ИСПРАВЛЕНИЕ] 29.10.2026 — ВЫСОТА ПАРЯЩЕГО ПИТОМЦА ОТ ВЛАДЕЛЬЦА, А НЕ ОТ ЗЕМЛИ (дефект владельца: SnowLion 31 «провален
//   под землю», `Reports/bugs_2026-10-29/pet31_snowlion_sunk.webp`; разбор — `Reports/pet31_snowlion/GROUND.md`).
//   ЭТАЛОН (`CPetObject::UpdateTransformAndPlate`, AsdaStory_game.c:594813-594832): при bHover (PetTable+96)
//   `pos.y += HoverHeight` прибавляется к Y МАТРИЦЫ питомца = Y владельца + 20 (смещение Translate(−30,+20,−40) × матрица
//   владельца), рельеф не читается; только наземного (`bHover = 0`) прижимает `terrainGetCellByPos`.
//   ПОРТ ставил парящего на `земля под питомцем + HoverHeight`: терял +20 и брал не ту опору. У пород 17–19, 31 (львы,
//   HoverHeight −20) это давало «земля − 20» вместо «ноги владельца», у остальных парящих — на 20 ниже оригинала.
//   1 (по умолчанию) — как эталон: Z = Z тела владельца + FollowOffset.Z (20) + HoverHeight. 0 — прежний порт (земля + HoverHeight).
static ASDA_DEV_CVAR(int32, CVarPetHoverFromOwner,
	TEXT("asda.Pet.HoverFromOwner"), 1,
	TEXT("1 (по умолчанию) — парящий питомец (bHover) стоит на высоте владельца + 20 + HoverHeight, как в оригинале ")
	TEXT("(AsdaStory_game.c:594813-594832). 0 — прежний порт: земля под питомцем + HoverHeight (львы 17–19/31 уходили под землю на 20)."),
	ECVF_Default);

// ★ ЦВЕТ ИМЕНИ ПИТОМЦА — по эталону, НЕ как у NPC.
//   uiCharPreview_updateRender @0xFAE3D0 (CPetObject::UpdateTransformAndPlate) после создания плашки
//   из шаблона 1661 делает: CControl_SetField923(plate, -5051137) — цвет текста контрола.
//   -5051137 = 0xFFB2ECFF (ARGB) → RGB (0xB2,0xEC,0xFF) — бледно-голубой. Для сравнения, у NPC/мобов
//   там же стоит -8818 = 0xFFFFDD8E (кремово-золотой) — то есть пет ЯВНО другого цвета.
//   ЗАКОН sRGB→linear: JuiSkin/JUI хранит цвет в sRGB, UE ждёт linear → FLinearColor(FColor) делает
//   декодирование sRGB (FLinearColor::FromSRGBColor), поэтому конструируем именно через FColor.
static const FColor kPetNameColorSRGB(0xB2, 0xEC, 0xFF, 0xFF);

AAsdaPetActor::AAsdaPetActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	MeshRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MeshRoot"));
	MeshRoot->SetupAttachment(Root);
	// Тот же масштаб/зеркало, что у персонажей и мобов (glTF-метры → см, компенсация хиральности).
	MeshRoot->SetRelativeScale3D(FVector(0.01f, -0.01f, 0.01f));
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(MeshRoot);
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(true);

	// Имя питомца над ним (эталон: тот же JUI-шаблон 1661, что у имён игроков; обновление раз в 4 с —
	//   у нас имя статично, поэтому просто ставим при инициализации).
	Nameplate = CreateDefaultSubobject<UAsdaNameplateComponent>(TEXT("Nameplate"));
	Nameplate->SetupAttachment(Root);
	Nameplate->Kind = EAsdaNameplateKind::Npc;   // только имя, без HP/клана (Kind = ВЫБОР ШАБЛОНА)
	Nameplate->bShowHp = false;
	// Высота — фиксированная от origin пета, как в оригинале (см. InitPet): 77 обычный / 88 парящий.
	Nameplate->bUseFixedWorldHeight = true;
	Nameplate->WorldHeight = 77.f;
	// Цвет ставим ЯВНО: Kind=Npc иначе подставит эталонный NPC-цвет 0xFFDD8E, а у пета свой (см. выше).
	Nameplate->NameColor = FLinearColor(kPetNameColorSRGB);
}

void AAsdaPetActor::InitPet(int32 InPetTypeId, const FString& InName, AActor* InOwner)
{
	PetTypeId = InPetTypeId;
	PetName = InName;
	OwnerChar = InOwner;

	// Модель/анимации/парение из таблицы (импортёр кладёт DT_PetModels рядом с прочими данными).
	// ★ Аудит №4 (B2): свой ленивый кэш заменён общей точкой `AsdaDt::Cached`.
	//   ⚠ Обход ЛИНЕЙНЫЙ и остаётся здесь: ключ строки таблицы — не PetTypeId, а порядковый номер,
	//   поэтому поиск идёт по полю записи (`FindRow` тут неприменим).
	// ★ СБРОС ДО ПОИСКА (найдено при разборе дефекта владельца №2, 25.09.2026). Актор превью
	//   ПЕРЕИСПОЛЬЗУЕТСЯ между породами (`AAsdaPetPreviewActor::SetPet` создаёт его один раз),
	//   и без этой строки порода, которой нет в `DT_PetModels`, молча оставляла бы имя модели от
	//   ПРЕДЫДУЩЕЙ: предупреждение «нет строки» не печаталось бы, а игрок видел бы чужого питомца.
	//   Поля парения сбрасываем тем же основанием — они из той же строки.
	ModelName.Reset();
	bHover = false;
	HoverHeight = 0.f;
	if (const UDataTable* T = AsdaDt::Cached(kPetModelsPath))
	{
		for (const TPair<FName, uint8*>& It : T->GetRowMap())
		{
			const FAsdaPetModelRow* R = reinterpret_cast<const FAsdaPetModelRow*>(It.Value);
			if (!R || R->PetTypeId != PetTypeId) { continue; }
			ModelName = R->ModelName;
			bHover = R->bHover;
			HoverHeight = R->HoverHeight;
			break;
		}
	}
	// ★★★ ПОПРАВКА РЕВИЗИИ 25.09.2026 (СРЕДНЕЕ-5): МЕШ СНИМАЕТСЯ И В ОБЕИХ ВЕТКАХ ОТКАЗА.
	//   БОЛЕЗНЬ. `ModelName.Reset()` выше я добавил, а `Mesh->SetSkeletalMesh` звался ТОЛЬКО на
	//   успешной ветке — значит при отсутствии строки в `DT_PetModels` или непроимпортированной
	//   модели в компоненте ОСТАВАЛСЯ МЕШ ПРЕДЫДУЩЕЙ ПОРОДЫ. Ровно то, что комментарий к сбросу
	//   обещал предотвратить: актор превью переиспользуется между породами, и игрок увидел бы
	//   чужого питомца. Хуже: автоподбор камеры честно «довёл бы кадр» по ЧУЖОЙ модели, и ни в
	//   логе, ни на экране это не выглядело бы ошибкой.
	//   ⇒ пустая порода обязана давать ПУСТОЙ слот, а не наследство.
	if (ModelName.IsEmpty())
	{
		Mesh->SetSkeletalMesh(nullptr);
		UE_LOG(LogAsdaPet, Warning, TEXT("[пет] тип %d: нет строки в DT_PetModels (таблица/импорт не готовы) — меш снят"), PetTypeId);
	}
	else if (USkeletalMesh* M = UAsdaModelAnimSubsystem::LoadMeshChecked(   // Д2
		UAsdaModelAnimSubsystem::BuildSkelPath(AsdaAssetPaths::kSkelRootPet, ModelName)))   // Д8; ★ волна 2 п. 7
	{
		Mesh->SetSkeletalMesh(M);
	}
	else
	{
		Mesh->SetSkeletalMesh(nullptr);
		UE_LOG(LogAsdaPet, Warning, TEXT("[пет] модель '%s' (тип %d) не импортирована — меш снят"), *ModelName, PetTypeId);
	}

	// ★ ВЫСОТА ПЛАШКИ ПО ЭТАЛОНУ (ui_core.c:99042-99060): к МИРОВОМУ Y пета (поле +213) перед проекцией
	//   прибавляется flt_1461D84 = 88.0, если bHover (поле +209), иначе flt_1461D88 = 77.0.
	//   Значения считаны из живого дампа клиента (asda.dmp). Габариты меша НЕ участвуют — поэтому
	//   у нас фиксированный режим, а не «верх коллизии + 10» (он задирал имя высоко над питомцем).
	//   Позиция актора у парящего пета УЖЕ поднята на HoverHeight — как и worldPos в оригинале.
	Nameplate->bUseFixedWorldHeight = true;
	Nameplate->WorldHeight = bHover ? 88.f : 77.f;
	Nameplate->ApplyHeight();   // спавн динамический: OnRegister отработал ДО чтения таблицы
	Nameplate->DisplayName = PetName;
	Nameplate->RefreshNameplate();
	// *** СМЕНА ПОРОДЫ ОБЯЗАНА ПЕРЕЗАПУСТИТЬ КЛИП (24.09.2026).
	//   `PlayPetClip` выходит сразу, если запрошенный трек уже играет (`CurrentTrack == TrackIdx`)
	//   — это защита от перезапуска каждый кадр в `Tick`. Но при ПОВТОРНОМ `InitPet` на том же
	//   акторе (так работает превью: один актор, меняется порода) `CurrentTrack` оставался равен 0
	//   от прошлой породы, вызов ниже выходил молча, и на НОВЫЙ меш никто не ставил анимацию —
	//   модель замирала в позе привязки. Сбрасываем признак: меш другой, играть надо заново.
	CurrentTrack = -1;
	// *** ПОЧЕМУ ИМЕННО ТРЕК 0 — ЧЕСТНОЕ ОБОСНОВАНИЕ (поправка ревизии, СРЕДН-2).
	//   ! «anim_00 = покой» ЭТАЛОНОМ НЕ ДОКАЗУЕМО В ПРИНЦИПЕ, и прежняя формулировка это скрывала.
	//   Оригинал имён клипов не выводит: он берёт ГОТОВЫЕ строки из `PetTable.BIN` полями @180,
	//   @212, @244 и клеит "%s.jbb" (`world_map.c:9955-9975`, раскладка `:7938-7946`); суффикса
	//   `anim_%02d` в его коде нет вовсе (он есть у NPC, `:9128`). `anim_00` — НАШЕ имя в .glb,
	//   данное по номеру трека импортёром, то есть оба прежних «метода» были портовыми.
	//   ЧТО ЭТАЛОНОМ ПОДТВЕРЖДЕНО: треков ровно ТРИ у всех пород (`world_map.c:9922`), роли по
	//   ИНДЕКСУ одинаковы, и индекс 2 — движение (`Char_SetIdleState(..., 2)` в ветке «движется»).
	//   ⇒ Достоверно именно то, что 0 и 1 — два покоя, а 2 — движение; какое имя у файла, неважно.
	PlayPetClip(0, true);   // стартуем с покоя (индекс, а не имя)
	if (InOwner) { PrevOwnerLoc = InOwner->GetActorLocation(); }
	UE_LOG(LogAsdaPet, Log, TEXT("[пет] тип=%d '%s' модель=%s парит=%d(h=%.0f)"),
		PetTypeId, *PetName, *ModelName, bHover ? 1 : 0, HoverHeight);
}

void AAsdaPetActor::PlayPetClip(int32 TrackIdx, bool bLoop)
{
	if (ModelName.IsEmpty() || CurrentTrack == TrackIdx) { return; }
	const FString Path = UAsdaModelAnimSubsystem::BuildSkelAnimPath(   // Д8
		AsdaAssetPaths::kSkelRootPet, ModelName, TrackIdx);   // ★ волна 2 п. 7
	if (UAnimSequence* Clip = UAsdaModelAnimSubsystem::LoadClipChecked(Path))   // Д2
	{
		Mesh->PlayAnimation(Clip, bLoop);
		CurrentTrack = TrackIdx;
	}
}

void AAsdaPetActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AActor* Own = OwnerChar.Get();
	if (!Own) { Destroy(); return; }   // владелец ушёл — питомец уходит с ним

	// ★ Жёсткое следование (эталон: матрица пета пересчитывается каждый кадр от матрицы владельца).
	//   Направление владельца берём из его меша (у персонажа вращается MeshRoot, а не актор).
	FRotator OwnRot = Own->GetActorRotation();
	if (const USceneComponent* OwnMesh = Own->FindComponentByClass<USkeletalMeshComponent>())
	{
		OwnRot = OwnMesh->GetComponentRotation();
		OwnRot.Pitch = 0.f; OwnRot.Roll = 0.f;   // питомец держится горизонтально
	}
	const FVector OwnLoc = Own->GetActorLocation();
	// ★ 22.10.2026 (плавность, `World/AsdaBlendAnimInstance.h`): позиция — от ТЕЛА владельца (актор + «хвост» сетевой поправки),
	//   как и поворот (от меша): у эталона матрица пета строится от матрицы модели владельца. Без хвоста (ключ 0) тело = актор.
	//   Признак «владелец идёт» ниже — по логической позиции актора, как прежде.
	FVector BodyLoc = OwnLoc;
	if (const AAsdaPlayerCharacter* OwnCh = Cast<AAsdaPlayerCharacter>(Own)) { BodyLoc += OwnCh->GetVisualTail(); }
	FVector Target = BodyLoc + OwnRot.RotateVector(FollowOffset);

	// Наземный — прижимается к рельефу под собой; парящий — от матрицы владельца (Target.Z = тело + 20) + HoverHeight
	//   (эталон и ключ отката — у `CVarPetHoverFromOwner` в начале файла).
	if (bHover && CVarPetHoverFromOwner.GetValueOnGameThread() != 0)
	{
		Target.Z += HoverHeight;
	}
	else
	{
		const float GroundZ = AsdaGround::GroundZ(GetWorld(), Target.X, Target.Y, BodyLoc.Z, this);
		Target.Z = bHover ? (GroundZ + HoverHeight) : GroundZ;
	}
	SetActorLocation(Target);
	// Питомец смотрит туда же, куда владелец (эталон: та же матрица ориентации).
	MeshRoot->SetWorldRotation(FRotator(0.f, OwnRot.Yaw, 0.f));

	// ★ Анимация: владелец движется → трек 2 (бег); стоит → покой, вариант выбирается случайно
	//   (эталон rand()%10 >= 2 → трек 0, иначе трек 1 — то есть 80/20).
	const bool bOwnerMoving = FVector::DistSquared2D(OwnLoc, PrevOwnerLoc) > 1.f;
	PrevOwnerLoc = OwnLoc;
	if (bOwnerMoving)
	{
		PlayPetClip(2, true);
		NextIdlePickS = 0.0;
	}
	else
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (CurrentTrack == 2 || Now >= NextIdlePickS)
		{
			PlayPetClip((FMath::Rand() % 10 >= 2) ? 0 : 1, true);
			NextIdlePickS = Now + 4.0;   // перевыбор варианта покоя (эталон обновляет по концу клипа)
		}
	}

	// Видимость наследуется от владельца (эталон: флаг рендера владельца).
	SetActorHiddenInGame(Own->IsHidden());
}
