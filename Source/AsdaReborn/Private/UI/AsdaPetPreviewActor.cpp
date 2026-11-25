// Copyright (c) 2026 @sitxovski (Discord)
// Превью питомца в собственном render-target 240×170 — см. шапку `UI/AsdaPetPreviewActor.h`.
// Порт `CPetWindow_RenderToTexture@0xDC6910` (`npc_mob_pet.c:5559-5671`) + ЛКМ-вращение (П8-1).
#include "UI/AsdaPetPreviewActor.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "World/AsdaPetActor.h"
#include "World/AsdaNameplateComponent.h"   // ★ 24.09: плашку имени в превью надо гасить (см. `SetPet`)
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"   // ключ отката asda.Pet.PreviewFaceCamera

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPetPreview, Log, All);

// *** КЛЮЧ ОТКАТА РАЗВОРОТА ЛИЦОМ (24.09.2026). Разбор, доказательства из эталона и причина,
//   по которой это ОТСТУПЛЕНИЕ, а не починка регресса, - у поля `FacingYawDeg` в шапке `.h`.
static ASDA_DEV_CVAR(int32, CVarPreviewFaceCamera,
	TEXT("asda.Pet.PreviewFaceCamera"), 1,
	TEXT("1 (по умолчанию) - разворачивать модель в превью 43052/43576 ЛИЦОМ к игроку (90 град, ")
	TEXT("число снято развёрткой, а не выведено) ")
	TEXT("(требование владельца; в эталоне угол 0 = базовая поза меша, а куда она смотрит - ")
	TEXT("из кода не выводится: позицию глаза камеры IDA потеряла в инлайн-asm). ")
	TEXT("0 - как эталон: базовая поза без добавочного разворота."),
	ECVF_Default);

// ★★★ КЛЮЧ ОТКАТА ДЕФЕКТА ВЛАДЕЛЬЦА №2 (25.09.2026) — «при выборе кролика превью пустое».
//   Полный разбор с числами живых прогонов — у `AAsdaPetPreviewActor::TickAutoFit`.
static ASDA_DEV_CVAR(int32, CVarPreviewAutoFit,
	TEXT("asda.Pet.PreviewAutoFit"), 1,
	TEXT("1 (по умолчанию) — после смены породы ДОВОДИТЬ камеру превью ПО ПИКСЕЛЯМ кадра: ")
	TEXT("замерить охват модели в буфере и подвинуть точку взгляда/дистанцию так, чтобы модель ")
	TEXT("стояла по центру и не упиралась в края. Нужно потому, что коробки габаритов у части ")
	TEXT("пород не описывают видимую модель (кролик 14: коробка позы вдвое выше модели). ")
	TEXT("0 — поведение 24.09.2026: только по габаритам ассета; у таких пород кадр пуст."),
	ECVF_Default);

// ★★★ КЛЮЧ ОТКАТА К ДОСЛОВНОЙ ЭТАЛОННОЙ ДИСТАНЦИИ (ревизия 25.09.2026, КРИТ-2).
//   Полный разбор — у места применения в `ApplyCamera`. Коротко: числа 20/10/10 из
//   `npc_mob_pet.c:5628-5633` в наших единицах модель не кадрируют (полукадр 5.4 и 2.7 при
//   высоте модели ~45), потому что единица длины у оригинала другая, а перевод потерян в
//   инлайн-asm. По умолчанию они — начальная оценка; ключ возвращает им последнее слово.
static ASDA_DEV_CVAR(int32, CVarPreviewEtalonDistWins,
	TEXT("asda.Pet.PreviewEtalonDistWins"), 0,
	TEXT("0 (по умолчанию, ОТСТУПЛЕНИЕ) — эталонная дистанция по породе (8/16/31) считается ")
	TEXT("НАЧАЛЬНОЙ ОЦЕНКОЙ: если она не вмещает модель, берётся посчитанная, а точку доводит ")
	TEXT("автоподбор. 1 — ДОСЛОВНО ОРИГИНАЛУ: число эталона не правится ничем; кадр у этих ")
	TEXT("трёх пород при этом заведомо неверен (модель во весь кадр или пусто)."),
	ECVF_Default);

AAsdaPetPreviewActor::AAsdaPetPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	// ДОСТОВЕРНЫЕ частные случаи эталона (`npc_mob_pet.c:5628-5633`). Остальные породы — через
	//   `DefaultDistance`, и это честно помечено как число ПОРТА (шапка `.h`).
	DistanceByPetIndex.Add(8, 20.f);
	DistanceByPetIndex.Add(16, 10.f);
	DistanceByPetIndex.Add(31, 10.f);
}

