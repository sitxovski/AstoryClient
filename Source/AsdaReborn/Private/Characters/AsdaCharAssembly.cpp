// Copyright (c) 2026 @sitxovski (Discord)
// Реализация системы сборки визуала персонажа — см. AsdaCharAssembly.h.
#include "Characters/AsdaCharAssembly.h"
#include "Npc/AsdaMobFxMaterial.h"   // ★ 01.11.2026 (mob_fx_audit): Custom Depth прозрачных деталей
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"

#include "Resources/AsdaModelAnimSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "World/AsdaCapsuleShadow.h"   // ★ 26.10.2026 [ИСПРАВЛЕНИЕ] части экипа — приёмники капсульной тени (баг «Ultra: чёрный персонаж»)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCharAssembly, Log, All);

// «Подшлемный» женский меш причёски (режим 1 шлема, `entity_char.c:11485-11488`) — одна точка для сборки и предзагрузки.
static const TCHAR* kUnderHelmetHairMeshF = TEXT("ch1_f_00_hair000_00_00_1");

FString UAsdaCharAssembly::EquipPartMeshPath(const FString& MeshName)
{
	// EquipParts/<name>/<name> — раскладка ассетов экипа (как в исходных ApplyEquipToSlot/ApplyCreateParts).
	return AsdaAssetPaths::EquipPartMesh(MeshName);   // ★ волна 2 п. 7: правило в реестре путей
}

FString UAsdaCharAssembly::CharStaticMeshPath(const FString& MeshName)
{
	return AsdaAssetPaths::CharStaticMeshObject(MeshName);   // ★ волна 2 п. 7
}

FString UAsdaCharAssembly::CharTexturePath(const FString& TexName)
{
	// Текстуры моделей персонажей — Textures/Characters/Texture (как у ApplyHair/ApplyFace/ApplyItemTexture).
	return AsdaAssetPaths::CharTextureObject(TexName);   // ★ волна 2 п. 7
}

FString UAsdaCharAssembly::FaceTexturePath(bool bMale, int32 FaceIndex)
{
	// Эталон SetParts_FaceTexture: face001_{NN}, NN — 2-значный с ведущим нулём.
	return CharTexturePath(AsdaAssetPaths::FaceTextureName(bMale, FaceIndex));   // ★ волна 2 п. 7
}

FString UAsdaCharAssembly::DefaultHandsMeshName(bool bMale)
{
	// Базовая часть — grov001 (нумерация 001 у всех базовых: chst001/pant001/shoe001).
	//   grov105 брать НЕЛЬЗЯ: это декоративные перчатки, а не голые кисти.
	return bMale ? FString(TEXT("ch1_m_00_grov001_00_00_0")) : FString(TEXT("ch1_f_00_grov001_00_00_0"));
}

USkeletalMesh* UAsdaCharAssembly::LoadEquipPartMesh(const FString& MeshName)
{
	if (MeshName.IsEmpty()) return nullptr;
	UAsdaModelAnimSubsystem* M = GEngine ? GEngine->GetEngineSubsystem<UAsdaModelAnimSubsystem>() : nullptr;
	if (!M) return nullptr;
	return M->LoadModel(EquipPartMeshPath(MeshName));
}

USkeletalMeshComponent* UAsdaCharAssembly::AttachPart(AActor* Owner, USceneComponent* Root, USkeletalMeshComponent* BaseSkel,
	bool bMale, int32 ItemId, const TCHAR* BaseSectionToHide, UAsdaItemTableSubsystem* Items)
{
	if (!Owner || !Root || !BaseSkel || !Items || ItemId <= 0) return nullptr;

	const FString Mesh = Items->GetItemMeshName(ItemId, bMale);
	if (Mesh.IsEmpty()) return nullptr;
	USkeletalMesh* PartMesh = LoadEquipPartMesh(Mesh);
	if (!PartMesh)
	{
		UE_LOG(LogAsdaCharAssembly, Warning, TEXT("Часть item=%d (%s) не найдена"), ItemId, *Mesh);
		return nullptr;
	}

	// Оверлей-компонент части: деформируется idle-позой базового тела (LeaderPose=BaseSkel).
	USkeletalMeshComponent* PC = NewObject<USkeletalMeshComponent>(Owner, *(FString(TEXT("Equip_")) + Mesh));
	PC->SetupAttachment(Root);
	PC->SetSkeletalMesh(PartMesh);
	PC->SetRelativeTransform(FTransform::Identity);
	PC->SetCastShadow(true);
	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (Reports/ultra_shadows/): бит приёмника капсульной тени — иначе одежда чернеет от
	//   капсул своего тела в не-Lumen пути неба (Среднее, DX11). Разбор — AsdaCapsuleShadow.h; ключ asda.Shadow.CapsuleSelfFix.
	AsdaShadow::ApplyCapsuleReceiver(PC, BaseSkel);
	PC->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	// ★ Метка «часть внешности» — по ней `ClearWornParts` снимет её при следующей пересборке.
	PC->ComponentTags.Add(WornPartTag);
	PC->RegisterComponent();
	PC->SetLeaderPoseComponent(BaseSkel);

	// ★ ЭТАЛОННАЯ текстура части = item_img_table.TexMale (НЕ запечённая из .jbm — там бывает чужая/мусор,
	//   и один меш шарится разными предметами). Свап "Tex" на всех слотах — как движок ставит TexMale.
	ApplyItemTexture(PC, Items->GetItemTexName(ItemId, bMale));

	// Скрыть ТОЛЬКО совпадающую базовую секцию (chst/pant/shoe), чтобы часть её заменила.
	// Шлем/перчатки (BaseSectionToHide==nullptr) — базу не трогают (в базовом теле нет их секций).
	if (BaseSectionToHide)
	{
		for (int32 mi = 0; mi < BaseSkel->GetNumMaterials(); ++mi)
		{
			if (UMaterialInterface* M = BaseSkel->GetMaterial(mi))
			{
				if (M->GetName().Contains(BaseSectionToHide)) { BaseSkel->ShowMaterialSection(mi, mi, false, 0); break; }
			}
		}
	}
	return PC;
}

// ═══ ★★★ 31.10.2026 [ОРИГИНАЛ] ПРОЗРАЧНЫЕ ЧАСТИ ПЕРСОНАЖА (волна acce_alpha, Reports/acce_alpha/REPORT.md) ═══
//   Эталон: подсетка .jbm с битом очереди mr+0x004 & 1 не рисуется в опак-проходе (meshNodeRenderDispatch@0x111E110),
//   а уходит в g_RenderQueue (entityCollectNameplates@0x9D8050), которую CRenderer_RenderWorldFrame@0x9773B0 рисует
//   при g_bUseEffectPath = 0 (render_core.c:1418-1446) — фиксированным конвейером: альфа = Tex.A, SRCALPHA/INVSRCALPHA,
//   альфа-тест >= 8/255. Без этого тату/наклейки (acce*), венки, кружево, вуали выходили белой/чёрной плашкой:
//   M_AsdaCharLit непрозрачный, а под прозрачной альфой в текстуре — белый или чёрный фон.
//   Список пар «меш+текстура» и режим (мягкая альфа → translucent, жёсткая вырезка → masked) — генерируемая таблица.
#include "AsdaCharAlphaParts.inl"

// Ключ отката: 1 — как оригинал (прозрачные части через M_AsdaCharLitBlend/Mask); 0 — прежний порт (всё M_AsdaCharLit).
static ASDA_DEV_CVAR(int32, CVarItemAlpha,
	TEXT("asda.Char.ItemAlpha"), 1,
	TEXT("1 = как оригинал: части, которые оригинал рисует смешиванием по альфе текстуры (бит очереди .jbm), "
	     "получают прозрачный вариант мастера; 0 = прежнее поведение порта: все части непрозрачные (белые/чёрные плашки)."),
	ECVF_Default);

// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] баг 8 (bug_chest_invisible, Reports/bug_chest_invisible/): у СМЕШАННЫХ частей (бит очереди
//   лишь у части подсеток .jbm) прозрачный мастер получал весь единственный слот UE — непрозрачный «корпус» (жилет, штаны,
//   шлем, древко лука) становился translucent без записи глубины: двусторонняя изнанка рисовалась поверх лица, вещь
//   выглядела невидимой/прозрачной. Оригинал рисует подсетки БЕЗ бита обычным непрозрачным проходом
//   (meshNodeRenderDispatch@0x111E110), очередь прозрачных — только подсетки С битом. Бит-подсетки перенесены в слот
//   `<слот0>_asdabit` (UAsdaAlphaSplitLib, AsdaImporters; 101 скелетная часть + 8 мешей оружия) — прозрачный мастер
//   получает только он. 1 — так; 0 — как alpha-5.0.4: прозрачный мастер на все слоты части.
static ASDA_DEV_CVAR(int32, CVarItemAlphaBitSlotOnly,
	TEXT("asda.Char.ItemAlphaBitSlotOnly"), 1,
	TEXT("1 = как оригинал: у смешанных частей прозрачный мастер только у слота бит-подсеток (_asdabit), остальные слоты "
	     "непрозрачные; 0 = как alpha-5.0.4: прозрачный мастер на все слоты части (непрозрачный корпус выглядел невидимым)."),
	ECVF_Default);

// Признак «смешанный меш» в упакованном значении AsdaCharAlphaMode (бит 7; режим — биты 0..6).
static constexpr uint8 kAlphaMixedBit = 0x80;

// Режим прозрачности части по таблице: 0 — нет (непрозрачный M_AsdaCharLit), 1 — Blend, 2 — Mask;
//   | kAlphaMixedBit — смешанный меш (бит очереди лишь у части подсеток, бит-подсетки в слоте `_asdabit`).
static uint8 AsdaCharAlphaMode(const FString& MeshName, const FString& TexName)
{
	// Индекс «меш|текстура» (нижний регистр) строится один раз из генерируемой таблицы.
	static TMap<FString, uint8> Index;
	if (Index.Num() == 0)
	{
		for (const FAsdaCharAlphaPart& P : GAsdaCharAlphaParts)
		{
			Index.Add(FString(P.Mesh) + TEXT("|") + P.Tex, (uint8)(P.Mode | (P.bMixed ? kAlphaMixedBit : 0)));
		}
	}
	const FString M = MeshName.ToLower();
	if (const uint8* V = Index.Find(M + TEXT("|") + TexName.ToLower())) { return *V; }
	if (const uint8* V = Index.Find(M + TEXT("|"))) { return *V; }   // запись «любая текстура» (acce* донора)
	return 0;
}

// ★ Свап модельной текстуры предмета (эталон item_img_table.TexMale) на все материальные слоты.
void UAsdaCharAssembly::ApplyItemTexture(UMeshComponent* Comp, const FString& TexName)
{
	if (!Comp || TexName.IsEmpty()) { return; }
	// Путь как у ApplyHair/ApplyFace: текстуры моделей персонажей в Textures/Characters/Texture.
	const FString TexPath = CharTexturePath(TexName);
	UAsdaTextureSubsystem* TexMgr = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
	UTexture2D* Tex = TexMgr ? TexMgr->LoadTextureCached(TexPath) : nullptr;
	if (!Tex)
	{
		UE_LOG(LogAsdaCharAssembly, Warning, TEXT("ApplyItemTexture: текстура не найдена %s"), *TexName);
		return;
	}
	// ★ acce_alpha: имя меша части — из самого компонента (скелетная часть экипа или жёсткий меш оружия/щита).
	FString MeshName;
	if (const USkeletalMeshComponent* SK = Cast<USkeletalMeshComponent>(Comp))
	{
		MeshName = GetNameSafe(SK->GetSkeletalMeshAsset());
	}
	else if (const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(Comp))
	{
		MeshName = GetNameSafe(SM->GetStaticMesh());
	}
	const uint8 Packed = CVarItemAlpha.GetValueOnGameThread() != 0 ? AsdaCharAlphaMode(MeshName, TexName) : 0;
	uint8 AlphaMode = Packed & ~kAlphaMixedBit;   // режим прозрачности (0 — непрозрачная часть)
	// ★ баг 8: есть ли у меша отдельный слот бит-подсеток (тогда прозрачный мастер — только ему).
	const TArray<FName> SlotNames = Comp->GetMaterialSlotNames();   // имена слотов по индексу материала
	bool bHasBitSlot = false;
	for (const FName& SN : SlotNames) { bHasBitSlot |= SN.ToString().EndsWith(AsdaMat::kBitSlotSuffix); }
	const bool bBitKey = CVarItemAlphaBitSlotOnly.GetValueOnGameThread() != 0;   // исправление бага 8 включено
	// ★ 01.11.2026 [ИСПРАВЛЕНИЕ] защита от переимпорта: смешанный меш без слота бит-подсеток (переимпорт из .glb стёр
	//   слот) НЕ получает прозрачный мастер на весь меш (это и был баг 8 — корпус невидим): часть рисуется непрозрачной,
	//   в журнал — предупреждение (вернуть слот: Reports/bug_chest_invisible/scripts/bci_split_data.py → bci_split_ue.py).
	if (AlphaMode != 0 && (Packed & kAlphaMixedBit) != 0 && !bHasBitSlot && bBitKey)
	{
		UE_LOG(LogAsdaCharAssembly, Warning,
			TEXT("ApplyItemTexture: смешанный меш %s без слота бит-подсеток (%s) — часть непрозрачная; перезапустить разделение слотов"),
			*MeshName, AsdaMat::kBitSlotSuffix);
		AlphaMode = 0;
	}
	UMaterialInterface* AlphaMaster = nullptr;   // вариант мастера для прозрачной части (нет — обычный путь)
	if (AlphaMode != 0)
	{
		AlphaMaster = LoadObject<UMaterialInterface>(nullptr, AlphaMode == 1 ? AsdaMat::kCharLitBlendPath : AsdaMat::kCharLitMaskPath);
		if (!AlphaMaster)
		{
			UE_LOG(LogAsdaCharAssembly, Warning, TEXT("ApplyItemTexture: нет варианта мастера (режим %d) для %s — часть непрозрачная"),
				AlphaMode, *MeshName);
		}
	}
	const bool bBitSlotOnly = bHasBitSlot && bBitKey;   // прозрачный мастер только слоту бит-подсеток
	for (int32 mi = 0; mi < Comp->GetNumMaterials(); ++mi)
	{
		if (UMaterialInterface* M = Comp->GetMaterial(mi))
		{
			// Слот получает прозрачный мастер: все слоты (прежнее) или только слот бит-подсеток.
			const bool bAlphaSlot = !bBitSlotOnly
				|| (SlotNames.IsValidIndex(mi) && SlotNames[mi].ToString().EndsWith(AsdaMat::kBitSlotSuffix));
			if (AlphaMaster && bAlphaSlot)
			{
				// Прозрачная часть: MID от варианта мастера с параметрами слота (тот же граф света), затем текстура предмета.
				//   Имя MID несёт имя исходника слота (правило проекта: системы сборки/«души» узнают слоты по имени материала).
				const FName AName = MakeUniqueObjectName(Comp, UMaterialInstanceDynamic::StaticClass(),
					FName(*FString::Printf(TEXT("MIDAlpha_%s"), *M->GetName())));
				UMaterialInstanceDynamic* AMID = UMaterialInstanceDynamic::Create(AlphaMaster, Comp, AName);
				AMID->CopyMaterialUniformParameters(M);
				AMID->SetTextureParameterValue(AsdaMat::kParamTex, Tex);
				Comp->SetMaterial(mi, AMID);
				// ★ 01.11.2026 (mob_fx_audit) [ИСПРАВЛЕНИЕ] «запись глубины» прозрачной детали (оригинал: очередь с ZWRITE=1):
				//   M_AsdaCharLitBlend (параметр DepthCull) гасит пиксели дальше ближней поверхности по Custom Depth, который пишет
				//   компонент. Ключ asda.Char.ItemAlphaDepth: 0 — DepthCull 0 и без прохода Custom Depth.
				if (AlphaMode == 1)
				{
					AMID->SetScalarParameterValue(TEXT("DepthCull"), AsdaMobFx::ItemAlphaDepthOn() ? 1.f : 0.f);
					AsdaMobFx::ApplyDepth(Comp);
				}
				continue;
			}
			if (UMaterialInstanceDynamic* MID = Comp->CreateDynamicMaterialInstance(mi, M))
			{
				MID->SetTextureParameterValue(AsdaMat::kParamTex, Tex);   // ★ В1-C F10
			}
		}
	}
}

