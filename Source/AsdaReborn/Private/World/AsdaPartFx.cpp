// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ЭФФЕКТЫ ЧАСТЕЙ ЭКИПИРОВКИ — НАБОРЫ .PEB (07.11.2026, баг владельца: пламя брони Lucifer). [ОРИГИНАЛ]
//   Эталон: при смене меша части `CCharacter_LoadHelmetPart@0x9E3390` (часть 0), `tagCHARACTER_SetPartsModel_Chest@0x9E4A80` (1),
//   `_Gloves@0x9E5320` (2), `_Pants@0x9E5BC0` (3), `CCharacter_SetParts_Shose@0x9E6460` (4), `CCharacter_LoadEquipPart@0x9E6D80`
//   (5 груз, 6 оружие) зовут `effLoadPebEffectSet@0x9EDA00` с именем меша (ImgRecord+4 муж / +68 жен):
//     • сначала гасят 3 слота части, затем читают `<меш>.PEB` из CharModel.bnz: магия 537266192, 3 записи {i32 номер кости, char[256] .jeb};
//     • номер кости -1 или пустое имя — слот пуст; файла нет — у части эффектов нет.
//   Снятая вещь и скрытый шлем гасят слоты (entity_char.c:12680, :12688, :13009, :13307, :13605, :13905).
//   Показ: `charEntityUpdateTick@0xA1ED30` (entity_char.c:22394) каждый кадр ставит эффекту матрицу «кость × мир» — attach-10;
//   очередь — `entityCollectNameplates@0x9D8050` (:8969). Гейта по состоянию нет: работает для всех персонажей (свой, соседи), на
//   транспорте, на трупе. Превью выбора персонажа сюда не входит: 7×3 слота читают только эти две функции мира.
//   Данные: в CharModel.bnz ровно 24 файла .PEB, все — семейство 305 (шлем, тело, перчатки, обувь; 3 вида; муж и жен), у штанов,
//   груза и оружия их нет. Ниже — таблица этих файлов: номер кости заменён именем по порядку костей клипа персонажа
//   (муж 50 костей, жен 54; 37/38 у мужчин и 40/41 у женщин — одна и та же пара 7_b2/_7_b2). Ведомость предметов:
//   Reports/batch13/part_fx/part_fx_items.csv.
//   Порт: эффект-акторы с тегом `PartFx`, привязка `ConfigureBoneAttach` (OwnerBoundFx = attach-10 на живую кость). Заказ обновляет
//   `AAsdaPlayerCharacter::InitAppearance` — единая точка сборки своего персонажа и соседей; сверка в тике возвращает эффект,
//   если его снял общий сброс эффектов носителя.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "AsdaEffectActor.h"
#include "Characters/AsdaWornParts.h"   // ★ R3: FAsdaWornParts — узкий заголовок вместо AsdaCharAssembly.h
#include "Items/AsdaItemTableSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPartFx, Log, All);

static ASDA_DEV_CVAR(int32, CVarAsdaCharPartFx,
	TEXT("asda.Char.PartFx"), 1,
	TEXT("[ОРИГИНАЛ] 1 — части экипировки с файлом .PEB (семейство 305: Lucifer и др.) несут эффекты на костях, как ")
	TEXT("effLoadPebEffectSet; 0 — как было (эффектов нет)."),
	ECVF_Default);

static ASDA_DEV_CVAR(float, CVarAsdaCharPartFxTrailMeters,
	TEXT("asda.Char.PartFxTrailMeters"), 1.f,
	TEXT("[ИСПРАВЛЕНИЕ] Видимая длина шлейфа брони семейства 305 видов 2 и 3, метры (решение владельца 07.11.2026: 1 м). ")
	TEXT("0 — длина из данных .jeb (видно: обувь 2 м, шлем 1,3 м). Вид 1 не меняется."),
	ECVF_Default);

namespace AsdaPartFx
{
	static const FName kTag(TEXT("PartFx"));   // метка всех эффектов частей на носителе

	// Одна запись .PEB: кость и база имени .jeb (nullptr — слот пуст).
	struct FSlot { const TCHAR* Bone; const TCHAR* Jeb; };
	// Файл .PEB: имя меша части и три слота.
	struct FPeb { const TCHAR* Mesh; FSlot Slots[3]; };