void AAsdaPetPreviewActor::SetPet(int32 PetTypeId, const FString& PetName)
{
	// ⚠ ПРОВЕРКА ИМЕННО `IsValid`, А НЕ «указатель не пуст». Уничтоженный актор остаётся
	//   ненулевым указателем до ближайшей сборки мусора (десятки секунд), и голое `if (PetActor)`
	//   на нём отвечало бы «модель на месте», когда её в сцене уже нет. Ровно эта ошибка и
	//   маскировала дефект превью: см. разбор у `SetActorTickEnabled` ниже.
	if (PetTypeId == ShownPetTypeId && IsValid(PetActor))
	{
		return;   // та же порода уже показана — эталон модель не пересоздаёт
	}

	UWorld* W = GetWorld();
	if (!W)
	{
		return;
	}
	if (!IsValid(PetActor))
	{
		PetActor = nullptr;   // мог остаться указатель на уничтоженный актор — затираем явно
		FActorSpawnParameters P;
		P.ObjectFlags |= RF_Transient;   // превью — не часть уровня, в сохранение не попадает
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// Ставим в ту же точку, что и сам актор сцены: капчер смотрит только на список ShowOnly,
		//   поэтому место в мире значения не имеет, лишь бы не мешало игроку.
		PetActor = W->SpawnActor<AAsdaPetActor>(AAsdaPetActor::StaticClass(), GetActorTransform(), P);
		if (!PetActor)
		{
			UE_LOG(LogAsdaPetPreview, Error, TEXT("[превью-питомца] не удалось создать актор модели"));
			return;
		}
		PetActor->SetActorEnableCollision(false);

		// ★★★ ПРИЧИНА ДЕФЕКТА ВЛАДЕЛЬЦА «ПРЕВЬЮ ПУСТОЕ» (найден на живой игре 24.09.2026).
		//   `AAsdaPetActor::Tick` ПЕРВОЙ СТРОКОЙ делает
		//       `AActor* Own = OwnerChar.Get(); if (!Own) { Destroy(); return; }`
		//   (`Private/World/AsdaPetActor.cpp:124-125`) — это правило МИРА: питомец уходит вместе
		//   с хозяином. А превью обязано звать `InitPet(..., nullptr)`: хозяина у модели в
		//   render-target нет и быть не может (сцена стоит на Z=200000, за кем ей следовать?).
		//   ⇒ модель жила РОВНО ДО ПЕРВОГО ТИКА и уничтожалась, буфер оставался пустым, кисть
		//   давала полностью прозрачный прямоугольник — и сквозь контрол 43052 игрок видел
		//   карту (на снимке владельца сквозь него был виден ЗАДНИК окна).
		//   Волна 9 этого не заметила: в логе есть и «модель создана», и «кисть подставлена»,
		//   а кадр никто не посмотрел (закон `false-negatives-method`: лог о создании НЕ
		//   доказывает, что объект дожил до рендера).
		//   ЛЕЧИМ ЗДЕСЬ, А НЕ В `AAsdaPetActor`: правило «нет хозяина — уходи» для мира верное,
		//   ломать его ради превью нельзя. Тик превью-модели не нужен целиком: следовать не за
		//   кем, а клип крутит сам `USkeletalMeshComponent` в режиме `AnimationSingleNode`,
		//   заведённом в конструкторе актора (`AsdaPetActor.cpp:38`), — он от `Tick` актора
		//   не зависит.
		PetActor->SetActorTickEnabled(false);
	}

	// Модель и клипы берёт штатный `InitPet` из `DT_PetModels` — своего загрузчика НЕ заводим
	//   (закон `check-existing-before-creating`). Владельца нет: превью ни за кем не следует.
	PetActor->InitPet(PetTypeId, PetName, nullptr);
	PetActor->SetActorTransform(GetActorTransform());

	// ★★★ ДЕФЕКТ ВЛАДЕЛЬЦА №2 (25.09.2026), ЧАСТЬ 1 ИЗ 2: ПОЗУ НАДО ПОСЧИТАТЬ ДО КАМЕРЫ.
	//   `ApplyCamera` ниже кадрирует по модели, а к этому моменту кости ещё стоят в ПОЗЕ
	//   ПРИВЯЗКИ: клип назначен (`InitPet` → `PlayPetClip(0)`), но ни одного тика компонента
	//   ещё не было. Считаем позу принудительно, тем же путём, каким её посчитал бы движок.
	//   Разбор самого дефекта — у `ApplyCamera`, ключ отката — `asda.Pet.PreviewAutoFit`.
	if (USkeletalMeshComponent* Msh = PetActor->FindComponentByClass<USkeletalMeshComponent>())
	{
		Msh->TickAnimation(0.f, false);
		Msh->RefreshBoneTransforms();
		// ★ БЕЗ ЭТОЙ СТРОКИ ПОЗА «НЕ ДОЕЗЖАЕТ». `RefreshBoneTransforms` пишет кости в
		//   РЕДАКТИРУЕМЫЙ буфер, а `CalcBounds` читает ОПУБЛИКОВАННЫЙ; пока буферы не
		//   переставлены, габариты считаются по смеси старой и новой позы. Прогон 25.09 это
		//   и показал: у кролика центр вышел 78.7 при позе привязки 142.9 и видимом центре ≈58,
		//   то есть ровно посередине — «полудоехавшая» поза.
		Msh->FinalizeBoneTransform();
		Msh->UpdateBounds();
	}
	// !! `SetActorTransform` ставит поворот актора СЦЕНЫ, то есть стирает и базовый разворот, и
	//   накопленный угол. Возвращаем их тут же, а не ждём ближайшего `TickRotation`: иначе первый
	//   же захваченный кадр (и выгрузка буфера диагностикой) показал бы питомца спиной.
	PetActor->SetActorRotation(FRotator(0.f, BaseYawDeg() + FMath::RadiansToDegrees(YawRad), 0.f));

	// Плашка имени над моделью в превью не нужна: у эталона в буфере 240×170 её нет
	//   (`CPetWindow_RenderToTexture` рисует одну модель, `npc_mob_pet.c:5657`), а имя питомца
	//   окно 43000 печатает своим контролом 43081. В кадр капчера она и так не попадает (в
	//   списке `ShowOnly` только меш), но в МИРЕ осталась бы висеть на Z=200000.
	//   ⚠ ГАСИМ ПОСЛЕ `InitPet`: он сам зовёт `RefreshNameplate`, и сделанное раньше он бы отменил.
	if (UAsdaNameplateComponent* NP = PetActor->FindComponentByClass<UAsdaNameplateComponent>())
	{
		NP->SetVisibility(false, true);
	}

	// В кадр попадает РОВНО меш питомца — капчер работает по списку `ShowOnly`.
	if (USceneCaptureComponent2D* C = Capture.Get())
	{
		C->ShowOnlyComponents.Reset();
		if (USkeletalMeshComponent* M = PetActor->FindComponentByClass<USkeletalMeshComponent>())
		{
			C->ShowOnlyComponents.Add(M);
		}
	}

	// ★★★ СМЕНА ПОРОДЫ СБРАСЫВАЕТ УГОЛ — `world_map.c:9433`. Без этой строки новый питомец
	//   появился бы уже повёрнутым на угол предыдущего.
	const bool bChanged = (ShownPetTypeId != PetTypeId);
	ShownPetTypeId = PetTypeId;
	if (bChanged)
	{
		ResetYaw();
	}
	ApplyCamera();
	EnsureRenderTargetSized(RenderSize, RenderSize);
	UE_LOG(LogAsdaPetPreview, Log,
		TEXT("[превью-питомца] порода %d «%s», буфер %dx%d, угол сброшен=%s"),
		PetTypeId, *PetName, RenderSize.X, RenderSize.Y, bChanged ? TEXT("да") : TEXT("нет"));
}

float AAsdaPetPreviewActor::BaseYawDeg() const
{
	// Разбор - у объявления `FacingYawDeg` в `.h`. Ключ выключен => ведём себя как эталон.
	return (CVarPreviewFaceCamera.GetValueOnGameThread() != 0) ? FacingYawDeg : 0.f;
}

FString AAsdaPetPreviewActor::DescribePetClip() const
{
	// Разбор — у объявления в `.h`: позиция обязана расти между замерами, иначе анимация стоит.
	if (!IsValid(PetActor)) { return FString(); }
	const USkeletalMeshComponent* M = PetActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!M) { return TEXT("меша нет"); }
	// ⚠ Имя клипа берём у ОДНОНОДОВОГО ИНСТАНСА: `PlayAnimation` кладёт ассет именно туда, а
	//   `AnimationData.AnimToPlay` при этом остаётся пустым — первый замер и напечатал «клип=НЕТ»
	//   при исправно идущей анимации.
	const UAnimSingleNodeInstance* SN = M->GetSingleNodeInstance();
	const UAnimationAsset* A = SN ? SN->GetAnimationAsset() : nullptr;
	return FString::Printf(TEXT("клип=%s позиция=%.3f с цикл=%s играет=%s"),
		A ? *A->GetName() : TEXT("НЕТ"),
		M->GetPosition(),
		M->AnimationData.bSavedLooping ? TEXT("да") : TEXT("нет"),
		M->IsPlaying() ? TEXT("да") : TEXT("нет"));
}