void UAsdaCharAssembly::ApplyFace(USkeletalMeshComponent* BaseSkel, bool bMale, int32 FaceIndex)
{
	if (!BaseSkel || FaceIndex < 0) { return; }
	// Текстура лица по индексу (2-значный zero-pad, эталон SetParts_FaceTexture: face001_{NN}).
	const FString TexPath = FaceTexturePath(bMale, FaceIndex);
	// Через менеджер текстур (правило: ресурсы только через синглтон-менеджер, не LoadObject напрямую).
	UAsdaTextureSubsystem* TexMgr = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
	UTexture2D* FaceTex = TexMgr ? TexMgr->LoadTextureCached(TexPath) : nullptr;
	if (!FaceTex)
	{
		UE_LOG(LogAsdaCharAssembly, Warning, TEXT("ApplyFace: текстура не найдена %s"), *TexPath);
		return;
	}
	// Секция материала лица base-скелета (slot-имя содержит "face001"/"face"); свап Tex-параметра через MID.
	const int32 NumMats = BaseSkel->GetNumMaterials();
	for (int32 mi = 0; mi < NumMats; ++mi)
	{
		UMaterialInterface* M = BaseSkel->GetMaterial(mi);
		if (M && M->GetName().Contains(TEXT("face")))
		{
			UMaterialInstanceDynamic* MID = BaseSkel->CreateDynamicMaterialInstance(mi, M);
			if (MID) { MID->SetTextureParameterValue(AsdaMat::kParamTex, FaceTex); }   // M_AsdaObject диффуз-параметр
			break;
		}
	}
}

FString UAsdaCharAssembly::ResolveHairMeshName(bool bMale, int32 HairStyle, int32 ColorIdx, UAsdaItemTableSubsystem* Items)
{
	if (HairStyle < 0) return FString();
	const TCHAR* G = bMale ? TEXT("m") : TEXT("f");
	// ── 1) hairtable по трём ключам (эталон findRecordByThreeKeys(gender, hairStyle, hairColor)) ──
	//    Имя меша = поле Mesh записи (+45). Для стилей 25..37 оно имеет хвост "_0" — формулой не вывести.
	if (Items)
	{
		UDataTable* DT = Items->GetDataTable(FName(TEXT("hairtable")));
		if (!DT)
		{
			// Таблица недоступна (не закукана в билде) — не падаем, уходим на hardcode-fallback эталона.
			UE_LOG(LogAsdaCharAssembly, Warning,
				TEXT("ResolveHairMeshName: DT_hairtable недоступна — fallback (стиль %d, цвет %d)"), HairStyle, ColorIdx);
		}
		else
		{
			const int32 WantGender = bMale ? 1 : 2;   // hairtable: 1=М, 2=Ж
			const TArray<FName> RowNames = DT->GetRowNames();
			for (const FName& RN : RowNames)
			{
				const FAsdaHairtableRow* R = DT->FindRow<FAsdaHairtableRow>(RN, TEXT("ResolveHairMeshName"), /*bWarnIfMissing*/false);
				if (!R) continue;
				if (R->Gender == WantGender && R->HairStyle == HairStyle && R->ColorIdx == ColorIdx && !R->Mesh.IsEmpty())
				{
					return R->Mesh;
				}
			}
		}
	}
	// ── 2) fallback = хардкод-switch эталона (нет записи в таблице) ──
	//    м: style 1..4 → hair002..hair005, иначе hair001;  ж: style 0..4 → hair001..hair005, иначе лысая hair000.
	if (HairStyle >= 0 && HairStyle <= 4)
	{
		return FString::Printf(TEXT("ch1_%s_00_hair%03d_00_00_0"), G, HairStyle + 1);
	}
	return bMale ? FString(TEXT("ch1_m_00_hair001_00_00_0")) : FString(TEXT("ch1_f_00_hair000_00_00_1"));
}

// ★★★ РЕЖИМ ВОЛОС ПОД ШЛЕМОМ — см. подробный разбор эталона у объявления в .h.
static ASDA_DEV_CVAR(int32, CVarHelmetHidesHair,
	TEXT("asda.Char.HelmetHidesHair"), 1,
	TEXT("1 = как эталон: шлем скрывает/подменяет причёску по полю item_img_table.SubType"
	     "Male/Female (0 обычные, 1 подшлемные у женщин, 2 волос нет); 0 = волосы всегда обычные."),
	ECVF_Default);

int32 UAsdaCharAssembly::ResolveHelmetHairMode(int32 HelmetItemId, bool bMale, UAsdaItemTableSubsystem* Items)
{
	if (HelmetItemId <= 0 || !Items) { return 0; }              // шлема нет — причёска обычная
	if (CVarHelmetHidesHair.GetValueOnGameThread() == 0) { return 0; }
	const FAsdaItemRecord* Rec = Items->GetItem(HelmetItemId);
	if (!Rec) { return 0; }
	const FAsdaImgRecord* Img = Items->GetImgRecord(Rec->ImgIndex);
	if (!Img) { return 0; }
	const int32 Mode = bMale ? Img->SubTypeMale : Img->SubTypeFemale;
	// −1 у не-шлемов (15707 записей) и любое иное значение трактуем как «обычная причёска»:
	//   эталон при таком поле шлем вообще не грузит, но ронять сборку персонажа из-за данных нельзя.
	return (Mode == 1 || Mode == 2) ? Mode : 0;
}

// ★★★ 25.10.2026 [ОРИГИНАЛ] ФЛАГ ПОКАЗА ШЛЕМА — разбор у объявления `FAsdaWornParts::bHelmetShown`.
static ASDA_DEV_CVAR(int32, CVarHelmetShowFlag,
	TEXT("asda.Char.HelmetShowFlag"), 1,
	TEXT("1 = как эталон: снятый флаг показа шлема (4003[8] / 4005[6] / 4025 действие 111 / окно настроек) — шлем не рисуется, "
	     "причёска обычная (CCharacter_LoadHelmetPart, флаг 0); 0 = прежнее поведение порта: шлем рисуется всегда."),
	ECVF_Default);

int32 UAsdaCharAssembly::EffectiveHelmetId(const FAsdaWornParts& Parts)
{
	if (Parts.bHelmetShown || CVarHelmetShowFlag.GetValueOnGameThread() == 0) { return Parts.Helmet; }
	return 0;   // эталон: флаг 0 → ветка «шлема нет» (шлем не грузится, `LoadHairModel(char, 0)`)
}

FString UAsdaCharAssembly::ResolveHairTextureName(bool bMale, int32 HairStyle, int32 ColorIdx)
{
	if (HairStyle < 0) return FString();
	// ЭТАЛОН 1:1 (entity_char.c:11535/11791):
	//   sprintf(Buffer, "ch1_%c_00_hair00%d_00_0%d_0.TEX", hairStyle + 1, hairColor);
	// "hair00" + десятичное (style+1) БЕЗ дополнения нулями: 1→hair001, 11→hair0011, 38→hair0038.
	return FString::Printf(TEXT("ch1_%s_00_hair00%d_00_0%d_0"), bMale ? TEXT("m") : TEXT("f"), HairStyle + 1, ColorIdx);
}