	// Содержимое 24 файлов .PEB (порядок слотов — как в файле). Кости «_11» и «_6» названы именами импорта UE: он переименовал
	//   кости вида «_N» в «Null_N», а FName("_11") даёт None.
	static const FPeb kPeb[] =
	{
	{ TEXT("ch1_f_00_chst305_01_00_0"), { {TEXT("Null_6"), TEXT("ch1_f_00_chst305_00_00_2_1")}, {TEXT("6"), TEXT("ch1_f_00_chst305_00_00_2_0")}, {nullptr, nullptr} } },
	{ TEXT("ch1_f_00_chst305_02_00_0"), { {TEXT("Null_6"), TEXT("ch1_f_00_chst305_00_00_2_1")}, {TEXT("6"), TEXT("ch1_f_00_chst305_00_00_2_0")}, {nullptr, nullptr} } },
	{ TEXT("ch1_f_00_chst305_03_00_0"), { {TEXT("Null_6"), TEXT("ch1_f_00_chst305_00_00_3_1")}, {TEXT("6"), TEXT("ch1_f_00_chst305_00_00_3_0")}, {nullptr, nullptr} } },
	{ TEXT("ch1_f_00_grov305_01_00_0"), { {TEXT("7_b2"), TEXT("ch1_m_00_grov305_00_00_01_0")}, {TEXT("_7_b2"), TEXT("ch1_m_00_grov305_00_00_01_1")}, {nullptr, nullptr} } },
	{ TEXT("ch1_f_00_grov305_02_00_0"), { {TEXT("7_b2"), TEXT("ch1_m_00_grov305_00_00_02_0")}, {TEXT("_7_b2"), TEXT("ch1_m_00_grov305_00_00_02_1")}, {nullptr, nullptr} } },
	{ TEXT("ch1_f_00_grov305_03_00_0"), { {TEXT("7_b2"), TEXT("ch1_m_00_grov305_00_00_03_0")}, {TEXT("_7_b2"), TEXT("ch1_m_00_grov305_00_00_03_1")}, {nullptr, nullptr} } },
	{ TEXT("ch1_f_00_hcap305_01_00_0"), { {TEXT("4"), TEXT("ch1_f_00_hcap305_00_00_1")}, {nullptr, nullptr}, {nullptr, nullptr} } },
	{ TEXT("ch1_f_00_hcap305_02_00_0"), { {TEXT("4"), TEXT("ch1_f_00_hcap305_00_00_2")}, {nullptr, nullptr}, {nullptr, nullptr} } },
	{ TEXT("ch1_f_00_hcap305_03_00_0"), { {TEXT("4"), TEXT("ch1_f_00_hcap305_00_00_3")}, {nullptr, nullptr}, {nullptr, nullptr} } },
	{ TEXT("ch1_f_00_shoe305_01_00_0"), { {TEXT("11"), TEXT("ch1_f_00_shoe305_00_00_1")}, {TEXT("Null_11"), TEXT("ch1_f_00_shoe305_00_00_1")}, {nullptr, nullptr} } },
	{ TEXT("ch1_f_00_shoe305_02_00_0"), { {TEXT("11"), TEXT("ch1_f_00_shoe305_00_00_2")}, {TEXT("Null_11"), TEXT("ch1_f_00_shoe305_00_00_2")}, {nullptr, nullptr} } },
	{ TEXT("ch1_f_00_shoe305_03_00_0"), { {TEXT("11"), TEXT("ch1_f_00_shoe305_00_00_3")}, {TEXT("Null_11"), TEXT("ch1_f_00_shoe305_00_00_3")}, {nullptr, nullptr} } },
	{ TEXT("ch1_m_00_chst305_01_00_0"), { {TEXT("Null_6"), TEXT("ch1_m_00_chst305_00_00_2_1")}, {TEXT("2"), TEXT("ch1_m_00_chst305_00_00_1")}, {TEXT("6"), TEXT("ch1_m_00_chst305_00_00_2_0")} } },
	{ TEXT("ch1_m_00_chst305_02_00_0"), { {TEXT("Null_6"), TEXT("ch1_m_00_chst305_00_00_2_1")}, {TEXT("6"), TEXT("ch1_m_00_chst305_00_00_2_0")}, {TEXT("2"), TEXT("ch1_m_00_chst305_00_00_2")} } },
	{ TEXT("ch1_m_00_chst305_03_00_0"), { {TEXT("Null_6"), TEXT("ch1_m_00_chst305_00_00_3_1")}, {TEXT("2"), TEXT("ch1_m_00_chst305_00_00_3")}, {TEXT("6"), TEXT("ch1_m_00_chst305_00_00_3_0")} } },
	{ TEXT("ch1_m_00_grov305_01_00_0"), { {TEXT("7_b2"), TEXT("ch1_m_00_grov305_00_00_01_0")}, {TEXT("_7_b2"), TEXT("ch1_m_00_grov305_00_00_01_1")}, {nullptr, nullptr} } },
	{ TEXT("ch1_m_00_grov305_02_00_0"), { {TEXT("7_b2"), TEXT("ch1_m_00_grov305_00_00_02_0")}, {TEXT("_7_b2"), TEXT("ch1_m_00_grov305_00_00_02_1")}, {nullptr, nullptr} } },
	{ TEXT("ch1_m_00_grov305_03_00_0"), { {TEXT("7_b2"), TEXT("ch1_m_00_grov305_00_00_03_0")}, {TEXT("_7_b2"), TEXT("ch1_m_00_grov305_00_00_03_1")}, {nullptr, nullptr} } },
	{ TEXT("ch1_m_00_hcap305_01_00_0"), { {TEXT("4"), TEXT("ch1_m_00_hcap305_00_00_1")}, {nullptr, nullptr}, {nullptr, nullptr} } },
	{ TEXT("ch1_m_00_hcap305_02_00_0"), { {TEXT("4"), TEXT("ch1_m_00_hcap305_00_00_2")}, {nullptr, nullptr}, {nullptr, nullptr} } },
	{ TEXT("ch1_m_00_hcap305_03_00_0"), { {TEXT("4"), TEXT("ch1_m_00_hcap305_00_00_3")}, {nullptr, nullptr}, {nullptr, nullptr} } },
	{ TEXT("ch1_m_00_shoe305_01_00_0"), { {TEXT("11"), TEXT("ch1_m_00_shoe305_00_00_1")}, {TEXT("Null_11"), TEXT("ch1_m_00_shoe305_00_00_1")}, {nullptr, nullptr} } },
	{ TEXT("ch1_m_00_shoe305_02_00_0"), { {TEXT("11"), TEXT("ch1_m_00_shoe305_00_00_2")}, {TEXT("Null_11"), TEXT("ch1_m_00_shoe305_00_00_2")}, {nullptr, nullptr} } },
	{ TEXT("ch1_m_00_shoe305_03_00_0"), { {TEXT("11"), TEXT("ch1_m_00_shoe305_00_00_3")}, {TEXT("Null_11"), TEXT("ch1_m_00_shoe305_00_00_3")}, {nullptr, nullptr} } },
	};