bool AAsdaPetPreviewActor::IsPetModelAlive() const
{
	// Разбор — у объявления в `.h`: именно `IsValid`, а не «указатель не пуст».
	return IsValid(PetActor);
}

void AAsdaPetPreviewActor::TickIdleClips(float /*DeltaSeconds*/)
{
	// Разбор эталона и почему трек 2 недостижим — у объявления в `.h`.
	if (!IsValid(PetActor)) { return; }
	USkeletalMeshComponent* M = PetActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!M) { return; }
	const UAnimSingleNodeInstance* SN = M->GetSingleNodeInstance();
	const UAnimationAsset* A = SN ? SN->GetAnimationAsset() : nullptr;
	const UAnimSequenceBase* Seq = Cast<UAnimSequenceBase>(A);
	if (!Seq) { return; }
	const float Len = Seq->GetPlayLength();
	if (Len <= KINDA_SMALL_NUMBER) { return; }

	// «Клип доиграл» у эталона — отдельная проверка перед переключением (`entity_char.c:62024`).
	//   У нас клип зациклен, поэтому конец ловим по ОТКАТУ позиции назад: она растёт до длины и
	//   прыгает в начало. Это надёжнее сравнения с `Len`: попасть кадром ровно в конец нельзя.
	const float Pos = M->GetPosition();
	const bool bWrapped = (Pos + KINDA_SMALL_NUMBER) < LastClipPos;
	LastClipPos = Pos;
	if (!bWrapped) { return; }

	// Строго 0 → 1 → 0, без случайности (`entity_char.c:62027-62034`).
	const int32 Cur = PetActor->GetCurrentTrack();
	const int32 Next = (Cur == 0) ? 1 : ((Cur == 1) ? 0 : Cur);
	if (Next != Cur)
	{
		PetActor->PlayPetClip(Next, true);
		LastClipPos = 0.f;
	}
}

void AAsdaPetPreviewActor::ClearPet()
{
	// Разбор — у объявления: эталон снимает модель превью, когда показывать некого.
	if (IsValid(PetActor)) { PetActor->Destroy(); }
	PetActor = nullptr;
	ShownPetTypeId = -1;
}

void AAsdaPetPreviewActor::ResetYaw()
{
	// Эталон: `g_fDragRotAccum = 0.0` (`item_enhance.c:3823` при открытии окна, `world_map.c:9433`
	//   при смене модели). Признак «первого кадра» тоже взводим — иначе первый же захват после
	//   сброса дал бы рывок от старого `LastX`.
	YawRad = 0.f;
	bRotating = false;
	bSkipFirst = true;
	if (PetActor)
	{
		// *** СБРОС ВОЗВРАЩАЕТ В ПОЛОЖЕНИЕ «ЛИЦОМ», А НЕ В ГОЛОЙ НОЛЬ. Накопитель угла эталона
		//   обнуляется (это он и есть - `YawRad`), а базовый разворот порта НЕ накопитель и
		//   обнуляться не должен: иначе открытие окна и смена породы разворачивали бы питомца
		//   обратно спиной. Разбор - у `FacingYawDeg` в `.h`.
		PetActor->SetActorRotation(FRotator(0.f, BaseYawDeg(), 0.f));
	}
}

void AAsdaPetPreviewActor::TickRotation(bool bLmbHeld, bool bPressOnPreview, float CursorX)
{
	// ── 1. Снятие захвата. Эталон опрашивает `GetAsyncKeyState(1)` прямо в рендере
	//      (`entity_char.c:61892-61893`); ОТДЕЛЬНОГО обработчика отпускания у него НЕТ.
	if (!bLmbHeld)
	{
		bRotating = false;
	}
	// ── 2. Взвод: нажатие ЛКМ над контролом превью 43052 (`item_enhance.c:4011-4016`, событие 0xB).
	if (bPressOnPreview && bLmbHeld)
	{
		bRotating = true;
	}

	// ── 3. Накопление угла.
	if (bRotating)
	{
		if (!bSkipFirst)
		{
			// ★ ТОЧНАЯ формула эталона (`entity_char.c:61901`):
			//     g_fDragRotAccum += (prevX − curX) * 0.02 * (−1.0)  ⇔  += (curX − prevX) * 0.02
			//   То есть 0.02 РАДИАНА на пиксель, вправо → угол растёт.
			//   Ни клампа, ни нормализации по 2π у эталона нет — не добавляем.
			YawRad += (CursorX - LastX) * kRadPerPixel;
		}
		LastX = CursorX;
		// ⚠ `bSkipFirst` — не оптимизация: он единственное, что спасает от рывка на мусорном
		//   «прошлом X» в первом кадре захвата (`entity_char.c:61900-61906`).
		bSkipFirst = false;
	}
	else
	{
		bSkipFirst = true;
	}

	// ── 4. Применение к модели. Вертикали у эталона нет — крутим только вокруг вертикальной оси.
	if (PetActor)
	{
		// Базовый разворот ПРИБАВЛЯЕТСЯ к накопителю, а не подменяет его: множитель эталона
		//   0.02 рад/пиксель остаётся ровно таким, каким был.
		PetActor->SetActorRotation(FRotator(0.f, BaseYawDeg() + FMath::RadiansToDegrees(YawRad), 0.f));
	}
}