UStaticMeshComponent* UAsdaCharAssembly::ApplyHair(AActor* Owner, USceneComponent* Root, USkeletalMeshComponent* BaseSkel, bool bMale, int32 HairStyle, int32 ColorIdx, UAsdaItemTableSubsystem* Items, bool bUnderHelmet)
{
	if (!Owner || !Root || HairStyle < 0) return nullptr;
	// ★ Режим 1 эталона: женщине под шлемом ставится ОТДЕЛЬНЫЙ «подшлемный» меш, мужчине —
	//   обычная причёска (`CCharacter_LoadHelmetPart`, `entity_char.c:12030-12038`).
	const FString Mesh = (bUnderHelmet && !bMale)
		? FString(kUnderHelmetHairMeshF)
		: ResolveHairMeshName(bMale, HairStyle, ColorIdx, Items);
	if (Mesh.IsEmpty()) return nullptr;
	// Меш волос = жёсткий StaticMesh (наши glb волос БЕЗ скиннинга), авторингован в МОДЕЛЬНОМ пространстве (bind-поза).
	UStaticMesh* HairMesh = LoadObject<UStaticMesh>(nullptr, *CharStaticMeshPath(Mesh));
	if (!HairMesh)
	{
		UE_LOG(LogAsdaCharAssembly, Warning, TEXT("ApplyHair: меш волос не найден %s (стиль %d)"), *Mesh, HairStyle);
		return nullptr;
	}
	UStaticMeshComponent* HC = NewObject<UStaticMeshComponent>(Owner, TEXT("HairMesh"));
	HC->SetStaticMesh(HairMesh);
	HC->SetCastShadow(true);
	// ★ КОСТЬ ГОЛОВЫ "4" (эталон: вшитая причёска скинована 100% к ней). Крепим статик-меш к этой кости с
	//   relative = IBM (обратная bind-CS кости): верты меша (в модельном простр.) ложатся 1:1, и меш СЛЕДУЕТ за
	//   idle-позой головы — как скинованная causal причёска. Иначе (Root identity) волосы в bind, тело в idle → «улетают».
	FTransform HeadIBM = FTransform::Identity;
	bool bBone = false;
	if (BaseSkel)
	{
		if (USkeletalMesh* SkMesh = BaseSkel->GetSkeletalMeshAsset())
		{
			const FReferenceSkeleton& Ref = SkMesh->GetRefSkeleton();
			const int32 BoneIdx = Ref.FindBoneIndex(FName(TEXT("4")));
			if (BoneIdx != INDEX_NONE)
			{
				const TArray<FTransform>& Local = Ref.GetRefBonePose();
				FTransform CS = FTransform::Identity;   // компонент-простр. bind-трансформ кости "4"
				for (int32 x = BoneIdx; x != INDEX_NONE; x = Ref.GetParentIndex(x)) { CS = CS * Local[x]; }
				HeadIBM = CS.Inverse();   // relative = boneBindCS^-1 → при bind верты = модельное простр.
				bBone = true;
			}
		}
	}
	if (bBone)
	{
		HC->SetupAttachment(BaseSkel, FName(TEXT("4")));
		HC->SetRelativeTransform(HeadIBM);
	}
	else
	{
		HC->SetupAttachment(Root);
		HC->SetRelativeTransform(FTransform::Identity);
		UE_LOG(LogAsdaCharAssembly, Warning, TEXT("ApplyHair: кость головы '4' не найдена — волосы на Root (могут смещаться)"));
	}
	// ★ Метка «часть внешности» — причёску тоже снимает `ClearWornParts` (иначе при пересборке
	//   на голове окажутся две причёски подряд).
	HC->ComponentTags.Add(WornPartTag);
	HC->RegisterComponent();
	// Цвет = свап "Tex" на цвет-текстуру по ФОРМУЛЕ ЭТАЛОНА (ColorIdx 0..2). Материал из .jbm не используется:
	//   у причёсок 011..025 строка материала в файле указывает на несуществующую hairNNN_00_00_0 (без 4-й цифры).
	const FString TexName = ResolveHairTextureName(bMale, HairStyle, ColorIdx);
	const FString TexPath = CharTexturePath(TexName);
	UAsdaTextureSubsystem* TexMgr = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
	UTexture2D* HairTex = TexMgr ? TexMgr->LoadTextureCached(TexPath) : nullptr;
	if (HairTex)
	{
		for (int32 mi = 0; mi < HC->GetNumMaterials(); ++mi)
		{
			if (UMaterialInterface* M = HC->GetMaterial(mi))
			{
				UMaterialInstanceDynamic* MID = HC->CreateDynamicMaterialInstance(mi, M);
				if (MID) { MID->SetTextureParameterValue(AsdaMat::kParamTex, HairTex); }   // ★ В1-C F10
			}
		}
	}
	else
	{
		UE_LOG(LogAsdaCharAssembly, Warning, TEXT("ApplyHair: цвет-текстура не найдена %s"), *TexPath);
	}
	return HC;
}

const FName UAsdaCharAssembly::WeaponMeshTag(TEXT("AsdaWeaponMesh"));
const FName UAsdaCharAssembly::WornPartTag(TEXT("AsdaWornPart"));

// ★★★ СНЯТИЕ ПРЕЖНЕЙ ВНЕШНОСТИ (см. развёрнутое «зачем» у объявления в заголовке).
//   Отбор строго по тегу `WornPartTag` — всё, что система не создавала, остаётся на месте.
void UAsdaCharAssembly::ClearWornParts(AActor* Owner, USkeletalMeshComponent* BaseSkel)
{
	if (!Owner) { return; }
	TArray<UActorComponent*> Doomed;
	Owner->GetComponents(Doomed);
	int32 nKilled = 0;
	for (UActorComponent* C : Doomed)
	{
		if (!C || !C->ComponentHasTag(WornPartTag)) { continue; }
		C->DestroyComponent();
		++nKilled;
	}
	// Базовому телу вернуть ВСЕ секции: их гасили `AttachPart` (chst/pant/shoe) и `HideBaseHair`.
	//   Без этого снятый нагрудник оставлял торс невидимым навсегда.
	if (BaseSkel) { BaseSkel->ShowAllMaterialSections(0); }
	if (nKilled > 0)
	{
		UE_LOG(LogAsdaCharAssembly, Verbose, TEXT("ClearWornParts: снято %d частей прежней внешности"), nKilled);
	}
}

int32 UAsdaCharAssembly::ResolveWeaponClass(int32 ItemId, UAsdaItemTableSubsystem* Items)
{
	if (ItemId <= 0 || !Items) { return 99; }
	const FAsdaItemRecord* R = Items->GetItem(ItemId);
	if (!R) { return 99; }
	// Эталон подменяет класс ТОЛЬКО для удочек (10 + Variant 1) → 2. Буры (Variant 2) в ветке костей
	//   остаются классом 10 (подмена на 10 в LoadMotionAnims касается НАБОРА КЛИПОВ, а не кости).
	if (R->ItemSubType == 10 && R->ItemVariant == 1) { return 2; }
	return R->ItemSubType;
}

void UAsdaCharAssembly::WeaponAttachInfo(int32 WeaponClass, bool bStowed, FName& OutBone, FTransform& OutXf)
{
	if (!bStowed)
	{
		OutBone = (WeaponClass == 8 || WeaponClass == 10) ? FName(TEXT("_8_a1")) : FName(TEXT("8_a1"));
		OutXf = FTransform(FRotator::ZeroRotator, FVector(0.f, 46.3f, 0.f));
		return;
	}
	OutBone = FName(TEXT("2_a1"));
	// Поправка берётся из ЕДИНОГО источника (см. WeaponStowAttachRaw) — ровно та же матрица,
	//   что уезжает в AAsdaEffectActor::FollowBoneAttach ⇒ меш и его свечение живут в ОДНОМ
	//   пространстве по построению, как в эталоне.
	const FMatrix M = WeaponStowAttachRaw(WeaponClass, /*bMesh*/true);
	// Классы 0,1,3,4 (одноручное/двуручное): поправок в эталоне НЕТ — оставляем bind, как в руке.
	OutXf = M.Equals(FMatrix::Identity, 0.0001f)
		? FTransform(FRotator::ZeroRotator, FVector(0.f, 46.3f, 0.f))
		: FTransform(M);
}