	// Заказанный эффект: кость, .jeb и метка актора, по которой сверка узнаёт уже стоящий эффект.
	struct FWant { FName Bone; FString Jeb; FName Key; bool bTrail = false; };

	// Заказ носителя: что должно стоять и счётчик кадров сверки.
	struct FOrder { TArray<FWant> Wants; uint32 Frame = 0; };
	static TMap<TWeakObjectPtr<AAsdaPlayerCharacter>, FOrder> GOrders;

	// Запись .PEB по имени меша (nullptr — файла нет).
	static const FPeb* Find(const FString& Mesh)
	{
		for (const FPeb& P : kPeb) { if (Mesh.Equals(P.Mesh, ESearchCase::IgnoreCase)) { return &P; } }
		return nullptr;
	}

	// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] Длина шлейфа. Отступление от данных оригинала по решению владельца: у эталона частица летит
	//   скорость × жизнь (обувь 200 × 3 с, шлем 150 × 3 с). Укорачиваем ЖИЗНЬ: темп рождения и скорость те же, поэтому частиц на метр
	//   столько же, а прозрачность и размер (Rate = разность / Life) доходят до конца на новой длине. Правится копия эмиттера на акторе,
	//   исходная жизнь берётся из класса, поэтому ключ 0 возвращает данные. 1 м = 100 единиц мира.
	//   Ключ задаёт ВИДИМУЮ длину. Хвост гаснет раньше конца пути: при данных частица летит 5,7 м (обувь) и 4,3 м (шлем), а в кадре видно 2 м
	//   и 1,3 м (хвост ещё и уходит за рельеф). Зависимость видимой длины от пути нелинейна: при пути 1 м видно 0,2 м. Значения ниже подобраны
	//   по кадрам и приняты владельцем по виду 07.11.2026 («perfect, keep it»); кадры — Reports/batch13/part_fx_trail.
	static void ApplyTrail(AAsdaEffectActor* E, bool bTrail)
	{
		const AAsdaEffectActor* Def = E->GetClass()->GetDefaultObject<AAsdaEffectActor>();
		constexpr float kVisibleShare = 0.36f;   // подбор по кадрам; значение принято владельцем 07.11.2026, не менять
		constexpr float kHiddenCm = 20.f;        // начало шлейфа, закрытое телом и шипами, см
		const float Meters = CVarAsdaCharPartFxTrailMeters.GetValueOnGameThread();
		const float Length = Meters > 0.f ? (Meters * 100.f + kHiddenCm) / kVisibleShare : 0.f;   // путь частицы
		for (int32 I = 0; I < E->Emitters.Num(); ++I)
		{
			if (!Def || !Def->Emitters.IsValidIndex(I) || !E->IsWorldSpaceParticleEmitter(I)) { continue; }
			FAsdaFxEmitter& Em = E->Emitters[I];
			const float Speed = Em.Nodes.Num() > 0 ? Em.Nodes[0].Speed : 0.f;
			const float Base = Def->Emitters[I].LifeBase;
			Em.LifeBase = (bTrail && Length > 0.f && Speed > 0.f) ? FMath::Min(Base, Length / Speed) : Base;
		}
	}