void AAsdaPetPreviewActor::ApplyCamera()
{
	USceneCaptureComponent2D* C = Capture.Get();
	if (!C)
	{
		return;
	}
	// ★ FOV капчера — ГОРИЗОНТАЛЬНЫЙ, а эталон задаёт ВЕРТИКАЛЬНЫЙ (30°) плюс аспект
	//   КОНСТАНТОЙ 1.4117647 (`npc_mob_pet.c:5645`, `:5647`). Переводим:
	//     tan(FovX/2) = tan(FovY/2) * Aspect
	const float FovYRad = FMath::DegreesToRadians(FovYDeg);
	const float FovXRad = 2.f * FMath::Atan(FMath::Tan(FovYRad * 0.5f) * Aspect);

	// ═══ КАДРИРОВАНИЕ. Дистанция и высота взгляда — по ГАБАРИТАМ МОДЕЛИ, а не по константам ═════
	//
	// ПОЧЕМУ ТАК, А НЕ «ЧИСЛОМ ЭТАЛОНА». Построение позиции глаза у эталона IDA ПОТЕРЯЛА: там
	//   инлайн-asm (`npc_mob_pet.c:5620-5626`, `:5635`), восстановить нечего — это честно записано
	//   в шапке `.h` с самого начала. Значит выбирать приходится порту, и выбор «одно число на все
	//   породы» ПЛОХОЙ: живой прогон 24.09 показал, что при `DefaultDistance = 120` и
	//   `EyeHeight = 45` модель породы 15 занимала в буфере 240×170 прямоугольник (70,93)-(160,169),
	//   то есть сидела в нижней половине И БЫЛА ОБРЕЗАНА НИЖНИМ КРАЕМ, а верхняя половина пустовала.
	//   У пород разного размера одно число не может дать верный кадр по определению — и именно
	//   поэтому у эталона дистанция и была ПО ПОРОДЕ (`sub_93A9DA(окно, tableIndex)`).
	// ! ПОПРАВКА РЕВИЗИИ (МЕЛКОЕ): ниже было написано, что подбор заменяет ТОЛЬКО `DefaultDistance`.
	//   Это неточно: ВЫСОТА ВЗГЛЯДА `LookZ` берётся из центра габаритов ВСЕГДА, в том числе когда
	//   дистанция пришла из эталонной таблицы. Так и задумано (без этого модель уезжает из кадра),
	//   но сказать надо прямо. Эталонной остаётся ДИСТАНЦИЯ, и только она.
	// ★ ЭТАЛОННЫЕ ЧАСТНЫЕ СЛУЧАИ ОСТАЮТСЯ ГЛАВНЕЕ: если для породы задано число эталона
	//   (8 → 20, 16 → 10, 31 → 10, `npc_mob_pet.c:5628-5633`), берётся ОНО, и подбор не работает.
	//   Автоподбор заменяет только `DefaultDistance` — то самое «число порта», а не данные оригинала.
	const FVector Base = GetActorLocation();
	float Dist = DefaultDistance;
	float LookZ = EyeHeight;
	const float* EtalonDist = DistanceByPetIndex.Find(ShownPetTypeId);

	USkeletalMeshComponent* M = PetActor ? PetActor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	// *** ГАБАРИТЫ БЕРЁМ У АССЕТА, А НЕ У ТЕКУЩЕЙ ПОЗЫ (24.09.2026, после включения анимации).
	//   `M->Bounds` — это прямоугольник КАДРА: он дышит вместе с клипом покоя, и камера, посчитанная
	//   по случайному кадру, у одних пород обрезала лапы, у других оставляла лишний воздух.
	//   Габариты ассета (`USkeletalMesh::GetBounds()`, поза привязки плюс запас импорта) от клипа
	//   не зависят, поэтому кадр стабилен и одинаков для всех поз. Масштаб компонента учитываем
	//   сами: у питомцев он (0.01, -0.01, 0.01) на `MeshRoot`.
	// ★★★ ДЕФЕКТ ВЛАДЕЛЬЦА №2 (25.09.2026): «ПРИ ВЫБОРЕ КРОЛИКА ПРЕВЬЮ ПУСТОЕ».
	//
	// ПРИЧИНА, ДОКАЗАННАЯ ЖИВЫМ ЛОГОМ САМОГО ВЛАДЕЛЬЦА (`Saved/Logs/AsdaReborn.log`):
	//     :5126  порода 14 «Кролик»  → дистанция 128.2, взгляд на высоте **142.9**, габариты Z=25.1
	//     :6068  порода 20 «Доги»    → дистанция 105.7, взгляд на высоте   46.7,  габариты Z=21.2
	//   Полукадр по вертикали = Dist · tg(FovY/2) = 128.2 · tg 15° = **34.4**. Видимый кролик
	//   стоит около Z≈47 (замерено пикселями, см. ниже), то есть почти на ТРИ полукадра НИЖЕ
	//   точки взгляда. В кадре НОЛЬ пикселей модели. У собаки центр ассета совпадает с видимым
	//   (46.7 против 46.2), поэтому она видна — и стендовые породы волны 9 тоже были «спокойными».
	//
	// ★ ПОЧЕМУ ОДНИМИ ГАБАРИТАМИ ЭТО НЕ ЧИНИТСЯ. У скелетного меша без физ-ассета `CalcBounds`
	//   гоняет коробку АССЕТА трансформом КОРНЕВОЙ кости, а не обтягивает реальную позу; кости же
	//   у парящих питомцев начинаются на земле. Живые замеры (`Reports/pet/win2/game_win2*.log`):
	//     порода 20: ассет 46.7±21.2 · коробка позы 24.6..69.2 · кости 0.0..58.8 · ВИДИМО 24.7..67.7
	//     порода 14: ассет 142.9±25.1 · коробка позы 21.9..135.5 · кости 0.0..62.8 · ВИДИМО ≈23.7..69.8
	//   Ни одна из трёх коробок не годится для ОБЕИХ пород сразу.
	//
	// ЧТО СДЕЛАНО. Габариты АССЕТА остаются ПЕРВЫМ ПРИБЛИЖЕНИЕМ (для собаки оно верное), а точную
	//   высоту и масштаб доводит `TickAutoFit` — ПО ПИКСЕЛЯМ уже отрисованного кадра. Коробка
	//   КОСТЕЙ запоминается как «точка заведомо внутри модели»: с неё начинается поиск, когда в
	//   кадре пусто и направлять подбор нечем.
	// КЛЮЧ ОТКАТА: `asda.Pet.PreviewAutoFit 0` — только габариты ассета, как 24.09 (кролик не виден).
	FBoxSphereBounds Bn(ForceInit);     // габариты АССЕТА (размер) — локальные, от опоры
	FVector PoseLocal(0.f, 0.f, 0.f);   // центр РЕАЛЬНОЙ позы относительно опоры актора
	double PoseExtentZ = 0.0;           // полувысота коробки реальной позы (для диагностики)
	bool bHavePose = false;
	if (M)
	{
		// `M->Bounds` — МИРОВОЙ прямоугольник текущей позы. Приводим к локальным координатам
		//   опоры: сцена превью стоит на Z = 200000, и без вычитания `Base` высота взгляда
		//   стала бы этим самым числом.
		// ★★★ ЦЕНТР СЧИТАЕМ ПО КОСТЯМ, А НЕ ПО `Bounds` (поправка живого прогона 25.09.2026).
		//   ПОЧЕМУ `Bounds` НЕ ГОДЯТСЯ ИМЕННО ДЛЯ ЦЕНТРА. У скелетного меша без физ-ассета
		//   `CalcBounds` берёт коробку АССЕТА и гоняет её трансформом КОРНЕВОЙ кости, а не
		//   обтягивает реальную позу. Прогон это и показал числами:
		//     собака (порода 20): коробка позы Z 24.6..69.2 (полувысота 22.3) ≈ габариты ассета 21.2 — верно;
		//     кролик (порода 14): коробка позы Z 21.9..135.5 (полувысота **56.8**) при тех же
		//       габаритах ассета 25.1 — то есть коробка ВДВОЕ выше модели, и её центр 78.7
		//       стоит заметно выше самого кролика (в кадре он лежал у нижнего края, охват
		//       (102,107)-(171,169) при центре кадра 85).
		//   Кости же лежат ВНУТРИ модели всегда, поэтому их коробка — честный центр. РАЗМЕР
		//   по-прежнему берём у ассета: на собаке он совпал с кадром до пикселя (полная высота
		//   коробки 42.3 против 43.3 замеренных), и трогать то, что работает, незачем.
		const TArray<FTransform>& CS = M->GetComponentSpaceTransforms();
		const FTransform CompXf = M->GetComponentTransform();
		bool bAnyBone = false;
		FVector BoneMin(0, 0, 0), BoneMax(0, 0, 0);
		for (const FTransform& T : CS)
		{
			const FVector P = CompXf.TransformPosition(T.GetLocation()) - Base;
			if (!bAnyBone) { BoneMin = BoneMax = P; bAnyBone = true; continue; }
			BoneMin = FVector(FMath::Min(BoneMin.X, P.X), FMath::Min(BoneMin.Y, P.Y), FMath::Min(BoneMin.Z, P.Z));
			BoneMax = FVector(FMath::Max(BoneMax.X, P.X), FMath::Max(BoneMax.Y, P.Y), FMath::Max(BoneMax.Z, P.Z));
		}
		if (bAnyBone)
		{
			PoseLocal = (BoneMin + BoneMax) * 0.5;
			PoseExtentZ = (BoneMax.Z - BoneMin.Z) * 0.5;
			bHavePose = true;
		}
		else
		{
			// Костей нет вовсе — падаем на прежний источник, он лучше, чем ничего.
			PoseLocal = M->Bounds.Origin - Base;
			PoseExtentZ = M->Bounds.BoxExtent.Z;
			bHavePose = (M->Bounds.BoxExtent.Z > 1.f);
		}
		if (const USkeletalMesh* Asset = M->GetSkeletalMeshAsset())
		{
			const FVector Sc = M->GetComponentTransform().GetScale3D().GetAbs();
			const FBoxSphereBounds Local = Asset->GetBounds();
			Bn = FBoxSphereBounds(Local.Origin * Sc, Local.BoxExtent * Sc, Local.SphereRadius * Sc.GetMax());
		}
		else
		{
			// Меш ещё не подставлен — падаем на прямоугольник кадра. ★ ПРИВЕДЁН К ЛОКАЛЬНЫМ:
			//   раньше сюда попадали МИРОВЫЕ координаты и `LookZ` становился 200000.
			Bn = FBoxSphereBounds(PoseLocal, M->Bounds.BoxExtent, M->Bounds.SphereRadius);
		}
	}
	const bool bHaveBounds = (M != nullptr) && (Bn.BoxExtent.Z > 1.f);
	// ★★★ ПЕРВОЕ ПРИБЛИЖЕНИЕ — по габаритам АССЕТА, как и было. Точную высоту взгляда доводит
	//   `TickAutoFit` ПО ПИКСЕЛЯМ КАДРА: ни одна коробка габаритов у части пород не описывает
	//   видимую модель (числа и разбор — у `TickAutoFit`).
	//   Коробку КОСТЕЙ запоминаем как «точку, заведомо внутри модели» — с неё начинается поиск,
	//   когда в кадре пусто и направлять подбор нечем.
	SearchLookZ = bHavePose ? (float)PoseLocal.Z : EyeHeight;
	SearchHalfZ = (float)FMath::Max(PoseExtentZ, 1.0);
	if (bHaveBounds)
	{
		// Смотрим в ЦЕНТР модели по высоте — тогда она не уползает ни вниз, ни вверх.
		LookZ = (float)Bn.Origin.Z;
		// ★ Считаем подбор ВСЕГДА (даже когда есть эталонное число): он нужен как запасной
		//   вариант в разборе эталонной дистанции ниже. Прежнее `if (!EtalonDist)` оставляло
		//   `Dist` равным `DefaultDistance`, и подстраховки не получалось.
		{
			// Дистанция, при которой модель влезает и по вертикали, и по горизонтали, с запасом
			//   `FitMargin`. Полувысота делится на tg(FovY/2), полуширина — на tg(FovX/2);
			//   берём большее и добавляем полуглубину, чтобы ближняя часть меша не вылезла за
			//   ближнюю плоскость отсечения.
			const float HalfH = Bn.BoxExtent.Z * FitMargin;
			const float HalfW = FMath::Max(Bn.BoxExtent.X, Bn.BoxExtent.Y) * FitMargin;
			const float NeedV = HalfH / FMath::Max(FMath::Tan(FovYRad * 0.5f), KINDA_SMALL_NUMBER);
			const float NeedH = HalfW / FMath::Max(FMath::Tan(FovXRad * 0.5f), KINDA_SMALL_NUMBER);
			Dist = FMath::Max(NeedV, NeedH) + Bn.BoxExtent.X;
			Dist = FMath::Max(Dist, NearClip + 1.f);   // ближе ближней плоскости камеру не ставим
		}
	}
	// ★ КЛАМП ПО БЛИЖНЕЙ ПЛОСКОСТИ ОТНОСИТСЯ И К ЧИСЛУ ЭТАЛОНА (найдено при разборе дефекта №2).
	//   Раньше `Dist = FMath::Max(Dist, NearClip + 1.f)` стоял ВНУТРИ ветки автоподбора, а строка
	//   ниже перетирала результат уже без клампа. У пород **16** и **31** эталонная дистанция
	//   равна 10 (`npc_mob_pet.c:5628-5633`), а `NearClip` сцены превью — ровно 10
	//   (`AsdaPreviewSceneActor.h:85`) ⇒ модель целиком отсекалась ближней плоскостью, и превью
	//   у этих двух пород было пустым ПО ДРУГОЙ причине. Это не отступление от оригинала: число
	//   эталона сохраняется, просто оно не может быть ближе, чем наш рендер вообще способен видеть.
	// ★★★ ПОПРАВКА РЕВИЗИИ 25.09.2026 (КРИТ-2 и СРЕДНЕЕ-3): ЧТО ДЕЛАТЬ С ЭТАЛОННОЙ ДИСТАНЦИЕЙ.
	//
	//   ФАКТ, КОТОРЫЙ ПРИШЛОСЬ ПРИЗНАТЬ. Числа эталона (порода 8 → 20, 16 → 10, 31 → 10,
	//   `npc_mob_pet.c:5628-5633`) В НАШИХ ЕДИНИЦАХ МОДЕЛЬ НЕ КАДРИРУЮТ: при 20 полукадр равен
	//   20·tg15° = 5.4, при 10 — 2.7, а питомцы у нас высотой ~45-50 единиц. То есть буквальное
	//   число даёт кадр в ВОСЕМЬ-ДВАДЦАТЬ раз меньше модели. Это не наша ошибка подбора: у
	//   оригинала другая единица длины (меш у нас сидит на масштабе 0.01 на `MeshRoot`), а
	//   перевести её нечем — построение позиции глаза IDA потеряла в инлайн-asm
	//   (`npc_mob_pet.c:5620-5626`), о чём честно сказано в шапке `.h` с самого начала.
	//
	//   ⇒ РЕШЕНИЕ, И ОНО ОТСТУПЛЕНИЕ (закон `fix-original-defects`, помечаю): по умолчанию
	//   эталонное число — ЛИШЬ НАЧАЛЬНАЯ ОЦЕНКА, а последнее слово за подбором. Иначе породы
	//   8/16/31 показывали бы обрезок модели во весь кадр, а 16 и 31 — вообще пустой кадр
	//   (при 10 и `NearClip` = 10 модель целиком за ближней плоскостью).
	//   КЛЮЧ ОТКАТА: `asda.Pet.PreviewEtalonDistWins 1` — дословно оригиналу, число не правится
	//   ничем; кадр при этом заведомо неверен, и это ожидаемо.
	//
	//   ★ ВТОРОЙ РУБЕЖ (СРЕДНЕЕ-3): ключ `asda.Pet.PreviewAutoFit 0` больше НЕ ВЕДЁТ В ПУСТОЙ
	//   КАДР. Раньше при выключенном подборе у пород 16/31 оставалось `Max(10, NearClip+1) = 11`
	//   и кадр был пуст (0.00 %). Теперь эталонное число берётся, только если оно ФИЗИЧЕСКИ
	//   способно вместить коробку ассета; не способно — берём посчитанную дистанцию и говорим
	//   об этом в лог.
	if (EtalonDist)
	{
		const float EtalonClamped = FMath::Max(*EtalonDist, NearClip + 1.f);
		if (CVarPreviewEtalonDistWins.GetValueOnGameThread() != 0)
		{
			Dist = EtalonClamped;   // дословно оригиналу, со всеми последствиями
		}
		else
		{
			// Влезает ли модель в кадр на эталонной дистанции? Полукадр против полувысоты коробки.
			const float EtalonHalfFrame = EtalonClamped * FMath::Tan(FovYRad * 0.5f);
			const float NeedHalf = bHaveBounds ? (float)Bn.BoxExtent.Z : 0.f;
			if (NeedHalf > 0.f && EtalonHalfFrame < NeedHalf)
			{
				UE_LOG(LogAsdaPetPreview, Log,
					TEXT("[превью-питомца] порода %d: эталонная дистанция %.1f даёт полукадр %.1f ")
					TEXT("при полувысоте модели %.1f — модель не вмещается, беру посчитанную %.1f ")
					TEXT("(откат к дословному числу: asda.Pet.PreviewEtalonDistWins 1)"),
					ShownPetTypeId, EtalonClamped, EtalonHalfFrame, NeedHalf, Dist);
				// `Dist` уже посчитан ветвью автоподбора выше — оставляем его.
			}
			else
			{
				Dist = EtalonClamped;
			}
		}
	}

	// ★★★ ВТОРАЯ ПОЛОВИНА СРЕДНЕГО-3 (найдена прогоном 25.09.2026 УЖЕ ПОСЛЕ первой правки).
	//   Починив дистанцию, я проверил откат `asda.Pet.PreviewAutoFit 0` — и порода 16 ВСЁ РАВНО
	//   дала пустой кадр (`game_revfix.log:4218`, 0.00 %). Дистанция к тому моменту была уже
	//   правильная (110.5), а мимо шла ВЫСОТА ВЗГЛЯДА: с выключенным подбором она берётся из
	//   центра коробки АССЕТА, а у этой породы клип покоя уводит корень на ~134 единицы.
	//   ⇒ ключ отката по-прежнему вёл в пустой кадр, просто по другой причине. Закрываю и её.
	//
	//   ПРОВЕРКА ДЕТЕРМИНИРОВАННАЯ, БЕЗ ПИКСЕЛЕЙ (значит работает и при выключенном подборе):
	//   коробка КОСТЕЙ заведомо внутри модели. Если она целиком ВНЕ будущего кадра — модель в
	//   кадр не попадёт гарантированно, и тогда честнее смотреть в центр костей: кадр выйдет
	//   приблизительным, но НЕ ПУСТЫМ. Пересекаются — ничего не трогаем (у «спокойных» пород
	//   центр ассета точнее, у собаки он совпадает с видимым до полупикселя).
	if (bHavePose)
	{
		const float HalfFrameNow = Dist * FMath::Tan(FovYRad * 0.5f);
		const double FrameLo = (double)LookZ - HalfFrameNow;
		const double FrameHi = (double)LookZ + HalfFrameNow;
		const double BoneLo = PoseLocal.Z - PoseExtentZ;
		const double BoneHi = PoseLocal.Z + PoseExtentZ;
		if (BoneHi < FrameLo || BoneLo > FrameHi)
		{
			UE_LOG(LogAsdaPetPreview, Log,
				TEXT("[превью-питомца] порода %d: кадр [%.1f…%.1f] и коробка костей [%.1f…%.1f] ")
				TEXT("НЕ ПЕРЕСЕКАЮТСЯ — модель заведомо мимо, беру центр костей %.1f вместо %.1f"),
				ShownPetTypeId, FrameLo, FrameHi, BoneLo, BoneHi, PoseLocal.Z, LookZ);
			LookZ = (float)PoseLocal.Z;
		}
	}

	CamLookZ = LookZ;
	CamDist  = Dist;
	// Точка отката автоподбора: оценка по габаритам. Сюда он возвращается, если не сошёлся.
	FallbackLookZ = LookZ;
	FallbackDist  = Dist;
	PlaceCapture();
	// Взводим автоподбор по пикселям: разбор — у `TickAutoFit`.
	AutoFitTries = (CVarPreviewAutoFit.GetValueOnGameThread() != 0) ? kAutoFitMaxTries : 0;
	AutoFitWait  = kAutoFitWaitFrames;
	AutoFitStep  = 0;
	// ★ ПЕЧАТАЕМ ОБА ЦЕНТРА И ПОЛУКАДР. Прежняя строка давала только одну высоту, и по ней
	//   дефект кролика прочитать было НЕЧЕМ: «взгляд на высоте 142.9» выглядит как обычное число.
	//   Теперь рядом стоят центр позы привязки, центр реальной позы и полувысота кадра на
	//   плоскости цели — расхождение видно сразу (закон `false-negatives-method`).
	const float HalfFrame = Dist * FMath::Tan(FovYRad * 0.5f);
	UE_LOG(LogAsdaPetPreview, Log,
		TEXT("[превью-питомца] камера: дистанция %.1f (%s), взгляд на высоте %.1f, ")
		TEXT("центр позы привязки Z=%.1f, центр РЕАЛЬНОЙ позы (по костям) Z=%.1f (сдвиг %.1f), ")
		TEXT("коробка КОСТЕЙ Z %.1f..%.1f (полувысота %.1f), ")
		TEXT("полувысота кадра %.1f, габариты модели %s"),
		Dist,
		EtalonDist ? TEXT("число ЭТАЛОНА по породе") : (bHaveBounds ? TEXT("подбор по габаритам") : TEXT("число порта")),
		LookZ,
		Bn.Origin.Z, PoseLocal.Z, PoseLocal.Z - Bn.Origin.Z,
		PoseLocal.Z - PoseExtentZ, PoseLocal.Z + PoseExtentZ, PoseExtentZ,
		HalfFrame, bHaveBounds ? *Bn.BoxExtent.ToString() : TEXT("НЕТ (меш не загружен)"));
}