// ★★ ЕДИНЫЙ ИСТОЧНИК эталонной поправки «за спиной» (подробный вывод — в шапке объявления).
//   M_game = T·R (row-vector), M_UE = Q̂⁻¹·M_game·Q̂, трансляция = 100·Q̂(t·R), Q̂(x,y,z) = (x, −z, y).
//   Проверка направления: длина оружия в .jbm идёт по локальному −Y (меч swrd001: y −7206…−695),
//   pos-канал .jeb — по game +Z; Q̂(0,0,1) = (0,−1,0) ⇒ обе оси совпадают. Поворот ниже переносит
//   эту ось ровно так же, как в эталоне (сверка: game +Z · RotY(−90°) = game −X = Q̂⁻¹(−1,0,0)).
FMatrix UAsdaCharAssembly::WeaponStowAttachRaw(int32 WeaponClass, bool bMesh)
{
	switch (WeaponClass)
	{
	// Копьё (2) / посох (5): T(34.3,−1.4,−41.7)·RotY(−90°). t·R = (41.7,−1.4,34.3) game
	//   ⇒ 100·Q̂ = (4170,−3430,−140); Q̂⁻¹·RotY(−90°)·Q̂ = поворот вокруг UE-Z на −90° (Yaw −90°).
	case 2: case 5:
		return FMatrix(FPlane(0, -1, 0, 0), FPlane(1, 0, 0, 0), FPlane(0, 0, 1, 0),
			FPlane(4170.f, -3430.f, -140.f, 1));
	// Арбалет (9): T(20,−30,−25)·RotX(−90°). t·R = (20,−25,30) game ⇒ 100·Q̂ = (2000,−3000,−2500);
	//   Q̂⁻¹·RotX(−90°)·Q̂ = поворот вокруг UE-X на +90° (Roll +90°).
	case 9:
		return FMatrix(FPlane(1, 0, 0, 0), FPlane(0, 0, -1, 0), FPlane(0, 1, 0, 0),
			FPlane(2000.f, -3000.f, -2500.f, 1));
	// Баллиста (11): T(49.7,−1.4,−18.2)·RotY(−90°). t·R = (18.2,−1.4,49.7) game
	//   ⇒ 100·Q̂ = (1820,−4970,−140); поворот тот же, что у 2/5 (Yaw −90°).
	case 11:
		return FMatrix(FPlane(0, -1, 0, 0), FPlane(1, 0, 0, 0), FPlane(0, 0, 1, 0),
			FPlane(1820.f, -4970.f, -140.f, 1));
	// Луки/арбалеты-«лук» (8,10): эталон переписывает ТОЛЬКО строку трансляции единичной матрицы —
	//   мешу (10,0,35) (dword 1092616192=10.0f, 0, 1108082688=35.0f, entity_char.c:14770-14772),
	//   свечению (0,0,35) (entity_char.c:28551-28553). Поворота нет.
	case 8: case 10:
		return FMatrix(FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 1, 0),
			FPlane(bMesh ? 1000.f : 0.f, -3500.f, 0.f, 1));
	// Классы 0,1,3,4 — в эталоне поправок нет.
	default:
		return FMatrix::Identity;
	}
}

void UAsdaCharAssembly::ApplyWeaponStow(USkeletalMeshComponent* BaseSkel, UStaticMeshComponent* WeaponComp,
	int32 WeaponClass, bool bStowed)
{
	if (!BaseSkel || !WeaponComp) { return; }
	FName Bone; FTransform Xf;
	WeaponAttachInfo(WeaponClass, bStowed, Bone, Xf);
	if (USkeletalMesh* SkMesh = BaseSkel->GetSkeletalMeshAsset())
	{
		// Кости нет (превью char-select, 19 костей) — оставляем как есть, без пересадки.
		if (SkMesh->GetRefSkeleton().FindBoneIndex(Bone) == INDEX_NONE) { return; }
	}
	WeaponComp->AttachToComponent(BaseSkel, FAttachmentTransformRules::KeepRelativeTransform, Bone);
	WeaponComp->SetRelativeTransform(Xf);
}

// ★★★ 07.11.2026 [ИСПРАВЛЕНИЕ] ★ отступление: дефект оригинала, НЕ воспроизводим. Щит за спиной (город, седло, эмоция, действие).
//   Эталон (`CCharacter_SetParts_Armor` entity_char.c:15933, `UpdateHeldItemEffect` :28879) сажает щит на наплечную кость "_2_a1", парную
//   кости оружия "2_a1", без поправки. Лицевая сторона щита — локальная +Y, верх — +Z; на "_2_a1" +Y смотрит вперёд сквозь тело, +Z вниз:
//   эмблема торчит из груди за головой, сзади видна изнанка, низ щита сверху — жалоба «щит в городе перевёрнут». Порт совпадал с эталоном
//   точно (поза костей сверена с клипами ch4_*: 0.0–0.6°). В скелете оригинала для щита есть своя кость "_2_b1": середина спины, +Y наружу,
//   +Z вверх во всех клипах покоя и движения; в клиенте её имени нет вовсе (в таблице строк только "_2_a1"). Кость — данные оригинала, поправочных
//   матриц не вводим. 1 = щит и его свечение за спиной на "_2_b1"; 0 = как эталон, "_2_a1".
static ASDA_DEV_CVAR(int32, CVarShieldStowBackBone,
	TEXT("asda.Char.ShieldStowBackBone"), 1,
	TEXT("1 (по умолчанию) = убранный щит и его свечение на кости середины спины _2_b1, лицом наружу и верхом вверх (дефект оригинала исправлен); ")
	TEXT("0 = как эталон: наплечная кость _2_a1, щит развёрнут лицом к телу и низом вверх."),
	ECVF_Default);

FName UAsdaCharAssembly::WeaponGlowBone(int32 WeaponClass, bool bStowed, bool bShield)
{
	if (bShield)
	{
		if (!bStowed) { return FName(TEXT("_7_a1")); }
		return CVarShieldStowBackBone.GetValueOnGameThread() != 0 ? FName(TEXT("_2_b1")) : FName(TEXT("_2_a1"));
	}
	if (bStowed) { return FName(TEXT("2_a1")); }
	return (WeaponClass == 8 || WeaponClass == 10) ? FName(TEXT("_8_a1")) : FName(TEXT("8_a1"));
}

FMatrix UAsdaCharAssembly::WeaponGlowAttachRaw(int32 WeaponClass, bool bStowed, bool bShield)
{
	if (!bStowed || bShield) { return FMatrix::Identity; }
	// ★ ФИКС (живой тест: «свечение бьёт назад от игрока»). Раньше здесь стояла матрица в СЫРЫХ D3D
	//   осях и в КЛИЕНТСКИХ единицах, а вставляется она в цепочку РОВНО там, где пространство —
	//   оси кости UE и сырые единицы скелета (A = FxToBone·Attach·BoneCS·Comp). Итог: поворот шёл
	//   вокруг не той оси (вместо «вдоль оружия» полотна ложились горизонтально), а перенос был
	//   в 100 раз мал. Теперь берём ту же матрицу, что и меш, — из единого источника.
	return WeaponStowAttachRaw(WeaponClass, /*bMesh*/false);
}