	// Поставить один эффект на кость. Шаги — как у `UAsdaWorldSubsystem::SpawnCharFx` (она закрытая), привязка — attach-10.
	static bool SpawnOne(AAsdaPlayerCharacter* Ch, const FWant& W)
	{
		UWorld* World = Ch->GetWorld();
		if (!World || !Ch->BaseSkel) { return false; }
		if (Ch->BaseSkel->GetBoneIndex(W.Bone) == INDEX_NONE)
		{
			UE_LOG(LogAsdaPartFx, Warning, TEXT("[часть] кости '%s' нет в скелете — эффект %s пропущен"), *W.Bone.ToString(), *W.Jeb);
			return false;
		}
		AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnByJebName(World, W.Jeb, Ch->GetActorLocation(), FRotator::ZeroRotator, Ch);
		if (!Fx)
		{
			UE_LOG(LogAsdaPartFx, Warning, TEXT("[часть] BP эффекта %s не найден (Tools/asda_build_char_fx.py)"), *W.Jeb);
			return false;
		}
		Fx->AttachToComponent(Ch->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
		Fx->Tags.Add(kTag);
		Fx->Tags.Add(W.Key);
		Fx->bFollowTickAfterPose = true;
		Fx->ApplyFollowTickOrder();
		Fx->ConfigureBoneAttach(Ch, Ch->BaseSkel, W.Bone, EAsdaFxAttachMode::OwnerBoundFx);
		ApplyTrail(Fx, W.bTrail);
		if (Ch->IsHidden()) { Fx->SetActorHiddenInGame(true); }   // GM-«скрыться»: как `SpawnCharFx`
		return true;
	}

	// Привести эффекты носителя к заказу: лишние снять, недостающие поставить. Стоящие не трогаем — их цикл не сбивается.
	static void Reconcile(AAsdaPlayerCharacter* Ch, const TArray<FWant>& Wants)
	{
		TArray<AActor*> Attached;
		Ch->GetAttachedActors(Attached);
		TSet<FName> Have;
		for (AActor* A : Attached)
		{
			if (!IsValid(A) || !A->ActorHasTag(kTag)) { continue; }
			const FWant* Want = Wants.FindByPredicate([A](const FWant& W) { return A->ActorHasTag(W.Key); });
			const bool bWanted = Want != nullptr;
			if (bWanted)
			{
				for (const FName& T : A->Tags) { Have.Add(T); }
				if (AAsdaEffectActor* E = Cast<AAsdaEffectActor>(A)) { ApplyTrail(E, Want->bTrail); }   // ключ меняется на ходу
			}
			else { A->Destroy(); }
		}
		for (const FWant& W : Wants)
		{
			if (!Have.Contains(W.Key)) { SpawnOne(Ch, W); }
		}
	}
}

// Включён ли показ эффектов частей.
bool AsdaPartFxOn()
{
	return CVarAsdaCharPartFx.GetValueOnGameThread() != 0;
}

// Сколько эффектов в файле .PEB меша (самотест; 0 — файла нет).
int32 AsdaPartFxSlotsOfMesh(const FString& Mesh)
{
	const AsdaPartFx::FPeb* P = AsdaPartFx::Find(Mesh);
	int32 N = 0;
	if (P) { for (const AsdaPartFx::FSlot& S : P->Slots) { if (S.Jeb) { ++N; } } }
	return N;
}

// Обновить заказ носителя по собранной внешности (вызов — `AAsdaPlayerCharacter::InitAppearance`).
void AsdaPartFxRefresh(AAsdaPlayerCharacter* Ch, const FAsdaWornParts& Parts)
{
	if (!Ch) { return; }
	using namespace AsdaPartFx;
	for (auto It = GOrders.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
	TArray<FWant> Wants;
	const UGameInstance* GI = Ch->GetGameInstance();
	UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (AsdaPartFxOn() && Items)
	{
		const bool bMale = Ch->Sex != 2;
		// Части эталона 0..4 и 6; груза (5) в порту нет. Шлем скрыт — его слоты гаснут (entity_char.c:12003).
		const int32 Worn[] = { Parts.bHelmetShown ? Parts.Helmet : 0, Parts.Chest, Parts.Gloves, Parts.Pants, Parts.Shoes, Parts.Weapon };
		for (int32 Part = 0; Part < UE_ARRAY_COUNT(Worn); ++Part)
		{
			if (Worn[Part] <= 0) { continue; }
			const FPeb* P = Find(Items->GetItemMeshName(Worn[Part], bMale));
			if (!P) { continue; }
			for (int32 I = 0; I < 3; ++I)
			{
				const FSlot& S = P->Slots[I];
				if (!S.Jeb) { continue; }
				FWant W;
				W.Bone = FName(S.Bone);
				W.Jeb = S.Jeb;
				W.bTrail = !FString(P->Mesh).Contains(TEXT("305_01_"));   // вид 1 — по данным
				W.Key = FName(*FString::Printf(TEXT("PartFx_%d_%d_%s"), Part, I, S.Jeb));
				Wants.Add(MoveTemp(W));
			}
		}
	}
	if (Wants.Num() == 0) { GOrders.Remove(Ch); }
	else { GOrders.FindOrAdd(Ch).Wants = Wants; }
	Reconcile(Ch, Wants);
	UE_LOG(LogAsdaPartFx, Log, TEXT("[часть] %s: эффектов частей %d"), *Ch->GetName(), Wants.Num());
}

// Сверка в тике носителя: раз в 16 кадров вернуть снятое общим сбросом. Без заказа — один поиск в карте.
void AsdaPartFxTick(AAsdaPlayerCharacter* Ch)
{
	AsdaPartFx::FOrder* O = AsdaPartFx::GOrders.Find(Ch);
	if (!O) { return; }
	if (!AsdaPartFxOn())
	{
		AsdaPartFx::GOrders.Remove(Ch);
		AsdaPartFx::Reconcile(Ch, {});
		return;
	}
	if ((++O->Frame & 15u) == 0) { AsdaPartFx::Reconcile(Ch, O->Wants); }
}

// Сколько эффектов частей стоит на носителе (самотест).
int32 AsdaPartFxCount(const AActor* Ch)
{
	if (!Ch) { return 0; }
	TArray<AActor*> Attached;
	Ch->GetAttachedActors(Attached);
	int32 N = 0;
	for (const AActor* A : Attached) { if (IsValid(A) && A->ActorHasTag(AsdaPartFx::kTag)) { ++N; } }
	return N;
}