// ═══════════ ★★★ АВТОПОДБОР КАДРА ПРЕВЬЮ ПО ПИКСЕЛЯМ (ДЕФЕКТ ВЛАДЕЛЬЦА №2, 25.09.2026) ═══════
//
// ЗАЧЕМ ВООБЩЕ. Кадрировать питомца по КОРОБКЕ ГАБАРИТОВ нельзя: ни одна из трёх доступных
//   коробок у части пород не описывает видимую модель. Числа сняты живыми прогонами
//   (`Reports/pet/win2/game_win2{b,c,d}.log`), а не выведены:
//     порода 20 «Доги»   — ассет центр 46.7 полувысота 21.2; коробка позы 24.6..69.2 (пв 22.3);
//                          коробка костей 0.0..58.8 (пв 29.4); ВИДИМАЯ модель 24.7..67.7.
//                          ⇒ верна коробка АССЕТА, коробка КОСТЕЙ уводит вниз на 17.
//     порода 14 «Кролик» — ассет центр 142.9 полувысота 25.1; коробка позы 21.9..135.5 (пв 56.8);
//                          коробка костей 0.0..62.8 (пв 31.4); ВИДИМАЯ модель ≈23.7..69.8.
//                          ⇒ ассет мимо на 96, поза мимо на 32, кости мимо на 15.
//   Причина разнобоя: у скелетного меша без физ-ассета `CalcBounds` гоняет коробку АССЕТА
//   трансформом корневой кости, а корень у клипа покоя кролика уведён; кости же у парящих
//   питомцев начинаются на земле (`bHover`, h=55), то есть НИЖЕ самой модели.
//
// ЧТО ДЕЛАЕМ ВМЕСТО ЭТОГО. Замеряем, ГДЕ МОДЕЛЬ ОКАЗАЛАСЬ В БУФЕРЕ, и правим камеру по
//   замеру — тем же числовым ядром, которым проверка снимает охват
//   (`AAsdaPreviewSceneActor::MeasureFrameCoverage`; второй копии метрики не заводим).
//   Это не «подгонка»: закон `diagnostic-must-use-real-path` требует смотреть на то, что
//   реально дошло до кадра, а закон `never-invent-only-original` запрещает выдумывать
//   константу там, где её можно измерить.
//
// ЦЕНА. Чтение буфера синхронное. Поэтому оно делается ТОЛЬКО при смене породы, не более
//   `kAutoFitMaxTries` раз, и между попытками ждём `kAutoFitWaitFrames` кадров — капчеру нужно
//   успеть отрисовать кадр новой камерой.
// КЛЮЧ ОТКАТА: `asda.Pet.PreviewAutoFit 0`.
void AAsdaPetPreviewActor::TickAutoFit()
{
	if (AutoFitTries <= 0) { return; }
	if (AutoFitWait > 0) { --AutoFitWait; return; }

	FIntRect R; int32 Hits = 0; FIntPoint Sz(0, 0);
	if (!MeasureFrameCoverage(R, Hits, Sz) || Sz.X <= 0 || Sz.Y <= 0)
	{
		// ★ ПОПРАВКА РЕВИЗИИ (КРИТ-1): даже этот выход больше не молчит.
		AutoFitTries = 0;
		UE_LOG(LogAsdaPetPreview, Warning,
			TEXT("[превью-питомца] автоподбор отменён: буфер не прочитался (порода %d). ")
			TEXT("Камера осталась на оценке по габаритам."), ShownPetTypeId);
		return;
	}
	--AutoFitTries;
	++AutoFitStep;
	const float FovYRad = FMath::DegreesToRadians(FovYDeg);
	const float HalfFrame = CamDist * FMath::Tan(FovYRad * 0.5f);   // полукадр в мировых единицах
	const float PxToWorld = (Sz.Y > 0) ? (2.f * HalfFrame / (float)Sz.Y) : 0.f;

	if (Hits == 0)
	{
		// ВЕТКА «В КАДРЕ ПУСТО». Направлять подбор нечем, поэтому прыгаем в заведомо верную
		//   точку — центр коробки КОСТЕЙ (кости всегда внутри модели) — и расширяем кадр так,
		//   чтобы в него гарантированно попала вся костная коробка.
		CamLookZ = SearchLookZ;
		const float NeedHalf = FMath::Max(SearchHalfZ * 1.6f, HalfFrame * 1.6f);
		CamDist = FMath::Max(NeedHalf / FMath::Max(FMath::Tan(FovYRad * 0.5f), KINDA_SMALL_NUMBER),
			NearClip + 1.f);
		PlaceCapture();
		AutoFitWait = kAutoFitWaitFrames;
		UE_LOG(LogAsdaPetPreview, Log,
			TEXT("[превью-питомца] автоподбор %d: в кадре ПУСТО ⇒ взгляд в центр костей %.1f, ")
			TEXT("дистанция %.1f (осталось попыток %d)"),
			AutoFitStep, CamLookZ, CamDist, AutoFitTries);
		return;
	}

	// ВЕТКА «МОДЕЛЬ ВИДНА». Два независимых исправления за проход: центр и масштаб.
	const float CoverCenterY = 0.5f * (float)(R.Min.Y + R.Max.Y);
	const float FrameCenterY = 0.5f * (float)Sz.Y - 0.5f;
	const float DeltaPx = FrameCenterY - CoverCenterY;          // + — модель ниже центра
	const bool bTouchesTop    = (R.Min.Y <= 0);
	const bool bTouchesBottom = (R.Max.Y >= Sz.Y - 1);
	const bool bTouchesSide   = (R.Min.X <= 0) || (R.Max.X >= Sz.X - 1);
	const bool bClipped = bTouchesTop || bTouchesBottom || bTouchesSide;

	// ★★★ ПОПРАВКА РЕВИЗИИ 25.09.2026 (КРИТ-1): ЗДЕСЬ БОЛЬШЕ НЕ ПРАВЯТ ВСЛЕПУЮ.
	//   БОЛЕЗНЬ. Ветка «модель упёрлась в край» умножала дистанцию на 1.35 за проход. У породы 8
	//   эталонная дистанция 20 даёт полукадр 5.4 при модели ~45 — нужно было ×20, а пять проходов
	//   ×1.35 довели лишь до 89.7 при нужных ~110. Попытки кончились, строки «автоподбор завершён»
	//   в логе НЕ ПОЯВИЛОСЬ ВОВСЕ, и владелец получил обрезанную модель во весь кадр молча.
	//   ЛЕЧЕНИЕ. Считаем ЧЕСТНО, тем же способом, что и ветка «в кадре пусто»: нижняя граница
	//   истинной полувысоты модели — это максимум из (а) коробки КОСТЕЙ, которая заведомо внутри
	//   модели, и (б) текущего полукадра, раз модель его уже переросла. Дальше — обычная формула
	//   «полувысота / tg(FovY/2)» с запасом `FitMargin`. Один проход вместо двадцати.
	//   ★ Страховка от застоя: если расчёт не дал роста (костей нет, оценка занижена), всё равно
	//   увеличиваем в 1.6 раза — прогресс обязан быть на КАЖДОМ проходе, иначе попытки сгорают зря.
	if (bClipped)
	{
		CamLookZ += DeltaPx * PxToWorld;
		const float NeedHalf = FMath::Max(SearchHalfZ, HalfFrame) * FitMargin;
		const float Honest = NeedHalf / FMath::Max(FMath::Tan(FovYRad * 0.5f), KINDA_SMALL_NUMBER);
		CamDist = FMath::Max3(Honest, CamDist * 1.6f, NearClip + 1.f);
		PlaceCapture();
		AutoFitWait = kAutoFitWaitFrames;
		UE_LOG(LogAsdaPetPreview, Log,
			TEXT("[превью-питомца] автоподбор %d: охват (%d,%d)-(%d,%d) упёрся в край%s%s%s ⇒ ")
			TEXT("нужна полувысота %.1f (кости %.1f, полукадр %.1f) ⇒ взгляд %.1f, дистанция %.1f ")
			TEXT("(осталось попыток %d)"),
			AutoFitStep, R.Min.X, R.Min.Y, R.Max.X, R.Max.Y,
			bTouchesTop ? TEXT(" сверху") : TEXT(""), bTouchesBottom ? TEXT(" снизу") : TEXT(""),
			bTouchesSide ? TEXT(" сбоку") : TEXT(""), NeedHalf, SearchHalfZ, HalfFrame,
			CamLookZ, CamDist, AutoFitTries);
		if (AutoFitTries <= 0) { ReportAutoFitGaveUp(TEXT("модель так и упирается в край кадра")); }
		return;
	}

	// Картинка целая. Доводим центр и, если модель мелкая, подтягиваем кадр к ней.
	const float CoverH = (float)(R.Max.Y - R.Min.Y + 1);
	const float CoverW = (float)(R.Max.X - R.Min.X + 1);
	const float WantFrac = 1.f / FitMargin;                     // доля буфера под модель
	const float Frac = FMath::Max(CoverH / (float)Sz.Y, CoverW / (float)Sz.X);
	const bool bCentered = FMath::Abs(DeltaPx) <= kAutoFitCenterTolPx;
	const bool bSized    = FMath::Abs(Frac - WantFrac) <= kAutoFitSizeTol;
	if (bCentered && bSized)
	{
		AutoFitTries = 0;
		UE_LOG(LogAsdaPetPreview, Log,
			TEXT("[превью-питомца] автоподбор завершён за %d прохода(ов): охват (%d,%d)-(%d,%d) ")
			TEXT("%dx%d в буфере %dx%d, смещение центра %.1f px, доля %.2f; взгляд %.1f, дистанция %.1f"),
			AutoFitStep, R.Min.X, R.Min.Y, R.Max.X, R.Max.Y, (int32)CoverW, (int32)CoverH,
			Sz.X, Sz.Y, DeltaPx, Frac, CamLookZ, CamDist);
		return;
	}
	if (!bCentered) { CamLookZ += DeltaPx * PxToWorld; }
	if (!bSized && Frac > KINDA_SMALL_NUMBER)
	{
		CamDist = FMath::Max(CamDist * (Frac / WantFrac), NearClip + 1.f);
	}
	PlaceCapture();
	AutoFitWait = kAutoFitWaitFrames;
	UE_LOG(LogAsdaPetPreview, Log,
		TEXT("[превью-питомца] автоподбор %d: охват (%d,%d)-(%d,%d), смещение центра %.1f px, ")
		TEXT("доля %.2f (нужно %.2f) ⇒ взгляд %.1f, дистанция %.1f (осталось попыток %d)"),
		AutoFitStep, R.Min.X, R.Min.Y, R.Max.X, R.Max.Y, DeltaPx, Frac, WantFrac,
		CamLookZ, CamDist, AutoFitTries);
	if (AutoFitTries <= 0) { ReportAutoFitGaveUp(TEXT("центр/масштаб не сошлись за отведённые проходы")); }
}