UStaticMeshComponent* UAsdaCharAssembly::AttachWeapon(AActor* Owner, USceneComponent* Root, USkeletalMeshComponent* BaseSkel,
	bool bMale, int32 ItemId, UAsdaItemTableSubsystem* Items)
{
	if (!Owner || !Root || !BaseSkel || !Items || ItemId <= 0) return nullptr;
	// itemId → имя меша (ch1_b_XX_*, унисекс). Эталон: gender==1 → ImgRecord+4, иначе +68 — для оружия
	//   оба указывают на один _b_-меш; bMale не критичен, берём мужской.
	const FString Mesh = Items->GetItemMeshName(ItemId, /*bMale*/true);
	if (Mesh.IsEmpty()) { UE_LOG(LogAsdaCharAssembly, Warning, TEXT("AttachWeapon: нет меша для itemId=%d"), ItemId); return nullptr; }
	UStaticMesh* WMesh = LoadObject<UStaticMesh>(nullptr, *CharStaticMeshPath(Mesh));
	if (!WMesh)
	{
		UE_LOG(LogAsdaCharAssembly, Warning, TEXT("AttachWeapon: StaticMesh оружия не импортирован (%s, itemId=%d)"), *Mesh, ItemId);
		return nullptr;
	}
	// Кость крепления M_DUMMY (эталон List_FindIndexByName(...,\"M_DUMMY\")). Есть в мир-скелете (51 кость),
	//   НЕТ в char-select превью (19 костей) — тогда молча пропускаем (в char-select оружие и не показывают).
	//   ★ В ОТЛИЧИЕ от причёски (model-space → relative=IBM), оружейный меш авторингован в ЛОКАЛЬНОМ
	//   пространстве кисти (origin = точка хвата): эталон рендерит его прямо мировой матрицей кости M_DUMMY.
	//   Значит relative к кости = IDENTITY (меш садится в origin кости = в руку и следует за ней).
	//   IBM уносил меш к инверсной позиции кости → «оружие в ногах».
	// ★ КОСТЬ ХВАТА. Эталон использует M_DUMMY, но в НАШЕМ экспортированном скелете M_DUMMY лежит у origin
	//   (замер ref-позы: CS=(-917,-397,Z=28) при росте модели 15010) — она не следует за рукой в rest-позе,
	//   поэтому статик-оружие на ней падало «в ноги». Реальная КИСТЬ — кость "8" (CS Z=8312, ниже плеча "7"
	//   Z=11988), см. equip_groundwork.md §1.2 (кости кистей 8/_8/7_b1). Крепим к "8", фолбэк — M_DUMMY.
	// ★ ЛУКИ/АРБАЛЕТЫ-«лук» (itemSubType 8/10) — ВТОРАЯ рука: эталон CCharacter_SetupModelFromItem
	//   (entity_char.c:14761-14767) для case 8/10 берёт "_8_a1", для 0..5/9/11 — "8_a1". Свечение заточки
	//   (CCharacter_UpdateHeldItemEffect, entity_char.c:28393) выбирает кость ТЕМ ЖЕ правилом — без этого
	//   меш лука и его свечение оказывались в РАЗНЫХ руках (обе кости есть в скелете: дамп .glb, 52 узла).
	// ★ Класс оружия РАЗРЕШАЕМ как эталон (удочка 10+Variant1 → 2), иначе удочка уезжала в левую руку.
	const int32 WeaponClass = ResolveWeaponClass(ItemId, Items);
	FName Bone; FTransform WeaponXf;
	WeaponAttachInfo(WeaponClass, /*bStowed=*/false, Bone, WeaponXf);
	if (USkeletalMesh* SkMesh = BaseSkel->GetSkeletalMeshAsset())
	{
		const FReferenceSkeleton& Ref = SkMesh->GetRefSkeleton();
		int32 BoneIdx = Ref.FindBoneIndex(Bone);
		if (BoneIdx == INDEX_NONE) { Bone = FName(TEXT("8_a1")); BoneIdx = Ref.FindBoneIndex(Bone); }
		if (BoneIdx == INDEX_NONE) { Bone = FName(TEXT("8")); BoneIdx = Ref.FindBoneIndex(Bone); }
		if (BoneIdx == INDEX_NONE) { Bone = FName(TEXT("M_DUMMY")); BoneIdx = Ref.FindBoneIndex(Bone); }
		if (BoneIdx == INDEX_NONE)
		{
			UE_LOG(LogAsdaCharAssembly, Log, TEXT("AttachWeapon: нет кости кисти ('8'/M_DUMMY) — оружие пропущено (превью?)"));
			return nullptr;
		}
		// ★ ДИАГ (разовый): дамп CS-позиций ВСЕХ костей — выбрать реальную кость КИСТИ (в пространстве меша,
		//   большой Z ~высота руки), т.к. M_DUMMY стоит у origin (Z~28) и для статик-оружия непригоден.
		static bool bDumpedBones = false;
		if (!bDumpedBones)
		{
			bDumpedBones = true;
			const TArray<FTransform>& Local = Ref.GetRefBonePose();
			const int32 NB = Ref.GetNum();
			for (int32 bi = 0; bi < NB; ++bi)
			{
				FTransform CS = FTransform::Identity;
				for (int32 x = bi; x != INDEX_NONE; x = Ref.GetParentIndex(x)) { CS = CS * Local[x]; }
				const FVector L = CS.GetLocation();
				UE_LOG(LogAsdaCharAssembly, Warning, TEXT("[BONE-DUMP] %02d %-12s CS=(%.0f, %.0f, %.0f)"),
					bi, *Ref.GetBoneName(bi).ToString(), L.X, L.Y, L.Z);
			}
		}
	}
	UStaticMeshComponent* WC = NewObject<UStaticMeshComponent>(Owner, TEXT("WeaponMesh"));
	WC->SetStaticMesh(WMesh);
	WC->SetCastShadow(true);
	WC->SetupAttachment(BaseSkel, Bone);
	// Метка для пересадки «в руку ↔ за спину» (AAsdaPlayerCharacter::RefreshWeaponStow).
	WC->ComponentTags.Add(WeaponMeshTag);
	// ★ И метка «часть внешности»: при смене оружия прежний меш обязан УЙТИ, иначе в руке окажутся
	//   два клинка сразу (`ClearWornParts`).
	WC->ComponentTags.Add(WornPartTag);
	// ★ ТРАНСФОРМ 1:1 (reverse: mirror_and_weapon_transform_re.md). Кость хвата = "8_a1" (эталон
	//   entity_char.c:14979 — ветки "8_a1"/"_8_a1"/"2_a1"). Меш оружия авторен ПРЯМО в локальном
	//   пространстве этой кости (его .jbb bind = единичная ротация + t=(0,0,0.463) во всех ключах),
	//   поэтому поворот = Identity, а смещение = +46.3 по Y (для текущего пайплайна мешей).
	WC->SetRelativeTransform(WeaponXf);
	WC->RegisterComponent();
	// ★ ЭТАЛОННАЯ текстура оружия = item_img_table.TexMale (в .jbm зашита ЧУЖАЯ: напр. swrd003→swrd002).
	ApplyItemTexture(WC, Items->GetItemTexName(ItemId, /*bMale*/true));
	UE_LOG(LogAsdaCharAssembly, Log, TEXT("AttachWeapon: '%s' (itemId=%d) на кость %s, tex=%s"),
		*Mesh, ItemId, *Bone.ToString(), *Items->GetItemTexName(ItemId, true));
	return WC;
}

// ═══ ★★★ 25.10.2026 [ОРИГИНАЛ] МЕШ ЩИТА — разбор эталона у объявления `AttachShield` в .h ═══
const FName UAsdaCharAssembly::ShieldMeshTag(TEXT("AsdaShieldMesh"));

// Ключ отката: 1 — меш щита как в эталоне; 0 — прежний порт (щит только источником свечения заточки).
static ASDA_DEV_CVAR(int32, CVarShieldMesh,
	TEXT("asda.Char.ShieldMesh"), 1,
	TEXT("1 = как эталон (CCharacter_SetParts_Armor): надетый щит рисуется мешем на кости _7_a1 (убран — _2_a1); "
	     "0 = прежнее поведение порта: меша щита нет, только свечение заточки."),
	ECVF_Default);

bool UAsdaCharAssembly::ShieldMeshOn()
{
	return CVarShieldMesh.GetValueOnGameThread() != 0;
}

FName UAsdaCharAssembly::ShieldAttachBone(bool bStowed)
{
	// Эталон entity_char.c:15933-15935: `gap_A42[15160] ? "_2_a1" : "_7_a1"`; за спиной — исправление `asda.Char.ShieldStowBackBone` (см. `WeaponGlowBone`). Общий источник с костью свечения щита.
	return WeaponGlowBone(/*WeaponClass*/0, bStowed, /*bShield*/true);
}

void UAsdaCharAssembly::ApplyShieldStow(USkeletalMeshComponent* BaseSkel, UStaticMeshComponent* ShieldComp, bool bStowed)
{
	if (!BaseSkel || !ShieldComp) { return; }
	const FName Bone = ShieldAttachBone(bStowed);
	if (BaseSkel->GetBoneIndex(Bone) == INDEX_NONE) { return; }   // кости нет — оставляем как есть
	if (ShieldComp->GetAttachParent() == BaseSkel && ShieldComp->GetAttachSocketName() == Bone) { return; }
	ShieldComp->AttachToComponent(BaseSkel, FAttachmentTransformRules::KeepRelativeTransform, Bone);
	// У щита поправок «за спиной» нет ни в эталоне, ни на кости "_2_b1": локальная матрица части не трогается.
	ShieldComp->SetRelativeTransform(FTransform::Identity);
}