// ★★★ ЧЕСТНЫЙ ОТКАЗ (ревизия 25.09.2026, КРИТ-1). Прежде исчерпание попыток не оставляло в логе
//   НИ ОДНОЙ строки — кадр оставался кривым, и понять это можно было только по картинке.
//   Теперь отказ: (1) кричит Warning с причиной и числами, (2) ВОЗВРАЩАЕТ камеру к оценке по
//   габаритам ассета, чтобы кадр был предсказуемо-приблизительным, а не случайным огрызком
//   последнего прохода.
void AAsdaPetPreviewActor::ReportAutoFitGaveUp(const TCHAR* Why)
{
	UE_LOG(LogAsdaPetPreview, Warning,
		TEXT("[превью-питомца] АВТОПОДБОР НЕ СОШЁЛСЯ за %d проход(ов) у породы %d: %s. ")
		TEXT("Откат к оценке по габаритам: взгляд %.1f → %.1f, дистанция %.1f → %.1f. ")
		TEXT("Кадр будет приблизительным (ключ asda.Pet.PreviewAutoFit)."),
		AutoFitStep, ShownPetTypeId, Why ? Why : TEXT("причина не указана"),
		CamLookZ, FallbackLookZ, CamDist, FallbackDist);
	CamLookZ = FallbackLookZ;
	CamDist  = FallbackDist;
	PlaceCapture();
}

// Поставить капчер по текущим `CamLookZ` / `CamDist`. Камера смотрит на модель спереди: глаз
//   отодвинут по −X и поднят на высоту точки взгляда, сама точка взгляда — на оси модели
//   (`npc_mob_pet.c:5637-5639`: цель = глаз + вперёд × 150).
void AAsdaPetPreviewActor::PlaceCapture()
{
	USceneCaptureComponent2D* C = Capture.Get();
	if (!C) { return; }
	const float FovYRad = FMath::DegreesToRadians(FovYDeg);
	const float FovXRad = 2.f * FMath::Atan(FMath::Tan(FovYRad * 0.5f) * Aspect);
	const FVector Base = GetActorLocation();
	const FVector Look = Base + FVector(0.f, 0.f, CamLookZ);
	const FVector Eye = Look + FVector(-CamDist, 0.f, 0.f);
	C->SetWorldLocation(Eye);
	C->SetWorldRotation((Look - Eye).Rotation());
	C->FOVAngle = FMath::RadiansToDegrees(FovXRad);
	C->bOverride_CustomNearClippingPlane = true;
	C->CustomNearClippingPlane = NearClip;
	C->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
}