UStaticMeshComponent* UAsdaCharAssembly::AttachShield(AActor* Owner, USceneComponent* Root, USkeletalMeshComponent* BaseSkel,
	bool bMale, int32 ItemId, UAsdaItemTableSubsystem* Items)
{
	if (!Owner || !Root || !BaseSkel || !Items || ItemId <= 0) { return nullptr; }
	if (!ShieldMeshOn()) { return nullptr; }   // прежний порт: меша щита нет
	// Эталон: `gender == 1` → ImgRecord+4 (меш) / +36 (текстура), иначе +68 / +100 — у всех 787 щитов обе записи совпадают,
	//   но берём по полу, как эталон.
	const FString Mesh = Items->GetItemMeshName(ItemId, bMale);
	if (Mesh.IsEmpty())
	{
		// Эталон: путь "Model\.jbm" не грузится → `return 0`, щита на модели нет (служебный код 28045).
		UE_LOG(LogAsdaCharAssembly, Log, TEXT("AttachShield: у щита itemId=%d нет имени меша — как в эталоне, модели нет"), ItemId);
		return nullptr;
	}
	UStaticMesh* SMesh = LoadObject<UStaticMesh>(nullptr, *CharStaticMeshPath(Mesh));
	if (!SMesh)
	{
		// Эталон: `modelGetOrLoadMeshFromCache` не нашёл .jbm в Character\Model → `return 0` (коды 23315-23317 ведут на
		//   модель предмета на земле itm1_n_98_othr058_00, которой в моделях персонажа нет).
		UE_LOG(LogAsdaCharAssembly, Warning, TEXT("AttachShield: меш щита не импортирован (%s, itemId=%d) — модели нет"), *Mesh, ItemId);
		return nullptr;
	}
	// Кость: при создании — «в руке» (`_7_a1`); пересадку за спину делает персонаж (`RefreshWeaponStow`) тем же флагом,
	//   что и оружие. Нет кости (превью выбора персонажа, 19 костей) — эталон в этот конвейер щит и не отдаёт.
	const FName Bone = ShieldAttachBone(/*bStowed*/false);
	if (BaseSkel->GetBoneIndex(Bone) == INDEX_NONE)
	{
		UE_LOG(LogAsdaCharAssembly, Log, TEXT("AttachShield: нет кости %s — щит пропущен (превью?)"), *Bone.ToString());
		return nullptr;
	}
	UStaticMeshComponent* SC = NewObject<UStaticMeshComponent>(Owner, TEXT("ShieldMesh"));
	SC->SetStaticMesh(SMesh);
	SC->SetCastShadow(true);
	SC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SC->SetupAttachment(BaseSkel, Bone);
	// Метки: «щит» (пересадка/рыбалка) и «часть внешности» (`ClearWornParts` снимает при пересборке — без неё снятый щит
	//   оставался бы на руке, а смена щита давала бы два).
	SC->ComponentTags.Add(ShieldMeshTag);
	SC->ComponentTags.Add(WornPartTag);
	// bind .jbb щита — единичный ⇒ меш садится в начало кости без смещения и поворота.
	SC->SetRelativeTransform(FTransform::Identity);
	SC->RegisterComponent();
	// Эталонная текстура = item_img_table TexMale/TexFemale на все подсетки (в .jbm бывает чужая: shid002 ⇒ _2.png).
	const FString Tex = Items->GetItemTexName(ItemId, bMale);
	ApplyItemTexture(SC, Tex);
	UE_LOG(LogAsdaCharAssembly, Log, TEXT("AttachShield: '%s' (itemId=%d) на кость %s, tex=%s"), *Mesh, ItemId, *Bone.ToString(), *Tex);
	return SC;
}

void UAsdaCharAssembly::HideBaseHair(USkeletalMeshComponent* BaseSkel)
{
	if (!BaseSkel) return;
	// ★ 2026-07-31: секция гасится ШТАТНО (ShowMaterialSection), а НЕ прозрачной текстурой.
	//   Причина: материалы семейства персонажа переведены на непрозрачный M_AsdaCharLit — альфа текстуры
	//   в оригинале НЕ прозрачность (EmptyProject.fx, tSkin/tRigid → PShade: Output.RGBColor.w = 1*fog*fade,
	//   Tex.w идёт только в блик/кубмап). Прежний трюк с T_Transparent на непрозрачном мастере не сработал бы.
	const int32 NumMats = BaseSkel->GetNumMaterials();
	int32 nHidden = 0;
	for (int32 mi = 0; mi < NumMats; ++mi)
	{
		UMaterialInterface* M = BaseSkel->GetMaterial(mi);
		if (M && M->GetName().Contains(TEXT("hair")))
		{
			BaseSkel->ShowMaterialSection(mi, mi, false, 0);
			++nHidden;
		}
	}
	if (nHidden == 0)
	{
		UE_LOG(LogAsdaCharAssembly, Verbose, TEXT("HideBaseHair: секция 'hair' в базовом меше не найдена"));
	}
}

int32 UAsdaCharAssembly::BuildWornParts(AActor* Owner, USceneComponent* Root, USkeletalMeshComponent* BaseSkel,
	bool bMale, const FAsdaWornParts& Parts, UAsdaItemTableSubsystem* Items)
{
	if (!Owner || !Root || !BaseSkel || !Items) return 0;

	// ★★★ СНАЧАЛА РАЗДЕТЬ. Сборка создаёт НОВЫЕ компоненты и гасит секции базы — без этого шага
	//   повторный вызов (смена экипировки: 0xFB5/0xFB7 у чужих, локальное применение 5001 у себя)
	//   накладывал бы новую вещь ПОВЕРХ старой и навсегда оставлял скрытой базовую секцию снятой.
	//   При первом вызове (спавн) — пустая операция. Подробности — у объявления `ClearWornParts`.
	ClearWornParts(Owner, BaseSkel);

	// Лицо (эталон грузит type 1 в playerCharObjectLoadColored наравне с частями): свап текстуры секции face001.
	ApplyFace(BaseSkel, bMale, Parts.FaceIndex);
	// Волосы применяем ТОЛЬКО когда причёска задана (HairStyle>=0, напр. char-create). Тогда база превью несёт
	//   ВШИТУЮ hair001 (артефакт экспорта) → гасим её секцию (иначе двойные волосы) и ставим выбранную.
	//   HairStyle<0 (char-select без явной причёски) → базовую вшитую причёску НЕ трогаем (иначе перс лысеет).
	// ★★★ ВОЛОСЫ ПОД ШЛЕМОМ. Режим берётся из ЗАПИСИ НАДЕТОГО ШЛЕМА (см. `ResolveHelmetHairMode`).
	//   Точка ОДНА на всех персонажей: `BuildWornParts` собирает и своего, и чужих (пакеты 0xFB5/
	//   0xFB7 и спавн соседей идут сюда же), поэтому правило работает на всём экране, а не на себе.
	// ★ 25.10.2026 [ОРИГИНАЛ]: при снятом флаге показа шлема рисуется ветка «шлема нет» (`EffectiveHelmetId`).
	const int32 HelmetDrawn = EffectiveHelmetId(Parts);
	const int32 HelmetHairMode = ResolveHelmetHairMode(HelmetDrawn, bMale, Items);
	if (Parts.HairStyle >= 0)
	{
		// Базовую вшитую причёску гасим ВСЕГДА, когда причёска задана: при режиме 2 иначе из-под
		//   шлема торчала бы именно она.
		HideBaseHair(BaseSkel);
		if (HelmetHairMode != 2)
		{
			ApplyHair(Owner, Root, BaseSkel, bMale, Parts.HairStyle, Parts.HairColor, Items,
				/*bUnderHelmet*/ HelmetHairMode == 1);
		}
		else
		{
			UE_LOG(LogAsdaCharAssembly, Verbose,
				TEXT("Волосы скрыты: шлем %d имеет режим 2 (SubType%s)"),
				HelmetDrawn, bMale ? TEXT("Male") : TEXT("Female"));
		}
	}
	else if (HelmetHairMode == 2)
	{
		// Причёска не задана (превью выбора персонажа), но шлем закрывает голову целиком —
		//   гасим ВШИТУЮ секцию волос базового меша, иначе она торчит сквозь шлем.
		HideBaseHair(BaseSkel);
	}

	// ★ Эталон playerCharObjectLoadColored@0xF0A2B0: грузятся РОВНО носимые типы, БЕЗ оружия/причёски.
	//   База = «голое тело» (chst001 несёт руки, pant001 — ноги, shoe001 — ступни). Надетый предмет
	//   каждого типа ЗАМЕНЯЕТ базовую секцию; не надетый — базовая секция остаётся видимой.
	const struct FWear { int32 ItemId; const TCHAR* Base; } Wear[] = {
		{ Parts.Chest,  TEXT("chst") },   // тело → скрыть базовый chst
		{ Parts.Pants,  TEXT("pant") },   // штаны → скрыть базовый pant
		{ Parts.Shoes,  TEXT("shoe") },   // обувь → скрыть базовый shoe
		{ Parts.Gloves, nullptr       },  // перчатки → оверлей
		{ HelmetDrawn,  nullptr       },  // шлем → оверлей (0 при снятом флаге показа — `EffectiveHelmetId`)
	};

	int32 nParts = 0;
	for (const FWear& W : Wear)
	{
		if (AttachPart(Owner, Root, BaseSkel, bMale, W.ItemId, W.Base, Items)) { ++nParts; }
	}

	// ★ КИСТИ РУК. В базовом теле секции кистей НЕТ (слоты превью: chst/pant/shoe/face/hair),
	//   кисти приходят оверлеем grov105 — «стоковые перчатки». AttachPart выходит на ItemId <= 0,
	//   поэтому при пустом слоте перчаток персонаж оставался без кистей.
	//   Подставляем базовый меш напрямую, минуя item-таблицу (у него нет itemId).
	if (Parts.Gloves <= 0)
	{
		//   Имя базовой части — общая точка `DefaultHandsMeshName` (там же — почему grov001, а не grov105).
		const FString DefGloves = DefaultHandsMeshName(bMale);
		if (USkeletalMesh* HandsMesh = LoadEquipPartMesh(DefGloves))
		{
			USkeletalMeshComponent* PC = NewObject<USkeletalMeshComponent>(
				Owner, *(FString(TEXT("Equip_")) + DefGloves));
			PC->SetupAttachment(Root);
			PC->SetSkeletalMesh(HandsMesh);
			PC->SetRelativeTransform(FTransform::Identity);
			PC->SetCastShadow(true);
			AsdaShadow::ApplyCapsuleReceiver(PC, BaseSkel);   // ★ 26.10.2026 [ИСПРАВЛЕНИЕ]: стоковые кисти — тоже приёмник (см. AttachPart)
			PC->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			// ★ Тот же тег: стоковые кисти тоже снимаются при пересборке (иначе надетые перчатки
			//   надеваются ПОВЕРХ голых кистей — z-fighting на пальцах).
			PC->ComponentTags.Add(WornPartTag);
			PC->RegisterComponent();
			PC->SetLeaderPoseComponent(BaseSkel);
			++nParts;
		}
		else
		{
			UE_LOG(LogAsdaCharAssembly, Warning, TEXT("Базовые кисти (%s) не найдены"), *DefGloves);
		}
	}

	// ★ ОРУЖИЕ В РУКЕ (эталон CCharacter_SetupModelFromItem@0x9E97A0): StaticMesh на кости M_DUMMY.
	//   В char-select база превью НЕ несёт M_DUMMY (19-костный скелет), в мире — несёт (51-костный);
	//   AttachWeapon сам проверит наличие кости и мягко пропустит, если её нет.
	if (Parts.Weapon > 0)
	{
		if (AttachWeapon(Owner, Root, BaseSkel, bMale, Parts.Weapon, Items)) { ++nParts; }
	}
	// ★ 25.10.2026 [ОРИГИНАЛ] ЩИТ (эталон CCharacter_SetParts_Armor@0x9EB3C0, экип-слот 8): жёсткий меш на кости "_7_a1".
	//   От оружия не зависит (эталон грузит его своей веткой и при любом оружии, и без оружия).
	if (Parts.Shield > 0)
	{
		if (AttachShield(Owner, Root, BaseSkel, bMale, Parts.Shield, Items)) { ++nParts; }
	}
	return nParts;
}

void UAsdaCharAssembly::CollectWornPartPaths(bool bMale, const FAsdaWornParts& Parts, UAsdaItemTableSubsystem* Items, TArray<FString>& Out)
{
	auto AddPart = [&Out](const FString& MeshName)
	{
		if (!MeshName.IsEmpty()) { Out.AddUnique(EquipPartMeshPath(MeshName) + TEXT(".") + MeshName); }
	};
	auto AddTex = [&Out](const FString& TexName)
	{
		if (!TexName.IsEmpty()) { Out.AddUnique(CharTexturePath(TexName)); }
	};

	// Лицо — `ApplyFace` (FaceIndex < 0 — не трогаем).
	if (Parts.FaceIndex >= 0) { Out.AddUnique(FaceTexturePath(bMale, Parts.FaceIndex)); }

	// Причёска — ветви `BuildWornParts` → `ApplyHair`: режим 2 шлема волос не ставит, режим 1 у женщин
	//   подменяет меш «подшлемным»; цвет-текстура — по формуле эталона.
	const int32 HelmetDrawn = EffectiveHelmetId(Parts);   // ★ 25.10.2026: та же ветка, что у сборки
	const int32 HelmetHairMode = ResolveHelmetHairMode(HelmetDrawn, bMale, Items);
	if (Parts.HairStyle >= 0 && HelmetHairMode != 2)
	{
		const FString Mesh = (HelmetHairMode == 1 && !bMale)
			? FString(kUnderHelmetHairMeshF)
			: ResolveHairMeshName(bMale, Parts.HairStyle, Parts.HairColor, Items);
		if (!Mesh.IsEmpty())
		{
			Out.AddUnique(CharStaticMeshPath(Mesh));
			AddTex(ResolveHairTextureName(bMale, Parts.HairStyle, Parts.HairColor));
		}
	}

	// Носимые части — `AttachPart`: меш по полу + эталонная текстура предмета (item_img_table).
	if (Items)
	{
		for (const int32 ItemId : { Parts.Chest, Parts.Pants, Parts.Shoes, Parts.Gloves, HelmetDrawn })
		{
			if (ItemId <= 0) { continue; }
			const FString Mesh = Items->GetItemMeshName(ItemId, bMale);
			if (Mesh.IsEmpty()) { continue; }
			AddPart(Mesh);
			AddTex(Items->GetItemTexName(ItemId, bMale));
		}
		// Оружие — `AttachWeapon` (меш и текстура всегда по мужской записи).
		if (Parts.Weapon > 0)
		{
			const FString Mesh = Items->GetItemMeshName(Parts.Weapon, /*bMale*/true);
			if (!Mesh.IsEmpty())
			{
				Out.AddUnique(CharStaticMeshPath(Mesh));
				AddTex(Items->GetItemTexName(Parts.Weapon, /*bMale*/true));
			}
		}
		// ★ 25.10.2026: щит — `AttachShield` (меш и текстура по полу; при выключенном ключе сборка его не ставит).
		if (Parts.Shield > 0 && ShieldMeshOn())
		{
			const FString Mesh = Items->GetItemMeshName(Parts.Shield, bMale);
			if (!Mesh.IsEmpty())
			{
				Out.AddUnique(CharStaticMeshPath(Mesh));
				AddTex(Items->GetItemTexName(Parts.Shield, bMale));
			}
		}
	}

	// Стоковые кисти — когда перчаток нет.
	if (Parts.Gloves <= 0) { AddPart(DefaultHandsMeshName(bMale)); }
}
