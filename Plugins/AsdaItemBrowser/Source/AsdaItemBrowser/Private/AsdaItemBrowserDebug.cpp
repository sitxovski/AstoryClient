// Copyright (c) 2026 @sitxovski (Discord)
// Реализация headless-самопроверки — см. AsdaItemBrowserDebug.h.
#include "AsdaItemBrowserDebug.h"
#include "AsdaItemBrowserData.h"
#include "SAsdaItemPreviewViewport.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "PreviewScene.h"
#include "Engine/World.h"
#include "AsdaEffectActor.h"
#include "ProceduralMeshComponent.h"
#include "Login/AsdaText.h"   // FAsdaText::Loc — подписи тултипа из DT_GameText
#include "HAL/IConsoleManager.h"   // ★ Ф15 (К-1): проверка гейта asda.Fx.Glow.EmittersWriter
#include "UObject/UnrealType.h"    // ★ Ф15 (К-1): FArrayProperty::Inner — свойство узла ЭЛЕМЕНТА массива
#include "Editor.h"                // ★ Ф16 (СРЕДН-1): GEditor->PlayWorld — признак «идёт симуляция»
#include "ScopedTransaction.h"     // ★ Ф17 (С-1): A/B «уборка сирот попадает в буфер отмены»
#include "Editor/TransBuffer.h"    // ★ Ф17 (С-1): в коммандлете буфера отмены нет — заводим временный
#include "HAL/PlatformProcess.h"   // ★ Ф20 (С-1): FPlatformProcess::Sleep — опыт ЖДЁТ время по-настоящему
#include "HAL/PlatformTime.h"      // ★ Ф20 (С-1): FPlatformTime::Seconds — те же часы, что и у продукта
#include "SAsdaItemBrowser.h"              // ★ 29.10.2026: кадр переделанного просмотрщика
#include "UI/AsdaItemTipStandalone.h"      // ★ 29.10.2026: карточка — игровым построителем подсказки
#include "UI/AsdaMarkup.h"                 // ★ 29.10.2026: снять теги подсказки для текстового дампа
#include "Slate/WidgetRenderer.h"          // ★ 29.10.2026: виджет → render target без окна
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "ImageUtils.h"                    // FImageUtils::SaveImageByExtension — PNG кадра
#include "ImageCore.h"                     // FImageView
#include "Framework/Application/SlateApplication.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaItemBrowserTest, Log, All);

static const TCHAR* MeshKindStr(EAsdaBrowserMeshKind K)
{
	switch (K)
	{
	case EAsdaBrowserMeshKind::EquipSkeletal: return TEXT("SkeletalMesh EquipParts");
	case EAsdaBrowserMeshKind::CharStatic:    return TEXT("StaticMesh Char");
	case EAsdaBrowserMeshKind::ItemStatic:    return TEXT("StaticMesh Items");
	case EAsdaBrowserMeshKind::Missing:       return TEXT("АССЕТА НЕТ");
	default:                                  return TEXT("имени модели нет");
	}
}

FString UAsdaItemBrowserDebug::DumpTree()
{
	TSharedRef<FAsdaItemBrowserData> D = FAsdaItemBrowserData::Get();
	if (!D->EnsureLoaded())
	{
		const FString E = FString::Printf(TEXT("[браузер-тест] данные не загружены: %s"), *D->GetLoadError());
		UE_LOG(LogAsdaItemBrowserTest, Error, TEXT("%s"), *E);
		return E;
	}
	TArray<TSharedPtr<FAsdaBrowserNode>> Roots;
	D->BuildTree(FString(), Roots);

	// ★ Дерево теперь трёхуровневое: пространство кодов → категория → подкатегория.
	FString S = FString::Printf(
		TEXT("[браузер-тест] записей %d (item_list %d + sowell_list %d + special_itemlist %d), корней %d\n"),
		D->Num(),
		D->NumInSpace(EAsdaItemSpace::ItemList),
		D->NumInSpace(EAsdaItemSpace::SowelList),
		D->NumInSpace(EAsdaItemSpace::SpecialList),
		Roots.Num());
	int32 Total = 0;
	for (const TSharedPtr<FAsdaBrowserNode>& R : Roots)
	{
		Total += R->Count;
		S += FString::Printf(TEXT("  %s\n"), *R->Label);
		for (const TSharedPtr<FAsdaBrowserNode>& C : R->Children)
		{
			if (C->Kind == EAsdaBrowserNodeKind::Item) { continue; }
			S += FString::Printf(TEXT("      %s\n"), *C->Label);
			for (const TSharedPtr<FAsdaBrowserNode>& G : C->Children)
			{
				if (G->Kind == EAsdaBrowserNodeKind::SubCategory)
				{
					S += FString::Printf(TEXT("          %s\n"), *G->Label);
				}
			}
		}
	}
	S += FString::Printf(TEXT("  ИТОГО записей в дереве: %d\n"), Total);
	UE_LOG(LogAsdaItemBrowserTest, Display, TEXT("%s"), *S);
	return S;
}

FString UAsdaItemBrowserDebug::DumpSearch(const FString& Filter, int32 Space, int32 MaxItemsPerBranch)
{
	TSharedRef<FAsdaItemBrowserData> D = FAsdaItemBrowserData::Get();
	if (!D->EnsureLoaded())
	{
		return FString::Printf(TEXT("[браузер-тест] данные не загружены: %s"), *D->GetLoadError());
	}
	TOptional<EAsdaItemSpace> SpaceFilter;
	if (Space >= 0 && Space < (int32)EAsdaItemSpace::Count) { SpaceFilter = (EAsdaItemSpace)Space; }

	TArray<TSharedPtr<FAsdaBrowserNode>> Roots;
	D->BuildTree(Filter, Roots, SpaceFilter);

	FString S = FString::Printf(TEXT("[браузер-поиск] «%s», пространство %s → корней %d\n"),
		*Filter, SpaceFilter.IsSet() ? AsdaSpaceTable(SpaceFilter.GetValue()) : TEXT("все три"), Roots.Num());

	// Рекурсивный обход: у предметов печатаем пометку пространства, код и имя (или «имени нет»).
	TFunction<void(const TSharedPtr<FAsdaBrowserNode>&, int32)> Walk =
		[&S, &D, MaxItemsPerBranch, &Walk](const TSharedPtr<FAsdaBrowserNode>& N, int32 Depth)
	{
		const FString Pad = FString::ChrN(Depth * 4, TEXT(' '));
		if (N->Kind == EAsdaBrowserNodeKind::Item)
		{
			const FString Name = D->GetName(N->ItemIndex);
			S += FString::Printf(TEXT("%s[%s] %d : %s\n"), *Pad, AsdaSpaceTag(N->Space),
				D->GetCode(N->ItemIndex), Name.IsEmpty() ? TEXT("(имени нет в данных)") : *Name);
			return;
		}
		S += FString::Printf(TEXT("%s%s\n"), *Pad, *N->Label);
		int32 Printed = 0;
		for (const TSharedPtr<FAsdaBrowserNode>& C : N->Children)
		{
			if (C->Kind == EAsdaBrowserNodeKind::Item && MaxItemsPerBranch > 0 && Printed >= MaxItemsPerBranch)
			{
				S += FString::Printf(TEXT("%s    … ещё %d\n"), *Pad, N->Children.Num() - Printed);
				break;
			}
			if (C->Kind == EAsdaBrowserNodeKind::Item) { ++Printed; }
			Walk(C, Depth + 1);
		}
	};
	for (const TSharedPtr<FAsdaBrowserNode>& R : Roots) { Walk(R, 1); }

	UE_LOG(LogAsdaItemBrowserTest, Display, TEXT("%s"), *S);
	return S;
}

FString UAsdaItemBrowserDebug::DumpItems(const TArray<int32>& ItemCodes, int32 Enhance, bool bMale)
{
	TSharedRef<FAsdaItemBrowserData> D = FAsdaItemBrowserData::Get();
	if (!D->EnsureLoaded())
	{
		const FString E = FString::Printf(TEXT("[браузер-тест] данные не загружены: %s"), *D->GetLoadError());
		UE_LOG(LogAsdaItemBrowserTest, Error, TEXT("%s"), *E);
		return E;
	}
	FString S;
	for (int32 Code : ItemCodes)
	{
		const int32 Idx = D->IndexByCode(Code);
		if (Idx == INDEX_NONE)
		{
			S += FString::Printf(TEXT("id=%d — записи нет\n"), Code);
			continue;
		}
		const FAsdaBrowserItemAssets A = D->ResolveAssets(Idx, bMale, Enhance);
		S += FString::Printf(TEXT("id=%d [%s / %s] «%s» тип=%d/%d редк=%d\n"),
			A.ItemCode, AsdaSpaceTag(A.Space), AsdaSpaceTable(A.Space),
			A.bHasName ? *A.Name : TEXT("ИМЕНИ НЕТ В ДАННЫХ"), A.ItemType, A.ItemSubType, A.ItemGrade);
		S += FString::Printf(TEXT("   модель: '%s' → %s %s\n"), *A.MeshName, MeshKindStr(A.MeshKind), *A.MeshObjectPath);
		S += FString::Printf(TEXT("   текстура: '%s' → %s\n"), *A.TexName,
			A.TexObjectPath.IsEmpty() ? TEXT("НЕТ АССЕТА") : *A.TexObjectPath);
		S += FString::Printf(TEXT("   иконка: атлас '%s' ячейка %d (%dpx/%dкол) → %s\n"),
			*A.Icon.Atlas, A.Icon.Cell, A.Icon.CellSize, A.Icon.Cols,
			A.Icon.AtlasObjectPath.IsEmpty() ? TEXT("НЕТ АССЕТА") : *A.Icon.AtlasObjectPath);
		S += FString::Printf(TEXT("   эффект(+%d): '%s' → %s\n"), Enhance,
			A.GlowJebName.IsEmpty() ? TEXT("нет") : *A.GlowJebName,
			A.GlowBpObjectPath.IsEmpty() ? TEXT("BP нет") : *A.GlowBpObjectPath);
		// Текстовая часть карточки (порядок секций тултипа эталона): подписи из DT_GameText.
		// ★ Эти msg-id относятся к тултипу ПРЕДМЕТА item_list; у самоцвета и спец-предмета своя
		//   ветка построения тултипа, поэтому подписи по ItemType/ItemGrade для них не печатаем.
		if (A.Space == EAsdaItemSpace::ItemList)
		{
			S += FString::Printf(TEXT("   msg2297='%s' msg1420='%s' грейд='%s' msg2330='%s' msg2331='%s' msg9564='%s' подтип='%s'\n"),
				*FAsdaText::Loc(2297), *FAsdaText::Loc(1420), *FAsdaText::Loc(1421 + A.ItemGrade),
				*FAsdaText::Loc(2330), *FAsdaText::Loc(2331), *FAsdaText::Loc(9564),
				*FAsdaText::Loc(8552 + A.ItemSubType));
			S += FString::Printf(TEXT("   уровень: %d..%d  цена: %d  заточка/сокеты: %d  торговля: %d\n"),
				A.Row ? A.Row->RequiredLevel : -1, A.Row ? A.Row->RequiredLevelMax : -1,
				A.Row ? A.Row->Price : -1, A.Row ? A.Row->EnchantOrSocket : -1, A.Row ? A.Row->TradableFlag : -1);
		}
		else if (A.SowelRow)
		{
			S += FString::Printf(TEXT("   бонус-стат=%d величина=%d грейд детали=%d гнездо=%d/%d уровень=%d цена=%d/%d\n"),
				A.SowelRow->BonusStatType, A.SowelRow->BonusValue, A.SowelRow->PartGrade,
				A.SowelRow->SocketTargetType, A.SowelRow->SocketTargetType2,
				A.SowelRow->RequiredLevel, A.SowelRow->SellPrice, A.SowelRow->BuyPrice);
		}
		else if (A.SpecialRow)
		{
			S += FString::Printf(TEXT("   функция(SubType)=%d ResultModel=%d Field88=%d Field92=%d кулдаун=%d цена=%d\n"),
				A.SpecialRow->SubType, A.SpecialRow->ResultModel, A.SpecialRow->Field88,
				A.SpecialRow->Field92, A.SpecialRow->CooldownBase, A.SpecialRow->Price);
			S += FString::Printf(TEXT("   иконка баффа: '%s' ячейка %d (+116=%d) → %s\n"),
				*A.BuffIconAtlas, A.BuffIconCell, A.BuffFlag116,
				A.BuffIconObjectPath.IsEmpty() ? TEXT("НЕТ АССЕТА") : *A.BuffIconObjectPath);
		}
		S += FString::Printf(TEXT("   описание (%s): %s\n"), *A.DescriptionTable,
			A.Description.IsEmpty() ? TEXT("(записи нет)") : *A.Description);
	}
	UE_LOG(LogAsdaItemBrowserTest, Display, TEXT("%s"), *S);
	return S;
}

FString UAsdaItemBrowserDebug::AuditAll(int32 MaxItems)
{
	TSharedRef<FAsdaItemBrowserData> D = FAsdaItemBrowserData::Get();
	if (!D->EnsureLoaded())
	{
		return FString::Printf(TEXT("[браузер-тест] данные не загружены: %s"), *D->GetLoadError());
	}
	const int32 N = (MaxItems > 0) ? FMath::Min(MaxItems, D->Num()) : D->Num();
	int32 MeshEquip = 0, MeshChar = 0, MeshItem = 0, MeshMissing = 0, MeshNone = 0;
	int32 TexOk = 0, TexNoName = 0, TexMissing = 0;
	int32 IconOk = 0, IconNoName = 0, IconMissing = 0;
	int32 GlowNamed = 0, GlowBpOk = 0;
	// Записи без имени в данных — по пространствам (честный остаток, ничего не выдумываем).
	int32 NoName[(int32)EAsdaItemSpace::Count] = { 0 };
	int32 InSpace[(int32)EAsdaItemSpace::Count] = { 0 };
	for (int32 i = 0; i < N; ++i)
	{
		const FAsdaBrowserItemAssets A = D->ResolveAssets(i, /*bMale*/true, /*Enhance*/7);
		++InSpace[(int32)A.Space];
		if (!A.bHasName) { ++NoName[(int32)A.Space]; }
		switch (A.MeshKind)
		{
		case EAsdaBrowserMeshKind::EquipSkeletal: ++MeshEquip; break;
		case EAsdaBrowserMeshKind::CharStatic:    ++MeshChar; break;
		case EAsdaBrowserMeshKind::ItemStatic:    ++MeshItem; break;
		case EAsdaBrowserMeshKind::Missing:       ++MeshMissing; break;
		default:                                  ++MeshNone; break;
		}
		if (A.TexName.IsEmpty()) { ++TexNoName; }
		else if (A.TexObjectPath.IsEmpty()) { ++TexMissing; }
		else { ++TexOk; }

		if (A.Icon.Atlas.IsEmpty()) { ++IconNoName; }
		else if (A.Icon.AtlasObjectPath.IsEmpty()) { ++IconMissing; }
		else { ++IconOk; }

		if (!A.GlowJebName.IsEmpty())
		{
			++GlowNamed;
			if (!A.GlowBpObjectPath.IsEmpty()) { ++GlowBpOk; }
		}
	}
	const FString S = FString::Printf(
		TEXT("[браузер-аудит] проверено %d (item_list %d, sowell_list %d, special_itemlist %d)\n")
		TEXT("  без имени в данных: item_list %d, sowell_list %d, special_itemlist %d\n")
		TEXT("  модели: equip-скелет %d, char-статик %d, item-статик %d, АССЕТА НЕТ %d, имени нет %d\n")
		TEXT("  текстуры: ассет есть %d, имени нет %d, АССЕТА НЕТ %d\n")
		TEXT("  иконки: атлас есть %d, атласа в данных нет %d, АССЕТА НЕТ %d\n")
		TEXT("  свечение +7: имя строится у %d, BP найден у %d\n"),
		N, InSpace[0], InSpace[1], InSpace[2], NoName[0], NoName[1], NoName[2],
		MeshEquip, MeshChar, MeshItem, MeshMissing, MeshNone,
		TexOk, TexNoName, TexMissing, IconOk, IconNoName, IconMissing, GlowNamed, GlowBpOk);
	UE_LOG(LogAsdaItemBrowserTest, Display, TEXT("%s"), *S);
	return S;
}

FString UAsdaItemBrowserDebug::DumpOrientation(const TArray<int32>& ItemCodes, bool bMale)
{
	TSharedRef<FAsdaItemBrowserData> D = FAsdaItemBrowserData::Get();
	if (!D->EnsureLoaded())
	{
		return FString::Printf(TEXT("[браузер-тест] данные не загружены: %s"), *D->GetLoadError());
	}
	FString S;
	for (int32 Code : ItemCodes)
	{
		const int32 Idx = D->IndexByCode(Code);
		if (Idx == INDEX_NONE) { S += FString::Printf(TEXT("id=%d — записи нет\n"), Code); continue; }
		const FAsdaBrowserItemAssets A = D->ResolveAssets(Idx, bMale, 0);
		FBox Local(ForceInit);
		if (A.MeshKind == EAsdaBrowserMeshKind::EquipSkeletal)
		{
			if (USkeletalMesh* M = LoadObject<USkeletalMesh>(nullptr, *A.MeshObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Local = M->GetBounds().GetBox();
			}
		}
		else if (A.MeshKind == EAsdaBrowserMeshKind::CharStatic || A.MeshKind == EAsdaBrowserMeshKind::ItemStatic)
		{
			if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *A.MeshObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Local = M->GetBoundingBox();
			}
		}
		if (!Local.IsValid)
		{
			S += FString::Printf(TEXT("id=%d «%s» — меша нет, ориентировать нечего\n"), A.ItemCode, *A.Name);
			continue;
		}
		const bool bWeaponOfs = (A.ItemType == 9 && A.MeshKind == EAsdaBrowserMeshKind::CharStatic);
		const bool bAllow = (A.ItemType == 9 || A.ItemType == 8);   // тот же гейт, что в панели
		const FTransform MeshLocal = SAsdaItemPreviewViewport::MakeMeshLocalTransform(bWeaponOfs);
		const FQuat Q = bAllow ? SAsdaItemPreviewViewport::ComputeAlignRotation(Local, MeshLocal) : FQuat::Identity;
		const FVector Ext = Local.GetExtent();
		const FVector C = Local.GetCenter();
		S += FString::Printf(TEXT("id=%d «%s» тип=%d/%d меш='%s'\n"), A.ItemCode, *A.Name, A.ItemType, A.ItemSubType, *A.MeshName);
		S += FString::Printf(TEXT("   габариты(лок) X=%.0f Y=%.0f Z=%.0f  центр X=%.0f Y=%.0f Z=%.0f  доворот=%s\n"),
			Ext.X * 2, Ext.Y * 2, Ext.Z * 2, C.X, C.Y, C.Z, *Q.Rotator().ToCompactString());
	}
	UE_LOG(LogAsdaItemBrowserTest, Display, TEXT("%s"), *S);
	return S;
}

FString UAsdaItemBrowserDebug::TestFxSpawn(int32 ItemCode, int32 Enhance)
{
	TSharedRef<FAsdaItemBrowserData> D = FAsdaItemBrowserData::Get();
	if (!D->EnsureLoaded())
	{
		return FString::Printf(TEXT("[браузер-тест] данные не загружены: %s"), *D->GetLoadError());
	}
	const int32 Idx = D->IndexByCode(ItemCode);
	if (Idx == INDEX_NONE) { return FString::Printf(TEXT("id=%d — записи нет"), ItemCode); }
	const FAsdaBrowserItemAssets A = D->ResolveAssets(Idx, /*bMale*/true, Enhance);
	if (A.GlowBpObjectPath.IsEmpty())
	{
		return FString::Printf(TEXT("id=%d «%s»: эффекта нет (jeb='%s')"), ItemCode, *A.Name, *A.GlowJebName);
	}
	UClass* Cls = LoadClass<AActor>(nullptr, *A.GlowBpObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Cls) { return FString::Printf(TEXT("id=%d: BP не загрузился (%s)"), ItemCode, *A.GlowBpObjectPath); }

	// Сцена превью БЕЗ Slate — ровно тот же мир, что во вьюпорте панели.
	FPreviewScene Scene{ FPreviewScene::ConstructionValues() };
	UWorld* W = Scene.GetWorld();
	if (!W) { return TEXT("не удалось создать мир превью"); }

	const FTransform FxWorld = FTransform::Identity;
	FActorSpawnParameters SP;
	SP.ObjectFlags = RF_Transient;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Fx = W->SpawnActor<AActor>(Cls, FxWorld, SP);
	if (!Fx) { return TEXT("актор эффекта не заспавнился"); }
	if (AAsdaEffectActor* E = Cast<AAsdaEffectActor>(Fx))
	{
		E->bStrictQuadAttach = true;
		E->bFollowPosition = false;
		Fx->RerunConstructionScripts();
	}
	FVector O1, X1; Fx->GetActorBounds(false, O1, X1);

	// ── Сколько ГЕОМЕТРИИ построил эффект (секции ProceduralMesh) ───────────────
	UProceduralMeshComponent* PMC = Fx->FindComponentByClass<UProceduralMeshComponent>();
	auto CountVerts = [PMC]() -> int32
	{
		int32 Total = 0;
		if (PMC)
		{
			for (int32 s = 0; s < PMC->GetNumSections(); ++s)
			{
				if (const FProcMeshSection* Sec = PMC->GetProcMeshSection(s)) { Total += Sec->ProcVertexBuffer.Num(); }
			}
		}
		return Total;
	};
	auto TickWorld = [W](int32 N) { for (int32 i = 0; i < N; ++i) { W->Tick(LEVELTICK_All, 1.f / 30.f); } };

	AAsdaEffectActor* EA = Cast<AAsdaEffectActor>(Fx);

	// (1) Камера превью-сцены рядом с предметом — как в панели после фикса.
	if (EA) { EA->bUseViewOverride = true; EA->ViewLocationOverride = FVector(0.f, -160.f, 60.f); EA->ViewRotationOverride = FRotator(-10.f, 90.f, 0.f); }
	TickWorld(30);
	const int32 VertsNear = CountVerts();
	FVector O2, X2; Fx->GetActorBounds(false, O2, X2);

	// (2) Камера ВЬЮПОРТА УРОВНЯ (открытая карта — десятки тысяч юнитов от нуля): так вело себя
	//     превью ДО фикса, потому что AAsdaEffectActor::GetViewLocation берёт FLevelEditorViewportClient.
	if (EA) { EA->ViewLocationOverride = FVector(60000.f, 60000.f, 5000.f); }
	TickWorld(5);
	const int32 VertsFar = CountVerts();

	// (3) Возврат камеры превью — геометрия должна восстановиться.
	if (EA) { EA->ViewLocationOverride = FVector(0.f, -160.f, 60.f); }
	TickWorld(10);
	const int32 VertsBack = CountVerts();

	// (4) Тот же кулл, но задан МАЛЕНЬКИЙ CullDistance при близкой камере — проверка, что путь кулла
	//     вообще исполняется в этом мире (т.е. актор тикает).
	int32 VertsTinyCull = -1;
	if (EA)
	{
		const float Save = EA->CullDistance;
		EA->CullDistance = 1.f;
		TickWorld(3);
		VertsTinyCull = CountVerts();
		EA->CullDistance = Save;
		TickWorld(5);
	}
	const bool bTickReg = Fx->PrimaryActorTick.IsTickFunctionRegistered();
	const bool bTickOn  = Fx->PrimaryActorTick.IsTickFunctionEnabled();

	const int32 NumEmitters = EA ? EA->Emitters.Num() : -1;
	const FString S = FString::Printf(
		TEXT("[браузер-fx] id=%d «%s» эффект '%s' эмиттеров=%d секций=%d\n")
		TEXT("   габариты после прогрева: центр(%.0f,%.0f,%.0f) размах(%.0f,%.0f,%.0f)\n")
		TEXT("   габариты после 1с тика:  центр(%.0f,%.0f,%.0f) размах(%.0f,%.0f,%.0f)\n")
		TEXT("   вершин при камере превью: %d;  при камере вьюпорта уровня (кулл): %d;  после возврата: %d\n")
		TEXT("   тик: зарегистрирован=%d включён=%d;  вершин при CullDistance=1: %d;  override=%d cull=%.0f поз=%s\n"),
		ItemCode, *A.Name, *A.GlowJebName, NumEmitters, PMC ? PMC->GetNumSections() : -1,
		O1.X, O1.Y, O1.Z, X1.X, X1.Y, X1.Z, O2.X, O2.Y, O2.Z, X2.X, X2.Y, X2.Z,
		VertsNear, VertsFar, VertsBack, bTickReg ? 1 : 0, bTickOn ? 1 : 0, VertsTinyCull,
		(EA && EA->bUseViewOverride) ? 1 : 0, EA ? EA->CullDistance : -1.f,
		*Fx->GetActorLocation().ToCompactString());
	UE_LOG(LogAsdaItemBrowserTest, Display, TEXT("%s"), *S);
	Fx->Destroy();
	return S;
}

// ═══ ★ 13.09.2026: самопроверка «Конфигуратора свечения» (слой данных вкладки, без Slate) ═══════════════
#include "AsdaFxGlowConfigModel.h"
#include "World/AsdaFxLightSettings.h"
#include "UObject/UObjectIterator.h"   // ★ ревизия Ф12 (К3): пересчёт экземпляров по мирам
#include "Editor.h"                    // ★ ревизия Ф12 (К2): GEditor->Trans — длина очереди отмен
#include "Editor/Transactor.h"         // ★ ревизия Ф12 (К2): UTransactor::GetQueueLength

FString UAsdaItemBrowserDebug::TestGlowConfig(const FString& Jeb, int32 ItemCode, float IntensityCd, float EmissiveBoost, bool bRemove)
{
	FString S;
	FAsdaFxGlowConfigModel M;
	if (!M.Reload())
	{
		S = FString::Printf(TEXT("[конфигуратор-тест] ЗАГРУЗКА НЕ УДАЛАСЬ: %s"), *M.GetError());
		UE_LOG(LogAsdaItemBrowserTest, Error, TEXT("%s"), *S);
		return S;
	}
	// Дерево без фильтра: что увидит владелец
	TArray<TSharedPtr<FAsdaGlowNode>> Roots;
	M.BuildTree(FString(), false, Roots);   // ★ волна Р1 (п.8): третий аргумент — фильтр «только изменённые»
	for (const TSharedPtr<FAsdaGlowNode>& R : Roots)
	{
		S += FString::Printf(TEXT("[конфигуратор-тест] корень «%s»: детей %d\n"), *R->Label, R->Children.Num());
	}
	TArray<TSharedPtr<FAsdaGlowNode>> Hits;
	M.BuildTree(Jeb, false, Hits);
	S += FString::Printf(TEXT("[конфигуратор-тест] поиск «%s»: корней %d; предметов с этим эффектом %d; семейство %s; полей %d\n"),
		*Jeb, Hits.Num(), M.NumItemsWithJeb(Jeb), *M.FamilyOfJeb(Jeb), M.GetFields().Num());

	FAsdaGlowTarget T;
	T.Jeb = Jeb; T.ItemId = ItemCode; T.Label = FString::Printf(TEXT("тест %d"), ItemCode);
	const FAsdaGlowField* FInt = M.GetFields().FindByPredicate([](const FAsdaGlowField& F) { return F.CppName == GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, IntensityCd); });
	const FAsdaGlowField* FBoost = M.GetFields().FindByPredicate([](const FAsdaGlowField& F) { return F.CppName == GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, EmissiveBoost); });
	if (!FInt || !FBoost) { S += TEXT("[конфигуратор-тест] полей IntensityCd/EmissiveBoost нет в таблице\n"); return S; }

	if (bRemove)
	{
		M.RemoveOverride(T);
	}
	else
	{
		FAsdaFxLightSpec Tmp = M.GetState(T).Work;
		S += FString::Printf(TEXT("[конфигуратор-тест] до правки: %.3f кд, эмиссия задана=%d ×%.2f (дефолт кд %.3f)\n"),
			Tmp.IntensityCd, (int32)Tmp.bOverrideEmissive, Tmp.EmissiveBoost, M.GetState(T).Defaults.IntensityCd);
		Tmp.IntensityCd = IntensityCd;
		M.SetField(T, *FInt, Tmp);
		Tmp = M.GetState(T).Work;
		Tmp.EmissiveBoost = EmissiveBoost;
		M.SetField(T, *FBoost, Tmp);
	}
	FString Msg;
	const bool bOk = M.Save(Msg);
	S += FString::Printf(TEXT("[конфигуратор-тест] сохранение: %s — %s\n"), bOk ? TEXT("OK") : TEXT("ОТКАЗ"), *Msg);

	// Что действует в CDO после перечитывания ini: запись предмета, запись эффекта, и промах для чужого предмета
	const UAsdaFxLightSettings* Set = UAsdaFxLightSettings::Get();
	auto Line = [&](int32 Item)
	{
		const FAsdaFxGlowOverride* O = Set->FindOverride(Jeb, Item);
		return O ? FString::Printf(TEXT("запись %s/%d свет=%d %.3f кд эмиссия=%d ×%.2f ярлык=%s"), *O->Jeb, O->ItemId, (int32)O->bLight,
			O->Spec.IntensityCd, (int32)O->Spec.bOverrideEmissive, O->Spec.EmissiveBoost, *O->Spec.FamilyId) : FString(TEXT("записи нет"));
	};
	S += FString::Printf(TEXT("[конфигуратор-тест] CDO: переопределений %d; предмет %d → %s; предмет %d → %s\n"),
		Set->Overrides.Num(), ItemCode, *Line(ItemCode), ItemCode + 1, *Line(ItemCode + 1));
	UE_LOG(LogAsdaItemBrowserTest, Display, TEXT("%s"), *S);
	return S;
}

// ═══ ★★★ РЕВИЗИЯ Ф12 (С6): СТАРЫЙ ВЫЧИСЛИТЕЛЬ meta EditCondition — ТОЛЬКО ДЛЯ ЗАМЕРА ══════════════
//   Взят ДОСЛОВНО из _backup/2026-09-20_fx_editor2/Plugins/AsdaItemBrowser/Source/AsdaItemBrowser/
//   Private/AsdaFxGlowConfigModel.cpp, строки 163-307 (та самая своя грамматика, которую волна Ф11
//   заменила штатным EditConditionEvaluator). Живёт ТОЛЬКО здесь и НЕ вызывается ничем, кроме
//   самопроверки: равенство поведения двух вычислителей надо ЗАМЕРИТЬ, а не объявить.
//   Если когда-нибудь замер разойдётся, разойдётся он в логе — с именем поля и формой.
namespace AsdaGlowCondLegacy
{
	// Разбить по оператору ВЕРХНЕГО уровня (внутри скобок не режем)
	static void SplitTop(const FString& E, const TCHAR* Op, TArray<FString>& Out)
	{
		Out.Reset();
		const int32 OpLen = FCString::Strlen(Op);
		int32 Depth = 0, Start = 0;
		for (int32 i = 0; i < E.Len(); ++i)
		{
			const TCHAR C = E[i];
			if (C == TEXT('(')) { ++Depth; continue; }
			if (C == TEXT(')')) { --Depth; continue; }
			if (Depth != 0 || i + OpLen > E.Len()) { continue; }
			if (FCString::Strncmp(*E + i, Op, OpLen) == 0)
			{
				Out.Add(E.Mid(Start, i - Start));
				Start = i + OpLen;
				i += OpLen - 1;
			}
		}
		Out.Add(E.Mid(Start));
	}

	// Число поля структуры: bool → 0/1, float/double, int, enum → значение
	static bool ReadProp(const FProperty* P, const FAsdaFxLightSpec& S, double& Out)
	{
		if (const FBoolProperty* B = CastField<FBoolProperty>(P)) { Out = B->GetPropertyValue_InContainer(&S) ? 1.0 : 0.0; return true; }
		if (const FFloatProperty* F = CastField<FFloatProperty>(P)) { Out = F->GetPropertyValue_InContainer(&S); return true; }
		if (const FDoubleProperty* D = CastField<FDoubleProperty>(P)) { Out = D->GetPropertyValue_InContainer(&S); return true; }
		if (const FIntProperty* I = CastField<FIntProperty>(P)) { Out = I->GetPropertyValue_InContainer(&S); return true; }
		if (const FEnumProperty* E = CastField<FEnumProperty>(P))
		{
			Out = (double)E->GetUnderlyingProperty()->GetSignedIntPropertyValue(E->ContainerPtrToValuePtr<void>(&S));
			return true;
		}
		return false;
	}

	static bool Eval(const FString& E, const FAsdaFxLightSpec& S);

	static bool Primary(const FString& In, const FAsdaFxLightSpec& S)
	{
		FString E = In.TrimStartAndEnd();
		if (E.IsEmpty()) { return true; }
		// Скобка охватывает всё выражение — снимаем её и считаем внутренность
		if (E.StartsWith(TEXT("(")) && E.EndsWith(TEXT(")")))
		{
			int32 Depth = 0;
			bool bWhole = true;
			for (int32 i = 0; i < E.Len() - 1; ++i)
			{
				if (E[i] == TEXT('(')) { ++Depth; }
				else if (E[i] == TEXT(')')) { --Depth; }
				if (Depth == 0) { bWhole = false; break; }
			}
			if (bWhole) { return Eval(E.Mid(1, E.Len() - 2), S); }
		}
		if (E.StartsWith(TEXT("!")) && !E.StartsWith(TEXT("!="))) { return !Primary(E.RightChop(1), S); }
		// Порядок важен: «>=» проверяется РАНЬШЕ «>», иначе «>» разрежет «>=»
		static const TCHAR* Ops[] = { TEXT("=="), TEXT("!="), TEXT(">="), TEXT("<="), TEXT(">"), TEXT("<") };
		const UStruct* Struct = FAsdaFxLightSpec::StaticStruct();
		for (int32 k = 0; k < (int32)UE_ARRAY_COUNT(Ops); ++k)
		{
			TArray<FString> Parts;
			SplitTop(E, Ops[k], Parts);
			if (Parts.Num() != 2) { continue; }
			const FString L = Parts[0].TrimStartAndEnd();
			const FString R = Parts[1].TrimStartAndEnd();
			const FProperty* P = Struct->FindPropertyByName(FName(*L));
			double A = 0.0, B = 0.0;
			if (!P || !ReadProp(P, S, A)) { return true; }
			if (const FEnumProperty* EP = CastField<FEnumProperty>(P))
			{
				FString Short = R;
				int32 Pos = INDEX_NONE;
				if (R.FindLastChar(TEXT(':'), Pos)) { Short = R.RightChop(Pos + 1); }
				const int64 V = EP->GetEnum()->GetValueByNameString(Short);
				if (V == INDEX_NONE) { return true; }
				B = (double)V;
			}
			else if (R.Equals(TEXT("true"), ESearchCase::IgnoreCase)) { B = 1.0; }
			else if (R.Equals(TEXT("false"), ESearchCase::IgnoreCase)) { B = 0.0; }
			else { B = FCString::Atod(*R); }
			switch (k)
			{
			case 0: return FMath::IsNearlyEqual(A, B);
			case 1: return !FMath::IsNearlyEqual(A, B);
			case 2: return A >= B;
			case 3: return A <= B;
			case 4: return A > B;
			default: return A < B;
			}
		}
		// Голое имя поля — читается как «истинно ли оно»
		const FProperty* P = Struct->FindPropertyByName(FName(*E));
		double V = 0.0;
		if (!P || !ReadProp(P, S, V)) { return true; }
		return V != 0.0;
	}

	static bool Eval(const FString& E, const FAsdaFxLightSpec& S)
	{
		TArray<FString> Parts;
		SplitTop(E, TEXT("||"), Parts);
		if (Parts.Num() > 1)
		{
			for (const FString& P : Parts) { if (Eval(P, S)) { return true; } }
			return false;
		}
		SplitTop(E, TEXT("&&"), Parts);
		if (Parts.Num() > 1)
		{
			for (const FString& P : Parts) { if (!Eval(P, S)) { return false; } }
			return true;
		}
		return Primary(E, S);
	}
}

// ═══ ★★★ ВОЛНЫ Р0 и Р1 (20.09.2026): САМОПРОВЕРКА РЕДАКТОРСКОЙ ЧАСТИ ══════════
//   Живой клик по вкладке невозможен (окна UE показывать запрещено), поэтому всё, что
//   панель считает ДО отрисовки — разделы, условия работы полей, подписи, дерево и
//   эмиттеры — проверяется здесь, тем же слоем данных, что у самой вкладки.
//   ★ НИ ОДНОЙ ЗАПИСИ НА ДИСК: ни Save, ни JSON, ни ini, ни .uasset.
// ═══ ★★★ ПОПРАВКА РЕВИЗИИ Ф12 (К3): ЧЕСТНО ПРО КАРТЫ ══════════════════════════════════════════════
//   ПРЕЖНИЙ ТЕКСТ ЗДЕСЬ И В ЗАГОЛОВКЕ УТВЕРЖДАЛ «карты проекта не грузятся». Сама функция карт и
//   вправду не грузит — но прогон 20.09 06:05 шёл через -ExecutePythonScript, то есть через ПОЛНЫЙ
//   редактор, и тот загрузил стартовую карту сам (headless_glow_editor_ui.log:1892,
//   «MAP LOAD FILE=…/Content/Map/Login.umap»). Утверждение было про функцию, а читалось как про
//   прогон — и оказалось ложным ровно там, где это важно (Login.umap защищён).
//   ЧТО ИСПРАВЛЕНО: прогон идёт коммандлетом -run=pythonscript (карта не грузится вовсе), а РАЗБОР
//   поэмиттерной правки считает только акторов СВОЕГО временного мира. Раздача правки при этом
//   по-прежнему идёт во все загруженные экземпляры — это её работа, и в редакторе владельца с
//   открытой картой так и должно быть; тест просто больше не выдаёт чужих акторов за свои.
FString UAsdaItemBrowserDebug::TestGlowEditorUi(const FString& Jeb)
{
	FString S;
	// ═══ ★★★ ВОЛНА Ф16 (ДИСЦИПЛИНА, 20.09.2026): ЧЕЙ БИНАРЬ СЕЙЧАС ИСПОЛНЯЕТСЯ ══════════════════
	//   Ревизия Ф15 отметила: «прогон позже сборки» доказывалось ТОЛЬКО временем файлов, а это
	//   косвенная улика — она не отличает «загрузился новый .dll» от «остался старый в памяти».
	//   Прямое свидетельство — МЕТКА КОМПИЛЯЦИИ, вшитая препроцессором в сам объектный код:
	//   `__DATE__ __TIME__` раскрывается при сборке ЭТОЙ единицы трансляции, и напечатать её может
	//   только тот бинарь, который эту единицу и содержит. Меток две — по одной на каждую цель:
	//   редакторский плагин (эта строка) и игровой модуль (AAsdaEffectActor::BuildStamp).
	S += FString::Printf(TEXT("[ред-тест] БИНАРЬ: плагин AsdaItemBrowserDebug.cpp скомпилирован %s %s; игровой модуль AsdaEffectActor.cpp — %s\n"),
		TEXT(__DATE__), TEXT(__TIME__), *AAsdaEffectActor::BuildStamp());
	FAsdaFxGlowConfigModel M;
	if (!M.Reload())
	{
		S = FString::Printf(TEXT("[ред-тест] ЗАГРУЗКА НЕ УДАЛАСЬ: %s"), *M.GetError());
		UE_LOG(LogAsdaItemBrowserTest, Error, TEXT("%s"), *S);
		return S;
	}

	// — волна Р0 п.2: разделы и раскладка полей по ним —
	const TArray<FString>& Secs = FAsdaFxGlowConfigModel::GetSections();
	TArray<int32> PerSec;
	PerSec.Init(0, Secs.Num());
	FString Misc;
	// ★ Ревизия Ф11 (С4): поля «только для чтения» считаются отдельно — их видно, но пишутся они не отсюда
	int32 NumRw = 0, NumRo = 0;
	FString RoList;
	for (const FAsdaGlowField& F : M.GetFields())
	{
		if (!F.Prop) { continue; }
		PerSec[FMath::Clamp(F.Section, 0, Secs.Num() - 1)]++;
		if (F.bReadOnly) { ++NumRo; RoList += (RoList.IsEmpty() ? TEXT("") : TEXT(", ")) + F.CppName.ToString(); }
		else { ++NumRw; }
		if (F.Section == Secs.Num() - 1) { Misc += (Misc.IsEmpty() ? TEXT("") : TEXT(", ")) + F.CppName.ToString(); }
	}
	for (int32 i = 0; i < Secs.Num(); ++i)
	{
		S += FString::Printf(TEXT("[ред-тест] раздел \"%s\": полей %d\n"), *Secs[i], PerSec[i]);
	}
	S += FString::Printf(TEXT("[ред-тест] в \"Служебное\" ушли: %s\n"), Misc.IsEmpty() ? TEXT("(ничего)") : *Misc);
	S += FString::Printf(TEXT("[ред-тест] только для чтения: %s\n"), RoList.IsEmpty() ? TEXT("(нет)") : *RoList);

	// — волна Р0 п.1: сколько полей РАБОТАЕТ при каждой форме —
	//   Проверка вторым методом: числа по формам обязаны РАЗЛИЧАТЬСЯ. Если разбор
	//   условия молча возвращает \"выполнено\" на всё, столбец выйдет одинаковым — и это видно сразу
	const UEnum* ShapeEnum = StaticEnum<EAsdaFxLightShape>();
	for (int32 k = 0; k < ShapeEnum->NumEnums() - 1; ++k)
	{
		FAsdaFxLightSpec Probe;
		Probe.Shape = (EAsdaFxLightShape)ShapeEnum->GetValueByIndex(k);
		int32 Active = 0, Hidden = 0;
		for (const FAsdaGlowField& F : M.GetFields())
		{
			if (!F.Prop) { continue; }
			bool bHides = false;
			if (FAsdaFxGlowConfigModel::IsFieldActive(F, Probe, bHides)) { ++Active; }
			else if (bHides) { ++Hidden; }
		}
		S += FString::Printf(TEXT("[ред-тест] форма %s: работает %d, спрятано %d\n"),
			*ShapeEnum->GetNameStringByIndex(k), Active, Hidden);
	}
	// ═══ ★★★ РЕВИЗИЯ Ф12 (С6): A/B «СТАРЫЙ ВЫЧИСЛИТЕЛЬ УСЛОВИЙ ПРОТИВ НОВОГО» — ЗАМЕР, А НЕ ДОВОД ═══
	//   Волна Ф11 заменила свою рекурсивную грамматику разбора meta EditCondition на ШТАТНЫЙ
	//   EditConditionEvaluator движка и объявила поведение прежним — рассуждением. Рассуждение
	//   доказательством не является. Здесь оба вычислителя считаются В ОДНОМ ПРОГОНЕ, на одном
	//   бинаре, по ВСЕМ формам и ВСЕМ полям, и печатается каждое расхождение поимённо.
	//   Старая грамматика взята ДОСЛОВНО из _backup/2026-09-20_fx_editor2/…/AsdaFxGlowConfigModel.cpp
	//   (строки 163-307) и живёт ТОЛЬКО здесь, в самопроверке: в рабочем коде её больше нет.
	// ═══ ★★★ РЕВИЗИЯ Ф13 (С4, 20.09.2026): ПЕРЕБОР БЫЛ ОДНОСТОРОННИМ — СТАЛ ДВУСТОРОННИМ ══════════
	//   ЧТО БЫЛО НЕ ТАК. 40 состояний варьировали ровно четыре вещи: форму, тень, мерцание и замок.
	//   А условия полей смотрят ещё и на *SizeMode==Fixed, *NormalMode/SpotDirMode==Explicit,
	//   ShapeSegments>1, bOverrideEmissive, RadiusMode==Section|Bounds — и при умолчаниях все эти
	//   сравнения ЛОЖНЫ во всех 40 состояниях. Значит, около дюжины полей сверялись только в ветке
	//   «спрятано»: расхождение вычислителей в ветке «работает» перебор поймать не мог в принципе,
	//   а число «сверок» выглядело большим и убедительным. Это ровно тот случай, когда
	//   самопроверка слепа к тому, что проверяет (закон pixel-metric-needs-control-pairs).
	//   КАК СДЕЛАНО. Свойства, от которых условия зависят, берутся ИЗ САМИХ ВЫРАЖЕНИЙ (разбор на
	//   идентификаторы + поиск по FAsdaFxLightSpec), а не списком, написанным руками: список руками
	//   устареет на первой же новой meta EditCondition. Дальше каждому такому свойству перебираются
	//   ВСЕ его значения (булево — оба, перечисление — все элементы, число — 0 и 5), и каждое
	//   сочетается с каждой формой. Прежние 40 состояний сохранены: они проверяют сочетания.
	//   ★ И ГЛАВНОЕ — ПЕЧАТАЕТСЯ ОХВАТ: у скольких условий за прогон наблюдались ОБЕ ветки. Поле,
	//   у которого ветка одна, называется поимённо, и число сверок больше ни о чём не «говорит».
	{
		int32 Cmp = 0, Diff = 0;
		FString DiffList;
		UScriptStruct* SpecStruct = FAsdaFxLightSpec::StaticStruct();

		// Шаг 1: имена свойств, упомянутых хоть в одном EditCondition
		TSet<FName> CondProps;
		for (const FAsdaGlowField& F : M.GetFields())
		{
			if (!F.Prop || F.EditCondition.IsEmpty()) { continue; }
			FString Tok;
			for (int32 i = 0; i <= F.EditCondition.Len(); ++i)
			{
				const TCHAR Ch = (i < F.EditCondition.Len()) ? F.EditCondition[i] : TEXT(' ');
				if (FChar::IsAlnum(Ch) || Ch == TEXT('_')) { Tok.AppendChar(Ch); continue; }
				// Не свойство структуры (литерал перечисления, число, имя типа) — SpecStruct его не знает
				if (!Tok.IsEmpty() && SpecStruct->FindPropertyByName(FName(*Tok))) { CondProps.Add(FName(*Tok)); }
				Tok.Reset();
			}
		}

		// Шаг 2: задать свойству его вариант № VariantIdx. false — вариантов больше нет
		auto ApplyVariant = [](FAsdaFxLightSpec& P, const FProperty* Prop, int32 VariantIdx) -> bool
		{
			void* Ptr = Prop->ContainerPtrToValuePtr<void>(&P);
			if (const FBoolProperty* BP = CastField<FBoolProperty>(Prop))
			{
				if (VariantIdx >= 2) { return false; }
				BP->SetPropertyValue(Ptr, VariantIdx != 0);
				return true;
			}
			if (const FEnumProperty* EP = CastField<FEnumProperty>(Prop))
			{
				const UEnum* En = EP->GetEnum();
				if (!En || VariantIdx >= En->NumEnums() - 1) { return false; }
				EP->GetUnderlyingProperty()->SetIntPropertyValue(Ptr, En->GetValueByIndex(VariantIdx));
				return true;
			}
			if (const FByteProperty* BtP = CastField<FByteProperty>(Prop))
			{
				if (const UEnum* En = BtP->Enum)
				{
					if (VariantIdx >= En->NumEnums() - 1) { return false; }
					BtP->SetIntPropertyValue(Ptr, En->GetValueByIndex(VariantIdx));
					return true;
				}
				if (VariantIdx >= 2) { return false; }
				BtP->SetIntPropertyValue(Ptr, (int64)(VariantIdx == 0 ? 0 : 5));
				return true;
			}
			if (const FNumericProperty* NP = CastField<FNumericProperty>(Prop))
			{
				// 0 и 5 закрывают обе ветки всех наших сравнений (== 0, > 0, > 1)
				if (VariantIdx >= 2) { return false; }
				if (NP->IsFloatingPoint()) { NP->SetFloatingPointPropertyValue(Ptr, VariantIdx == 0 ? 0.0 : 5.0); }
				else { NP->SetIntPropertyValue(Ptr, (int64)(VariantIdx == 0 ? 0 : 5)); }
				return true;
			}
			return false;   // строка, структура, объектная ссылка — в наших условиях не встречаются
		};

		// Шаг 3: состояния. Каждое свойство условий × каждое его значение × каждая форма
		TArray<FAsdaFxLightSpec> Probes;
		int32 NumVaried = 0;
		FString NoVariants;
		for (const FName& PropName : CondProps)
		{
			const FProperty* Prop = SpecStruct->FindPropertyByName(PropName);
			if (!Prop) { continue; }
			bool bAny = false;
			for (int32 V = 0; ; ++V)
			{
				FAsdaFxLightSpec Seed;
				if (!ApplyVariant(Seed, Prop, V)) { break; }
				bAny = true;
				for (int32 k = 0; k < ShapeEnum->NumEnums() - 1; ++k)
				{
					FAsdaFxLightSpec P = Seed;
					P.Shape = (EAsdaFxLightShape)ShapeEnum->GetValueByIndex(k);
					Probes.Add(P);
				}
			}
			if (bAny) { ++NumVaried; }
			else { NoVariants += (NoVariants.IsEmpty() ? TEXT("") : TEXT(", ")) + PropName.ToString(); }
		}
		// Прежние 40 сочетаний — их смысл (несколько признаков разом) новый перебор не покрывает
		for (int32 k = 0; k < ShapeEnum->NumEnums() - 1; ++k)
		{
			FAsdaFxLightSpec P;
			P.Shape = (EAsdaFxLightShape)ShapeEnum->GetValueByIndex(k);
			Probes.Add(P);
			FAsdaFxLightSpec P2 = P; P2.bCastShadow = true;            Probes.Add(P2);
			FAsdaFxLightSpec P3 = P; P3.FlickerHz = 5.f;               Probes.Add(P3);
			FAsdaFxLightSpec P4 = P; P4.bManual = true;                Probes.Add(P4);
			FAsdaFxLightSpec P5 = P; P5.bCastShadow = true; P5.FlickerHz = 5.f; P5.bManual = true; Probes.Add(P5);
		}

		// Шаг 4: сверка обоих вычислителей + охват веток по каждому условию
		TMap<FName, TPair<bool, bool>> Seen;   // поле → (наблюдалось «работает», наблюдалось «спрятано»)
		for (const FAsdaFxLightSpec& P : Probes)
		{
			for (const FAsdaGlowField& F : M.GetFields())
			{
				if (!F.Prop || F.EditCondition.IsEmpty()) { continue; }
				bool bH = false;
				const bool bNew = FAsdaFxGlowConfigModel::IsFieldActive(F, P, bH);
				const bool bOld = AsdaGlowCondLegacy::Eval(F.EditCondition, P);
				++Cmp;
				TPair<bool, bool>& Cov = Seen.FindOrAdd(F.CppName, TPair<bool, bool>(false, false));
				if (bNew) { Cov.Key = true; } else { Cov.Value = true; }
				if (bNew != bOld)
				{
					++Diff;
					if (Diff <= 12)
					{
						DiffList += FString::Printf(TEXT("\n[ред-тест]     %s (%s) форма=%s: старый %s, новый %s"),
							*F.CppName.ToString(), *F.EditCondition,
							*ShapeEnum->GetNameStringByValue((int64)P.Shape),
							bOld ? TEXT("работает") : TEXT("спрятано"), bNew ? TEXT("работает") : TEXT("спрятано"));
					}
				}
			}
		}
		int32 Both = 0;
		FString OneSided;
		for (const TPair<FName, TPair<bool, bool>>& C : Seen)
		{
			if (C.Value.Key && C.Value.Value) { ++Both; }
			else
			{
				OneSided += FString::Printf(TEXT("%s%s (только «%s»)"), OneSided.IsEmpty() ? TEXT("") : TEXT(", "),
					*C.Key.ToString(), C.Value.Key ? TEXT("работает") : TEXT("спрятано"));
			}
		}
		S += FString::Printf(TEXT("[ред-тест] С4 состояний перебора: %d (свойств из условий %d, из них перебираемых %d%s%s)\n"),
			Probes.Num(), CondProps.Num(), NumVaried,
			NoVariants.IsEmpty() ? TEXT("") : TEXT("; БЕЗ вариантов: "), NoVariants.IsEmpty() ? TEXT("") : *NoVariants);
		S += FString::Printf(TEXT("[ред-тест] С4 ОХВАТ УСЛОВИЙ: обе ветки наблюдались у %d из %d полей с условием%s%s\n"),
			Both, Seen.Num(),
			OneSided.IsEmpty() ? TEXT(" — односторонних нет") : TEXT("; ОДНОСТОРОННИЕ: "), OneSided.IsEmpty() ? TEXT("") : *OneSided);
		S += FString::Printf(TEXT("[ред-тест] С6 A/B вычислителей условий: сверок %d (%d состояний × поля с условием), расхождений %d%s\n"),
			Cmp, Probes.Num(), Diff, *DiffList);
	}

	// Контрольные пары: мерцание 0/5 Гц и тень выкл/вкл
	auto CountActive = [&M](const FAsdaFxLightSpec& P)
	{
		int32 N = 0;
		for (const FAsdaGlowField& F : M.GetFields())
		{
			bool bH = false;
			if (F.Prop && FAsdaFxGlowConfigModel::IsFieldActive(F, P, bH)) { ++N; }
		}
		return N;
	};
	FAsdaFxLightSpec PlainA, FlickB, ShadowC, ShadowD;
	FlickB.FlickerHz = 5.f;
	ShadowD.bCastShadow = true;
	S += FString::Printf(TEXT("[ред-тест] мерцание 0 Гц -> %d рабочих, 5 Гц -> %d; тень выкл -> %d, вкл -> %d\n"),
		CountActive(PlainA), CountActive(FlickB), CountActive(ShadowC), CountActive(ShadowD));

	// — волна Р0 пп.3 и 5: подписи без номеров и поправленные имена —
	int32 Numbered = 0;
	for (const FAsdaGlowField& F : M.GetFields())
	{
		if (F.Prop && !F.Label.IsEmpty() && FChar::IsDigit(F.Label[0])) { ++Numbered; }
	}
	S += FString::Printf(TEXT("[ред-тест] подписей, начинающихся с цифры: %d (ожидается 0)\n"), Numbered);
	static const TCHAR* Watch[] = { TEXT("RadiusMin"), TEXT("RadiusMax"), TEXT("BarnDoorAngle"), TEXT("AlphaPower"),
		TEXT("Emitter"), TEXT("MaxDrawDistance"), TEXT("SourceRadius") };
	for (const TCHAR* Name : Watch)
	{
		const FAsdaGlowField* F = M.GetFields().FindByPredicate([Name](const FAsdaGlowField& X) { return X.CppName == FName(Name); });
		S += FString::Printf(TEXT("[ред-тест] %s -> \"%s\" (раздел \"%s\")\n"), Name,
			F ? *F->Label : TEXT("поля нет"), F ? *Secs[FMath::Clamp(F->Section, 0, Secs.Num() - 1)] : TEXT("-"));
	}

	// — волна Р1 пп.6 и 8: корни дерева и фильтр \"только изменённые\" —
	TArray<TSharedPtr<FAsdaGlowNode>> Roots, Changed;
	M.BuildTree(FString(), false, Roots);
	for (const TSharedPtr<FAsdaGlowNode>& R : Roots)
	{
		S += FString::Printf(TEXT("[ред-тест] корень \"%s\": веток %d\n"), *R->Label, R->Children.Num());
	}
	M.BuildTree(FString(), true, Changed);
	S += FString::Printf(TEXT("[ред-тест] фильтр \"только изменённые\": корней %d (без фильтра %d)\n"), Changed.Num(), Roots.Num());

	// — волна Р1 п.7: эмиттеры выбранного эффекта —
	TSharedPtr<FAsdaGlowNode> FxNode;
	for (const TSharedPtr<FAsdaGlowNode>& R : Roots)
	{
		if (!R->Label.StartsWith(TEXT("Эффекты карт"))) { continue; }
		for (const TSharedPtr<FAsdaGlowNode>& Fam : R->Children)
		{
			for (const TSharedPtr<FAsdaGlowNode>& E : Fam->Children)
			{
				if (!FxNode.IsValid() && (Jeb.IsEmpty() || E->Jeb.Equals(Jeb, ESearchCase::IgnoreCase))) { FxNode = E; }
			}
		}
	}
	if (!FxNode.IsValid())
	{
		S += TEXT("[ред-тест] эффект для разбора эмиттеров не найден\n");
	}
	else
	{
		M.BuildEmitterChildren(FxNode);
		S += FString::Printf(TEXT("[ред-тест] эффект %s: эмиттеров %d\n"), *FxNode->Jeb, FxNode->Children.Num());
		for (const TSharedPtr<FAsdaGlowNode>& Em : FxNode->Children)
		{
			FAsdaGlowEmitterRef R;
			R.ClassPath = Em->FxClassPath;
			R.Index = Em->EmitterIndex;
			const FAsdaFxLightSpec W = M.EmitterWork(R);
			const FAsdaFxLightSpec Inh = M.EmitterInherited(R);
			S += FString::Printf(TEXT("[ред-тест]   %s | сейчас %.2f кд / от эффекта %.2f кд\n"), *Em->Label, W.IntensityCd, Inh.IntensityCd);
		}
	}
	S += FString::Printf(TEXT("[ред-тест] полей в SPEC_FIELDS: %d (из них правимых %d, только для чтения %d)\n"),
		M.GetFields().Num(), NumRw, NumRo);

	// ═══ ★★★ РЕВИЗИЯ Ф12 (К1, К2, К3, С1, С4): ДОКАЗАТЕЛЬСТВО ПОЭМИТТЕРНОЙ ПРАВКИ ═══════════════
	//   Всё делается в СВОЁМ временном мире (UWorld::CreateWorld, транзитный пакет). Сам тест карт
	//   не грузит; если карта уже открыта в редакторе владельца, раздача дойдёт и до её акторов —
	//   это её работа, поэтому РАЗБОР (счёт и сверка) ограничен акторами своего мира, а число
	//   «чужих» экземпляров печатается отдельной строкой.
	//   ПРОВЕРЯЕТСЯ ПАРОЙ (закон pixel-metric-needs-control-pairs):
	//     подопытный экземпляр — полная копия CDO: он ОБЯЗАН получить правку;
	//     ★ контрольный — отличающийся ДРУГИМ полем (ревизия Ф12, С1). Прежде контроль отличался
	//       ТЕМ ЖЕ полем, что и правилось, и потому не мог поймать главное отступление от закона
	//       движка: движок сравнивает ОДНО правимое свойство, а мы сравнивали всю структуру.
	//       Экземпляр, которому владелец поштучно задал ЛЮБОЕ другое поле, правки не получал вовсе
	//       и оставался отличным от CDO — то есть дельтой, которую сохранение карты впечатает в .umap.
	//   В конце всё возвращается к исходному, и признак «изменён» снимается — ★ но только если
	//   состояние И ВПРАВДУ вернулось (ревизия Ф12, С4): функция BlueprintCallable, её могут позвать
	//   в живом редакторе, и снять там признак с по-настоящему изменённого пакета значит спрятать
	//   правки владельца и от «Save All», и от вопроса при выходе.
	if (FxNode.IsValid() && FxNode->Children.Num() > 0 && FxNode->Children[0]->EmitterIndex != INDEX_NONE)
	{
		FAsdaGlowEmitterRef R;
		R.ClassPath = FxNode->Children[0]->FxClassPath;
		R.Index = FxNode->Children[0]->EmitterIndex;
		AAsdaEffectActor* Cdo = M.EmitterCdo(R);
		UClass* Cls = Cdo ? Cdo->GetClass() : nullptr;
		UPackage* Pkg = Cls ? Cls->GetOutermost() : nullptr;
		const bool bWasDirty = Pkg && Pkg->IsDirty();
		bool bStateRestored = false;   // ★ С4: вернулось ли состояние к исходному (решает судьбу признака «изменён»)
		// ★ С4: снимок ИСХОДНОГО состояния целиком — и эмиттеры, и долговечная копия. По нему в
		//   конце и решается, вернулось ли всё на место
		TArray<FAsdaFxEmitterGlow> Glow0;
		TArray<FAsdaFxEmitterGlowSaved> Saved0;
		if (Cdo)
		{
			for (const FAsdaFxEmitter& Em : Cdo->Emitters) { Glow0.Add(Em.Glow); }
			Saved0 = Cdo->EmitterGlowSaved;
		}
		const FAsdaGlowField* Fld = M.GetFields().FindByPredicate([](const FAsdaGlowField& X)
			{ return X.CppName == GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, IntensityCd); });
		// ★ С1: ВТОРОЕ, ДРУГОЕ поле — им отличается контрольный экземпляр. Берём первое правимое
		//   число, не совпадающее с правимым полем
		const FAsdaGlowField* Fld2 = M.GetFields().FindByPredicate([Fld](const FAsdaGlowField& X)
			{
				return X.Prop && !X.bReadOnly && Fld && X.CppName != Fld->CppName
					&& (CastField<FFloatProperty>(X.Prop) || CastField<FDoubleProperty>(X.Prop));
			});
		UWorld* W = (Cls && Fld && Fld->Prop && Fld2 && Fld2->Prop) ? UWorld::CreateWorld(EWorldType::Editor, false) : nullptr;
		if (!W)
		{
			S += TEXT("[ред-тест] К1: разбор пропущен (нет класса эффекта, поля «Сила света», второго числового поля или мира)\n");
		}
		else
		{
			FActorSpawnParameters Sp;
			Sp.ObjectFlags = RF_Transient;   // актор стенда не должен попасть ни в один пакет
			AAsdaEffectActor* Probe = W->SpawnActor<AAsdaEffectActor>(Cls, Sp);
			AAsdaEffectActor* Ctrl = W->SpawnActor<AAsdaEffectActor>(Cls, Sp);
			if (!Probe || !Ctrl || !Probe->Emitters.IsValidIndex(R.Index) || !Ctrl->Emitters.IsValidIndex(R.Index))
			{
				S += TEXT("[ред-тест] К1: экземпляры не создались — разбор пропущен\n");
			}
			else
			{
				// ★ К3: сколько экземпляров этого класса в СВОЁМ мире, а сколько в прочих (карты).
				//   Прогон коммандлетом -run=pythonscript карт не грузит — «прочих» ожидается 0
				int32 Mine = 0, Foreign = 0;
				for (TObjectIterator<AAsdaEffectActor> It; It; ++It)
				{
					if (It->GetClass() != Cls || It->HasAnyFlags(RF_ClassDefaultObject)) { continue; }
					if (It->GetWorld() == W) { ++Mine; } else { ++Foreign; }
				}
				S += FString::Printf(TEXT("[ред-тест] К3 экземпляров класса: в своём мире %d, в прочих мирах %d (карты этим прогоном не грузятся — ожидается 0)\n"),
					Mine, Foreign);

				// ★ С1: контрольный отличается ДРУГИМ полем — и обязан сохранить своё отличие,
				//   но при этом ПОЛУЧИТЬ правку правимого поля (закон движка: поле за полем)
				const float CtrlMark = 12345.f;
				{
					void* Spec = &Ctrl->Emitters[R.Index].Glow.Spec;
					if (const FFloatProperty* FP = CastField<FFloatProperty>(Fld2->Prop)) { FP->SetPropertyValue_InContainer(Spec, CtrlMark); }
					else if (const FDoubleProperty* DP = CastField<FDoubleProperty>(Fld2->Prop)) { DP->SetPropertyValue_InContainer(Spec, (double)CtrlMark); }
				}
				auto CtrlOther = [Ctrl, Fld2, &R]() -> float
				{
					const void* Spec = &Ctrl->Emitters[R.Index].Glow.Spec;
					if (const FFloatProperty* FP = CastField<FFloatProperty>(Fld2->Prop)) { return (float)FP->GetPropertyValue_InContainer(Spec); }
					if (const FDoubleProperty* DP = CastField<FDoubleProperty>(Fld2->Prop)) { return (float)DP->GetPropertyValue_InContainer(Spec); }
					return 0.f;
				};

				const EAsdaFxEmitterGlowSource Src0 = M.EmitterSource(R);
				const float Before = M.EmitterWork(R).IntensityCd;
				const float Want = Before + 7.f;
				FAsdaFxLightSpec Tmp = M.EmitterWork(R);
				Tmp.IntensityCd = Want;
				// ★ К2: транзакция не должна быть ПУСТОЙ. Пустую буфер выбрасывает
				//   (EditorTransaction.cpp:1333-1337), и очередь отмен не растёт — вот и проверка
				const int32 Q0 = (GEditor && GEditor->Trans) ? GEditor->Trans->GetQueueLength() : -1;
				M.SetEmitterField(R, *Fld, Tmp);
				const int32 Q1 = (GEditor && GEditor->Trans) ? GEditor->Trans->GetQueueLength() : -1;

				// ★ К1: РАЗМЕР ДОЛГОВЕЧНОЙ КОПИИ В СЕРЕДИНЕ — без него «после возврата 0 == было 0»
				//   ничего не доказывает: надо видеть, что запись там ПОЯВЛЯЛАСЬ и её было что чистить
				const int32 SavedMid = Cdo ? Cdo->EmitterGlowSaved.Num() : -1;
				const float CdoAfter = M.EmitterWork(R).IntensityCd;
				const float ProbeAfter = Probe->Emitters[R.Index].Glow.Spec.IntensityCd;
				const bool bProbeGot = Probe->Emitters[R.Index].Glow.Source == EAsdaFxEmitterGlowSource::Manual
					&& FMath::IsNearlyEqual(ProbeAfter, Want);
				const bool bCtrlKeptOwn = FMath::IsNearlyEqual(CtrlOther(), CtrlMark);
				const bool bCtrlGotEdit = FMath::IsNearlyEqual(Ctrl->Emitters[R.Index].Glow.Spec.IntensityCd, Want);
				S += FString::Printf(TEXT("[ред-тест] К1 эмиттер %d у %s: было %.2f -> в CDO %.2f; подопытный экземпляр %.2f (%s)\n"),
					R.Index, *FxNode->Jeb, Before, CdoAfter, ProbeAfter,
					bProbeGot ? TEXT("ПРАВКА ДОЕХАЛА") : TEXT("★ НЕ ДОЕХАЛА"));
				S += FString::Printf(TEXT("[ред-тест] С1 контрольный экземпляр (своё поле «%s»=%.2f): своё %s, правимое поле %.2f %s\n"),
					*Fld2->Label, CtrlMark, bCtrlKeptOwn ? TEXT("СОХРАНИЛ — верно") : TEXT("★ ЗАТЁРТО"),
					Ctrl->Emitters[R.Index].Glow.Spec.IntensityCd,
					bCtrlGotEdit ? TEXT("ПОЛУЧЕНО — верно (закон движка: поле за полем)") : TEXT("★ НЕ ПОЛУЧЕНО — раздача всё ещё сравнивает структуру целиком"));
				// ★★★ К2: ЧТО ЗДЕСЬ ВООБЩЕ ПРОВЕРЯЕМО. Буфер отмены заводит только ПОЛНЫЙ редактор;
				//   в коммандлете (-run=pythonscript, а прогон обязан идти именно им — К3)
				//   GEditor->Trans нет, Modify честно возвращает false, и «очередь транзакций»
				//   измерить негде. Поэтому здесь доказывается ПРИЧИНА прежнего провала — что CDO
				//   получил RF_Transactional; сама отмена проверяется живым Ctrl+Z в редакторе.
				//   Выдать отсутствие буфера за провал значило бы врать в другую сторону.
				const bool bHasTrans = (GEditor && GEditor->Trans);
				S += FString::Printf(TEXT("[ред-тест] К2 флаг RF_Transactional у CDO после правки: %s (без него в буфер отмены не пишется НИЧЕГО — UObjectGlobals.cpp:3215-3229)\n"),
					(Cdo && Cdo->HasAnyFlags(RF_Transactional)) ? TEXT("СТОИТ — верно") : TEXT("★ НЕ СТОИТ"));
				if (bHasTrans)
				{
					S += FString::Printf(TEXT("[ред-тест] К2 отмена: Modify вернул %s, очередь транзакций %d -> %d (%s), правок мимо буфера за сеанс %d\n"),
						FAsdaFxGlowConfigModel::LastEmitterEditUndoable() ? TEXT("«записано в буфер»") : TEXT("★ «НЕ ЗАПИСАНО»"),
						Q0, Q1, (Q1 > Q0) ? TEXT("транзакция не пустая — верно") : TEXT("★ ТРАНЗАКЦИЯ ПУСТА: Ctrl+Z съест чужое действие"),
						FAsdaFxGlowConfigModel::NumNonUndoableEmitterEdits());
				}
				else
				{
					S += TEXT("[ред-тест] К2 отмена: буфера отмены в этом прогоне НЕТ (коммандлет, GEditor->Trans == null) — Modify закономерно вернул «не записано», очередь не измеряется. ЭТО НЕ ПРОВАЛ и НЕ ДОКАЗАТЕЛЬСТВО: живой Ctrl+Z проверяется только кликом в редакторе владельца\n");
				}

				// К3: пакет помечен изменённым САМОЙ правкой, число на кнопке и число «не записано» сходятся
				S += FString::Printf(TEXT("[ред-тест] К3 ассетов правилось %d, не записано %d, без пометки %d; пакет изменён: %s\n"),
					M.NumFxAssetsEdited(), M.NumFxAssetsUnsaved(), M.NumFxAssetsUnmarked(),
					(Pkg && Pkg->IsDirty()) ? TEXT("да") : TEXT("★ НЕТ"));
				// С8: * стоит у ТОГО эмиттера, что правился, и не стоит у соседа
				S += FString::Printf(TEXT("[ред-тест] С8 отметка * у правленого эмиттера: %s; у соседнего: %s\n"),
					M.IsEmitterPending(R) ? TEXT("есть — верно") : TEXT("★ НЕТ"),
					[&M, &R]()
					{
						FAsdaGlowEmitterRef N2 = R; N2.Index = R.Index + 1;
						return M.EmitterCdo(N2) ? (M.IsEmitterPending(N2) ? TEXT("★ ЕСТЬ (не должно)") : TEXT("нет — верно")) : TEXT("соседа нет");
					}());

				// С3: фильтр «только изменённые» обязан ПОКАЗАТЬ этот эффект, пока правка жива, —
				//   и по поэмиттерным числам в ассете, а не только по таблице свечения
				{
					TArray<TSharedPtr<FAsdaGlowNode>> Chg;
					M.BuildTree(FString(), true, Chg);
					bool bFound = false;
					for (const TSharedPtr<FAsdaGlowNode>& Root : Chg)
					{
						for (const TSharedPtr<FAsdaGlowNode>& Fam : Root->Children)
						{
							for (const TSharedPtr<FAsdaGlowNode>& E : Fam->Children)
							{
								if (E->Jeb.Equals(FxNode->Jeb, ESearchCase::IgnoreCase)) { bFound = true; }
							}
						}
					}
					S += FString::Printf(TEXT("[ред-тест] С3 фильтр «только изменённые» при живой правке: корней %d, правленый эффект в дереве: %s; состояние НЕИЗВЕСТНО у %d эффект(ов) — они помечены «?» и под фильтром не показаны\n"),
						Chg.Num(), bFound ? TEXT("есть — верно") : TEXT("★ НЕТ"), M.NumFxGlowUnknown());
				}

				// К2: «Сброс» возвращает то, что лежало в АССЕТЕ (а не число эффекта)
				M.ResetEmitterField(R, *Fld);
				const float AfterReset = M.EmitterWork(R).IntensityCd;
				S += FString::Printf(TEXT("[ред-тест] К2 сброс поля: %.2f (ожидалось значение ассета %.2f) — %s\n"),
					AfterReset, Before, FMath::IsNearlyEqual(AfterReset, Before) ? TEXT("верно") : TEXT("★ РАСХОЖДЕНИЕ"));

				// ★★★ К1: ВОЗВРАТ ЭМИТТЕРА ЦЕЛИКОМ — И ПРОВЕРКА ДОЛГОВЕЧНОЙ КОПИИ ПОСЛЕ НЕГО.
				//   Прежде копия печаталась ДО возврата — то есть ровно в тот момент, когда она ещё
				//   заведомо в порядке. Неполный откат (запись с отменёнными числами остаётся в
				//   EmitterGlowSaved и воскресает в PostLoad) такой проверкой не ловился вовсе.
				M.RevertEmitterToAsset(R);
				const bool bBackSrc = M.EmitterSource(R) == Src0;
				bool bGlowBack = Cdo && Cdo->Emitters.Num() == Glow0.Num();
				if (bGlowBack)
				{
					UScriptStruct* GS = FAsdaFxEmitterGlow::StaticStruct();
					for (int32 i = 0; i < Glow0.Num() && bGlowBack; ++i)
					{
						bGlowBack = GS->CompareScriptStruct(&Glow0[i], &Cdo->Emitters[i].Glow, PPF_None);
					}
				}
				bool bSavedBack = Cdo && Cdo->EmitterGlowSaved.Num() == Saved0.Num();
				if (bSavedBack)
				{
					UScriptStruct* SS = FAsdaFxEmitterGlowSaved::StaticStruct();
					for (int32 i = 0; i < Saved0.Num() && bSavedBack; ++i)
					{
						bSavedBack = SS->CompareScriptStruct(&Saved0[i], &Cdo->EmitterGlowSaved[i], PPF_None);
					}
				}
				S += FString::Printf(TEXT("[ред-тест] К1 ПОСЛЕ возврата: «откуда числа» %s, эмиттеры %s, долговечная копия %d записей (до правки %d, в середине %d) — %s; отметка * %s\n"),
					bBackSrc ? TEXT("восстановлено") : TEXT("★ НЕ ВОССТАНОВЛЕНО"),
					bGlowBack ? TEXT("совпали") : TEXT("★ РАСХОДЯТСЯ"),
					Cdo ? Cdo->EmitterGlowSaved.Num() : -1, Saved0.Num(), SavedMid,
					bSavedBack ? TEXT("совпала") : TEXT("★ РАСХОДИТСЯ — откат НЕПОЛНЫЙ, отменённая правка воскреснет при загрузке"),
					M.IsEmitterPending(R) ? TEXT("★ ОСТАЛАСЬ") : TEXT("снята — верно"));
				bStateRestored = bBackSrc && bGlowBack && bSavedBack;
			}
			// ═══ ★★★ РЕВИЗИЯ Ф13 (С1, 20.09.2026): СТОРОЖ НЕ ТЕРЯЕТ ЗАПИСИ БЕЗ ЭМИТТЕРА-ХОЗЯИНА ═══
			//   Случай, который не проверялся вовсе: копия НЕ ПУСТА (Saved0 > 0) и часть её записей
			//   не сопоставляется ни одному нынешнему эмиттеру — так выглядит ассет после того, как
			//   сборщик перепечатал список из .jeb и переименовал эмиттеры. Прежняя авторитетная
			//   запись (любая правка в конфигураторе) такие записи выбрасывала — то есть чистила
			//   ровно то, ради чего сторож и заведён.
			//   Всё делается на ТРАНЗИТНОМ акторе своего мира: ни ассет, ни CDO не трогаются.
			{
				AAsdaEffectActor* Keep = W->SpawnActor<AAsdaEffectActor>(Cls, Sp);
				if (!Keep || Keep->Emitters.Num() == 0)
				{
					S += TEXT("[ред-тест] С1 сторож: проверка пропущена (экземпляр не создался или у эффекта нет эмиттеров)\n");
				}
				else
				{
					// Запись-сирота: ни имени, ни номера такого эмиттера в списке нет
					FAsdaFxEmitterGlowSaved Orphan;
					Orphan.Name = TEXT("__asda_orphan__");
					Orphan.JebType = 0x7F;
					Orphan.Index = Keep->Emitters.Num() + 5;
					Orphan.Glow.Source = EAsdaFxEmitterGlowSource::Manual;
					Orphan.Glow.Spec.IntensityCd = 777.f;
					Keep->EmitterGlowSaved.Reset();
					Keep->EmitterGlowSaved.Add(Orphan);
					const int32 SavedBefore = Keep->EmitterGlowSaved.Num();
					// ★ И ОДИН НАСТОЯЩИЙ НАСТРОЕННЫЙ ЭМИТТЕР: без него свежий снимок вышел бы пустым
					//   и сработал бы ДРУГОЙ сторож («пустой снимок копию не затирает»), а проверялась
					//   бы не та защита. Контрольная пара к случаю «снимок пуст»
					Keep->Emitters[0].Glow.Source = EAsdaFxEmitterGlowSource::Manual;
					Keep->Emitters[0].Glow.Spec.IntensityCd = 42.f;
					Keep->SaveEmitterGlow(/*bAuthoritative=*/true);
					bool bOrphanKept = false, bLiveWritten = false;
					for (const FAsdaFxEmitterGlowSaved& Rec : Keep->EmitterGlowSaved)
					{
						if (Rec.Name == Orphan.Name && FMath::IsNearlyEqual(Rec.Glow.Spec.IntensityCd, 777.f)) { bOrphanKept = true; }
						if (Rec.Index == 0 && FMath::IsNearlyEqual(Rec.Glow.Spec.IntensityCd, 42.f)) { bLiveWritten = true; }
					}
					S += FString::Printf(TEXT("[ред-тест] С1 сторож: копия до записи %d (вся — без эмиттера-хозяина), после АВТОРИТЕТНОЙ записи %d; сирота %s; настроенный эмиттер %s\n"),
						SavedBefore, Keep->EmitterGlowSaved.Num(),
						bOrphanKept ? TEXT("СОХРАНЕНА — верно") : TEXT("★ ПОТЕРЯНА: авторитетная запись всё ещё чистит уцелевшую копию"),
						bLiveWritten ? TEXT("записан — верно") : TEXT("★ НЕ ЗАПИСАН"));
					// Контрольная пара: тот же актор БЕЗ настроенных эмиттеров и НЕавторитетная запись —
					// прежний сторож обязан сработать и копию сохранить
					Keep->Emitters[0].Glow = FAsdaFxEmitterGlow();
					Keep->EmitterGlowSaved.Reset();
					Keep->EmitterGlowSaved.Add(Orphan);
					Keep->SaveEmitterGlow(/*bAuthoritative=*/false);
					S += FString::Printf(TEXT("[ред-тест] С1 контроль (нет настроенных эмиттеров, запись НЕавторитетная): копия %d записей — %s\n"),
						Keep->EmitterGlowSaved.Num(), Keep->EmitterGlowSaved.Num() > 0 ? TEXT("сохранена, верно") : TEXT("★ ОЧИЩЕНА"));
				}
			}

			// ═══ ★★★ ВОЛНА Ф14 (К-1, 20.09.2026): A/B «РУКА ВЛАДЕЛЬЦА ПРОТИВ ПЕРЕПЕЧКИ» ═════════
			//   ЧТО ДОКАЗЫВАЕТСЯ. Возврат эмиттера к умолчанию ЧЕРЕЗ ПАНЕЛЬ ДЕТАЛЕЙ обязан
			//   вычищать запись из долговечной копии, а перепечка эмиттеров сборщиком — НЕ обязана
			//   (её записи и есть единственный след настроек владельца).
			//   ЗОВЁТСЯ НАСТОЯЩИЙ ПУТЬ: AAsdaEffectActor::PostEditChangeProperty с тем же событием,
			//   какое строит движок. Различаются ветки ровно тем, чем они различаются в жизни:
			//     ВЕТКА A (рука владельца): Property = ЛИСТ структуры свечения, MemberProperty = Emitters;
			//     ВЕТКА B (сборщик):        Property = MemberProperty = Emitters (массив целиком).
			//   СТАРЫЙ ЗАКОН — тоже НАСТОЯЩИМ вызовом: прежнее место звало SaveEmitterGlow() без
			//   параметра, и это ровно `SaveEmitterGlow(false)`. Его последствие показано до конца:
			//   RestoreEmitterGlow возвращает ОТМЕНЁННОЕ число — именно так правка и терялась.
			//   Всё — на ТРАНЗИТНОМ акторе своего мира: ни ассет, ни CDO не трогаются.
			{
				AAsdaEffectActor* K1 = W->SpawnActor<AAsdaEffectActor>(Cls, Sp);
				FProperty* PropEmitters = FindFProperty<FProperty>(AAsdaEffectActor::StaticClass(),
					GET_MEMBER_NAME_CHECKED(AAsdaEffectActor, Emitters));
				FProperty* PropLeaf = FindFProperty<FProperty>(FAsdaFxEmitterGlow::StaticStruct(),
					GET_MEMBER_NAME_CHECKED(FAsdaFxEmitterGlow, Source));
				if (!K1 || K1->Emitters.Num() == 0 || !PropEmitters || !PropLeaf)
				{
					S += TEXT("[ред-тест] К-1 A/B: пропущено (нет экземпляра, эмиттеров или свойств для события правки)\n");
				}
				else
				{
					const float Mark = 99.f;   // число, которое владелец задал и потом отменил
					// Привести актор в состояние «настроен ровно ОДИН эмиттер, копия непуста»
					auto Arm = [K1, Mark]()
					{
						for (FAsdaFxEmitter& E : K1->Emitters) { E.Glow = FAsdaFxEmitterGlow(); }
						K1->Emitters[0].Glow.Source = EAsdaFxEmitterGlowSource::Manual;
						K1->Emitters[0].Glow.Spec.IntensityCd = Mark;
						// ★ Признак подхвата — как у настоящего настроенного эмиттера. Без него
						//   PostEditChangeProperty законно подхватит числа ЭФФЕКТА поверх наших
						//   (одноразовый подхват при переводе в «вручную», волна Ф8), и ветка B
						//   проверяла бы артефакт теста, а не продукт
						K1->Emitters[0].Glow.bSpecSeeded = true;
						K1->SaveEmitterGlow(/*bAuthoritative=*/true);   // копия = 1 запись
					};
					// Возврат «взято от эффекта» — то, что делает владелец в панели деталей
					auto ClearByHand = [K1]() { K1->Emitters[0].Glow = FAsdaFxEmitterGlow(); };

					// ── СТАРЫЙ ЗАКОН, ДОСЛОВНО: прежний вызов SaveEmitterGlow() без параметра ──
					Arm();
					const int32 ArmedCopy = K1->EmitterGlowSaved.Num();
					ClearByHand();
					K1->SaveEmitterGlow();                       // ← ровно прежняя строка места правки
					const int32 OldCopy = K1->EmitterGlowSaved.Num();
					K1->RestoreEmitterGlow();                    // ← что сделает PostLoad при следующей загрузке
					const float OldAfterLoad = K1->Emitters[0].Glow.Spec.IntensityCd;
					const bool bOldLost = K1->Emitters[0].Glow.Source == EAsdaFxEmitterGlowSource::Manual
						&& FMath::IsNearlyEqual(OldAfterLoad, Mark);
					S += FString::Printf(TEXT("[ред-тест] К-1 A/B СТАРЫЙ ЗАКОН (SaveEmitterGlow() без параметра): копия была %d -> после возврата %d; загрузка ассета вернула «откуда числа»=%s, силу %.2f — %s\n"),
						ArmedCopy, OldCopy,
						K1->Emitters[0].Glow.Source == EAsdaFxEmitterGlowSource::Manual ? TEXT("вручную") : TEXT("от эффекта"),
						OldAfterLoad,
						bOldLost ? TEXT("ПРАВКА ВЛАДЕЛЬЦА ТЕРЯЕТСЯ — дефект воспроизведён") : TEXT("★ ПРОВАЛ: дефект не воспроизводится, A/B бессмысленно"));

					// ═══ ★★★ ВОЛНА Ф15 (К-1): ПО ОДНОМУ СЛУЧАЮ НА КАЖДЫЙ ЖЕСТ ПАНЕЛИ ════════════
					//   ЧЕМ ЭТО ЛУЧШЕ ПРЕЖНИХ ВЕТОК A/B. Ф14 строила ровно ДВА события: «лист» и
					//   «массив целиком» — и подписывала второе «сборщик». Но ту же самую форму
					//   («массив целиком») панель деталей даёт на ВОСЕМЬ своих жестов, и потому
					//   ветка B ставила штамп «верно» ровно на дыре, из-за которой правка владельца
					//   и терялась. Теперь случай заводится на КАЖДЫЙ жест, а форма события берётся
					//   не из головы, а из того, что строит движок:
					//     свойство события: ЛИСТ структуры / ArrayProperty->Inner (узел ЭЛЕМЕНТА) /
					//       сам массив (узел массива) — PropertyNode.cpp:3402-3419;
					//     вид правки: ArrayAdd :1438 и :1722, ArrayRemove :1911, ArrayClear :1621,
					//       Duplicate :2403, ArrayMove (перетаскивание элемента), ValueSet и
					//       ValueSet|ResetToDefault :372-376 + :1162 — всё PropertyHandleImpl.cpp;
					//     скрипт (UE-Python set_editor_property): вид правки Unspecified и только он
					//       (PropertyAccessUtil.h:67, PyWrapperOwnerContext.cpp:55-80).
					//   ПРОВЕРЯЕТСЯ СОСТОЯНИЕ АССЕТА, А НЕ РЕШЕНИЕ: после каждого жеста копия
					//   обязана быть пустой (отменённая запись ушла), и «загрузка ассета»
					//   (RestoreEmitterGlow, как в PostLoad) не должна воскрешать отменённые числа.
					//   У пути СКРИПТА — наоборот: копия цела, числа возвращены немедленно.
					//   РЯДОМ ИЗМЕРЯЕТСЯ И СТАРЫЙ ЗАКОН Ф14 (`Leaf != Member` ПО ИМЕНАМ): видно,
					//   на каких именно жестах он врал.
					FArrayProperty* ArrEmitters = CastField<FArrayProperty>(PropEmitters);
					FProperty* PropInner = ArrEmitters ? ArrEmitters->Inner : nullptr;
					{
						// ═══ ★★★ ВОЛНА Ф16 (МЕЛК-2, 20.09.2026): ТЕСТ ОБЯЗАН СТРОИТЬ ТО, ЧТО СТРОИТ ДВИЖОК ══
						//   ЧТО БЫЛО НЕ ТАК. Случай «жест по ОДНОМУ элементу массива» Ф15 строила как
						//   `Property = ArrayProperty->Inner`. Движок в этом случае кладёт
						//   `Property == nullptr`, а не Inner, и вот откуда это видно по строкам:
						//     * панель зовёт PostEditChangeChainProperty, когда цепочка непуста
						//       (PropertyNode.cpp:3402-3419);
						//     * UObject::PostEditChangeChainProperty строит событие как
						//       `FPropertyChangedEvent PropertyEvent(PropertyChain.GetActiveNode() ?
						//        ...->GetValue() : nullptr, ChangeType)` (Obj.cpp:613-614), а
						//       MemberProperty ставит из головы цепочки (Obj.cpp:632-635);
						//     * в цепочку узел ЭЛЕМЕНТА не попадает вовсе: BuildPropertyChain
						//       пропускает узлы с ArrayIndex != INDEX_NONE и Inner-свойства
						//       (PropertyNode.cpp:3734-3737), поэтому SetActivePropertyNode(Inner)
						//       не находит узла (EditPropertyChain.cpp:19-31) и активный узел
						//       остаётся пустым.
						//   РЕШЕНИЕ ОТ ЭТОГО НЕ МЕНЯЕТСЯ (и nullptr, и Inner ≠ MemberProperty, то есть
						//   оба дают «панель»), дефекта в продукте нет — но тест, строящий НЕ ТУ форму,
						//   доказывает не то, что нужно. Теперь узел 1 — это `nullptr` как у движка, а
						//   узел 3 оставлен КОНТРОЛЬНОЙ ПАРОЙ: если какой-то путь всё же принесёт Inner,
						//   закон обязан сказать «панель» и там.
						// Какое свойство несёт узел события: 0 — лист структуры, 1 — элемент массива
						// (движок: nullptr), 2 — сам массив, 3 — контроль: ArrayProperty->Inner
						struct FGest { const TCHAR* Name; int32 Node; EPropertyChangeType::Type Type; bool bPanel; };
						const FGest Gestures[] =
						{
							{ TEXT("правка поля свечения (лист структуры)"), 0, EPropertyChangeType::ValueSet, true },
							{ TEXT("«Reset to Default» на ОДНОМ эмиттере"),  1, (EPropertyChangeType::Type)(EPropertyChangeType::ValueSet | EPropertyChangeType::ResetToDefault), true },
							{ TEXT("вставка (Paste) в ОДИН эмиттер"),        1, EPropertyChangeType::ValueSet, true },
							{ TEXT("«Reset to Default» на всём массиве"),    2, (EPropertyChangeType::Type)(EPropertyChangeType::ValueSet | EPropertyChangeType::ResetToDefault), true },
							{ TEXT("вставка текста в массив"),               2, EPropertyChangeType::ValueSet, true },
							{ TEXT("«+» (добавить эмиттер)"),                2, EPropertyChangeType::ArrayAdd, true },
							{ TEXT("вставка элемента (Insert)"),             2, EPropertyChangeType::ArrayAdd, true },
							{ TEXT("удаление элемента"),                     2, EPropertyChangeType::ArrayRemove, true },
							{ TEXT("дублирование элемента"),                 2, EPropertyChangeType::Duplicate, true },
							{ TEXT("очистка массива"),                       2, EPropertyChangeType::ArrayClear, true },
							{ TEXT("перемещение элемента"),                  2, EPropertyChangeType::ArrayMove, true },
							{ TEXT("КОНТРОЛЬ: узел с ArrayProperty->Inner"), 3, EPropertyChangeType::ValueSet, true },
							{ TEXT("СКРИПТ-сборщик: emitters целиком"),      2, EPropertyChangeType::Unspecified, false },
						};
						int32 OldLawWrong = 0;
						for (const FGest& G : Gestures)
						{
							// ★ Ф16 (МЕЛК-2): узел 1 — РОВНО то, что кладёт движок: nullptr
							FProperty* NodeProp = (G.Node == 0) ? PropLeaf : (G.Node == 1) ? nullptr
								: (G.Node == 3) ? PropInner : PropEmitters;
							if (G.Node != 1 && !NodeProp)
							{
								S += FString::Printf(TEXT("[ред-тест] К-1 жест «%s»: ★ ПРОВАЛ — свойство узла не найдено (Emitters не массив?)\n"), G.Name);
								continue;
							}
							Arm();
							ClearByHand();
							FPropertyChangedEvent Ev(NodeProp, G.Type);
							Ev.SetActiveMemberProperty(PropEmitters);
							// Как решает НОВЫЙ закон (та же функция, что зовёт сам актор)
							const bool bSaysPanel = AAsdaEffectActor::IsEmittersWriteFromDetailsPanel(Ev);
							// Как решал СТАРЫЙ закон Ф14 — сравнением ИМЁН свойств; пустое свойство
							// у него давало NAME_None (так его и звал актор)
							const bool bOldSaysPanel = ((NodeProp ? NodeProp->GetFName() : NAME_None) != PropEmitters->GetFName());
							if (bOldSaysPanel != G.bPanel) { ++OldLawWrong; }
							K1->PostEditChangeProperty(Ev);
							const int32 CopyNow = K1->EmitterGlowSaved.Num();
							K1->RestoreEmitterGlow();   // ← ровно то, что сделает PostLoad при загрузке ассета
							const bool bManualNow = K1->Emitters[0].Glow.Source == EAsdaFxEmitterGlowSource::Manual
								&& FMath::IsNearlyEqual(K1->Emitters[0].Glow.Spec.IntensityCd, Mark);
							const bool bOk = G.bPanel ? (CopyNow == 0 && !bManualNow) : (CopyNow > 0 && bManualNow);
							S += FString::Printf(TEXT("[ред-тест] К-1 жест «%s» (узел=%s вид=0x%X): новый закон сказал «%s», копия %d, после загрузки эмиттер %s; старый закон Ф14 сказал «%s»%s — %s\n"),
								G.Name,
								(G.Node == 0) ? TEXT("лист") : (G.Node == 1) ? TEXT("элемент (Property=nullptr, как у движка)")
									: (G.Node == 3) ? TEXT("элемент (Inner — контрольная пара)") : TEXT("массив"),
								(uint32)G.Type,
								bSaysPanel ? TEXT("панель") : TEXT("скрипт"), CopyNow,
								bManualNow ? TEXT("с отменёнными числами") : TEXT("без своих чисел"),
								bOldSaysPanel ? TEXT("панель") : TEXT("скрипт"),
								(bOldSaysPanel == G.bPanel) ? TEXT("") : TEXT(" ← ЗДЕСЬ ОН ВРАЛ"),
								(bOk && bSaysPanel == G.bPanel) ? TEXT("верно") : TEXT("★ ПРОВАЛ: состояние ассета после жеста неверно"));
						}
						S += FString::Printf(TEXT("[ред-тест] К-1 итог A/B: старый закон Ф14 (`Leaf != Member` по именам) ошибся на %d жест(ах) из %d — %s\n"),
							OldLawWrong, (int32)UE_ARRAY_COUNT(Gestures),
							OldLawWrong > 0 ? TEXT("дефект воспроизведён") : TEXT("★ ПРОВАЛ: дефект не воспроизводится, A/B бессмысленно"));
					}

					// ── ГЕЙТ СО СТОРОНЫ СКРИПТА: сильнее любой догадки о форме события ──
					//   Ключ `asda.Fx.Glow.EmittersWriter` — единственный признак, не зависящий от
					//   формы события вовсе. Проверяем ОБА принудительных значения на форме, которую
					//   они обязаны перебить: 1 на событии панели и 0 на событии скрипта
					if (IConsoleVariable* CVarW = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fx.Glow.EmittersWriter")))
					{
						// ═══ ★★★ ВОЛНА Ф16: ПРИОРИТЕТ УСТАНОВКИ КЛЮЧА — ЭТО НЕ МЕЛОЧЬ ═════════════
						//   Ф15 ставила значение с `ECVF_SetByCode`, и прогон Ф16 поймал, чем это
						//   плохо: сборщик (и питоновская часть самопроверки) поднимает ключ
						//   КОНСОЛЬНОЙ командой, то есть с приоритетом SetByConsole. После этого
						//   запись SetByCode движок молча ИГНОРИРУЕТ (приоритет ниже уже
						//   установленного), и тест «гейт не работает» ★ПРОВАЛивался на ровном
						//   месте — причём не из-за продукта. Ставим тем же приоритетом, каким
						//   ключ поднимает сборщик, и ПРОВЕРЯЕМ ЧТЕНИЕМ ОБРАТНО.
						const int32 WasW = CVarW->GetInt();
						FPropertyChangedEvent EvPanel(PropLeaf, EPropertyChangeType::ValueSet);
						EvPanel.SetActiveMemberProperty(PropEmitters);
						FPropertyChangedEvent EvScript(PropEmitters, EPropertyChangeType::Unspecified);
						EvScript.SetActiveMemberProperty(PropEmitters);
						CVarW->Set(1, ECVF_SetByConsole);
						const bool bSet1 = CVarW->GetInt() == 1;
						const bool bForcedScript = !AAsdaEffectActor::IsEmittersWriteFromDetailsPanel(EvPanel);
						CVarW->Set(0, ECVF_SetByConsole);
						const bool bSet0 = CVarW->GetInt() == 0;
						const bool bForcedPanel = AAsdaEffectActor::IsEmittersWriteFromDetailsPanel(EvScript);
						CVarW->Set(WasW, ECVF_SetByConsole);
						S += FString::Printf(TEXT("[ред-тест] К-1 гейт asda.Fx.Glow.EmittersWriter: значение 1 встало %s и на событии панели даёт «скрипт» %s; значение 0 встало %s и на событии скрипта даёт «панель» %s; ключ возвращён в %d — %s\n"),
							bSet1 ? TEXT("да") : TEXT("★ НЕТ"),
							bForcedScript ? TEXT("да") : TEXT("★ НЕТ"),
							bSet0 ? TEXT("да") : TEXT("★ НЕТ"),
							bForcedPanel ? TEXT("да") : TEXT("★ НЕТ"),
							CVarW->GetInt(),
							(bSet1 && bSet0 && bForcedScript && bForcedPanel) ? TEXT("верно: гейт сильнее формы события") : TEXT("★ ПРОВАЛ: гейт не работает"));
					}
					else
					{
						S += TEXT("[ред-тест] К-1 гейт: ★ ПРОВАЛ — ключ asda.Fx.Glow.EmittersWriter не зарегистрирован\n");
					}

					// ═══ ★★★ ВОЛНА Ф17 (С-2): ЗАЛИПШИЙ ГЕЙТ ПЕРЕСТАЛ МОЛЧАТЬ ═══════════════════
					//   ЧТО ДОКАЗЫВАЕТСЯ. При ключе, ОСТАВЛЕННОМ В 1, IsEmittersWriteFromDetailsPanel
					//   отвечает «скрипт» первой же строкой, RestoreEmitterGlow возвращает прежние
					//   числа, и правка владельца в панели исчезает. Предупреждение при этом не
					//   печаталось вовсе: старое условие требовало ключ < 0
					//   (AsdaEffectActor.cpp, ветка Ф16/СРЕДН-1), а единственная строка на этом
					//   пути — Verbose, по умолчанию выключенная. Симптом был НЕНАБЛЮДАЕМ.
					//   ВЕТКА A — старый закон дословно: `!bFromPanel && Гейт < 0`.
					//   ВЕТКА B — нынешний: `Гейт == 1 && человеческая форма` ⇒ Warning + событие
					//     для видимого сообщения; повтор по тому же актору НЕ шлётся.
					//   КОНТРОЛЬНЫЕ ПАРЫ: (C) гейт не поднят — молчим; (D) гейт поднят, но форма
					//     СКРИПТОВАЯ (так пишет настоящий сборщик) — тоже молчим, иначе каждый
					//     законный прогон сборщика заваливал бы владельца сообщениями.
					if (IConsoleVariable* CVarG = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fx.Glow.EmittersWriter")))
					{
						const int32 WasG = CVarG->GetInt();
						// Счётчик событий «подозрение на залипший гейт» — по нему и видно, молчал ли продукт
						int32 Fired = 0;
						FDelegateHandle H = AAsdaEffectActor::OnEmittersWriterGateSuspect().AddLambda(
							[&Fired](AAsdaEffectActor* /*Fx*/, int32 /*Gate*/) { ++Fired; });
						// Событие человеческой формы: правится ЛИСТ структуры, вид ValueSet
						FPropertyChangedEvent EvHuman(PropLeaf, EPropertyChangeType::ValueSet);
						EvHuman.SetActiveMemberProperty(PropEmitters);
						// Событие скриптовой формы: массив целиком, вид Unspecified
						FPropertyChangedEvent EvBot(PropEmitters, EPropertyChangeType::Unspecified);
						EvBot.SetActiveMemberProperty(PropEmitters);

						// ── СИМПТОМ: при поднятом ключе правка в панели и вправду откатывается ──
						CVarG->Set(1, ECVF_SetByConsole);
						Arm();                                   // эмиттер 0 настроен, копия = 1 запись
						ClearByHand();                           // владелец вернул «взято от эффекта»
						// ★ Ф19 (С-1): «этому эффекту ещё не говорили» — теперь это НОМЕР ЭПОХИ, а не bool
						K1->GlowWriterGateSuspectEpoch = INDEX_NONE;
						Fired = 0;
						K1->PostEditChangeProperty(EvHuman);
						const bool bRolledBack = K1->Emitters[0].Glow.Source == EAsdaFxEmitterGlowSource::Manual
							&& FMath::IsNearlyEqual(K1->Emitters[0].Glow.Spec.IntensityCd, Mark);
						const int32 FiredFirst = Fired;
						// Повтор по тому же актору — сообщение одноразовое
						K1->PostEditChangeProperty(EvHuman);
						const int32 FiredAfterRepeat = Fired;
						// Старый закон на этом же событии: условие `!bFromPanel && Гейт < 0`
						const bool bOldWouldWarn = (!AAsdaEffectActor::IsEmittersWriteFromDetailsPanel(EvHuman))
							&& (CVarG->GetInt() < 0);
						S += FString::Printf(TEXT("[ред-тест] С-2/Ф17 ВЕТКА A+B (ключ поднят в 1, правка человеческой формы): правка молча откатилась %s; старый закон предупредил бы %s; новый закон выдал событий %d, после повтора %d — %s\n"),
							bRolledBack ? TEXT("да — симптом воспроизведён") : TEXT("★ НЕТ (симптома нет, A/B бессмысленно)"),
							bOldWouldWarn ? TEXT("★ да (дефект не воспроизводится)") : TEXT("НЕТ — дефект воспроизведён: симптом был ненаблюдаем"),
							FiredFirst, FiredAfterRepeat,
							(bRolledBack && !bOldWouldWarn && FiredFirst == 1 && FiredAfterRepeat == 1)
								? TEXT("верно: залипший гейт говорит о себе ровно один раз на актор")
								: TEXT("★ ПРОВАЛ: симптом залипшего гейта по-прежнему не наблюдаем"));

						// ── КОНТРОЛЬ C: ключ НЕ поднят — ложной тревоги быть не должно ──
						CVarG->Set(-1, ECVF_SetByConsole);
						K1->GlowWriterGateSuspectEpoch = INDEX_NONE;
						Fired = 0;
						Arm();
						K1->PostEditChangeProperty(EvHuman);
						const int32 FiredNoGate = Fired;

						// ── КОНТРОЛЬ D: ключ поднят, но форма СКРИПТОВАЯ (законный прогон сборщика) ──
						CVarG->Set(1, ECVF_SetByConsole);
						K1->GlowWriterGateSuspectEpoch = INDEX_NONE;
						Fired = 0;
						Arm();
						K1->PostEditChangeProperty(EvBot);
						const int32 FiredBot = Fired;

						// ═══ ★★★ ВОЛНА Ф18 (С-1): ВТОРОЕ ЗАЛИПАНИЕ В ТОМ ЖЕ СЕАНСЕ ═══════════
						//   ЧТО ДОКАЗЫВАЕТСЯ. Признак «уже предупредили» держит сообщение
						//   одноразовым, ПОКА ГЕЙТ ПОДНЯТ, а не весь сеанс. Владелец за сеанс
						//   гоняет сборщик не один раз: гейт поднимается и опускается снова и
						//   снова, и ВТОРОЕ залипание обязано сказать о себе так же громко, как
						//   первое. До этой волны сброса признака в ПРОДУКТЕ не было вовсе (только
						//   в этом тесте), и второе залипание уходило в Verbose — то есть молчало.
						//   ★ НИ ОДНОГО РУЧНОГО СБРОСА ВНУТРИ ОПЫТА: признак снимает сам продукт,
						//     иначе тест проверял бы собственную строку, а не закон.
						//   ВЕТКА A — старый закон Ф17 в виде теневой модели (самого кода больше
						//     нет, звать нечего): признак ставится один раз и не снимается никогда.
						//   ВЕТКА B — нынешний продукт на той же последовательности жестов.
						bool bOldFlag = false;
						int32 OldFired = 0;
						auto OldLaw = [&bOldFlag, &OldFired](int32 Gate, bool bHuman)
						{
							// дословно ветка Ф17: сброса нет ни на каком пути
							if (Gate == 1 && bHuman && !bOldFlag) { bOldFlag = true; ++OldFired; }
						};
						CVarG->Set(1, ECVF_SetByConsole);
						K1->GlowWriterGateSuspectEpoch = INDEX_NONE;   // ← НАЧАЛО опыта, а не сброс внутри него
						Fired = 0;
						Arm();
						K1->PostEditChangeProperty(EvHuman);            // первое залипание
						const int32 FiredStick1 = Fired;
						OldLaw(1, true);
						// Сборщик закончил и ОПУСТИЛ ключ; владелец правит дальше — ложной тревоги
						// быть не должно, а подозрение обязано сняться силами продукта.
						// ★ Ф19 (С-1): «снято» теперь значит «эпоха гейта ушла вперёд от той, в
						//   которой предупреждали». Снимает это сам ключ (колбэк), а не правка.
						CVarG->Set(-1, ECVF_SetByConsole);
						Arm();
						K1->PostEditChangeProperty(EvHuman);
						const bool bClearedByProduct =
							(K1->GlowWriterGateSuspectEpoch != AAsdaEffectActor::GetGlowWriterGateEpoch());
						const int32 FiredLowered = Fired;
						OldLaw(-1, true);
						// Ключ залип ВТОРОЙ раз в том же сеансе на том же эффекте
						CVarG->Set(1, ECVF_SetByConsole);
						Arm();
						K1->PostEditChangeProperty(EvHuman);
						const int32 FiredStick2 = Fired;
						OldLaw(1, true);
						S += FString::Printf(TEXT("[ред-тест] С-1/Ф18 второе залипание в том же сеансе: первое дало событий %d; после опускания ключа подозрение снято продуктом %s и ложной тревоги нет (событий по-прежнему %d); второе залипание дало событий всего %d. Старый закон Ф17 на той же последовательности выдал бы %d — %s\n"),
							FiredStick1,
							bClearedByProduct ? TEXT("да") : TEXT("★ НЕТ"),
							FiredLowered, FiredStick2, OldFired,
							(FiredStick1 == 1 && bClearedByProduct && FiredLowered == 1 && FiredStick2 == 2 && OldFired == 1)
								? TEXT("верно: дефект воспроизведён (старый закон промолчал бы) и закрыт")
								: TEXT("★ ПРОВАЛ: второе залипание по-прежнему молчит"));

						// ═══ ★★★ ВОЛНА Ф19 (С-1): ЭФФЕКТ, КОТОРЫЙ МЕЖДУ ЗАЛИПАНИЯМИ НЕ ТРОГАЛИ ═══
						//   ЧТО ДОКАЗЫВАЕТСЯ. Закон Ф18 снимал признак ТОЛЬКО на записи в ЭТОТ
						//   актор при гейте ≠ 1. Такой записи в жизни почти не бывает: законный
						//   писатель — сборщик, и он держит гейт ПОДНЯТЫМ всё время работы
						//   (Tools/asda_fx_common.py::emitters_writer_gate), то есть его перепечка
						//   приходит при гейте 1 и признак не снимает. Значит снять признак могла
						//   только рука владельца, заглянувшая именно в этот эффект между двумя
						//   залипаниями. Сценарий дыры — когда она НЕ заглядывала:
						//     гейт залип → правка X → предупреждение → ключ опущен →
						//     X БОЛЬШЕ НЕ ТРОГАЛИ → ключ залип снова → правка X ⇒ молчание.
						//   Этим тест отличается от предыдущего: там между залипаниями стояла
						//   правка при опущенном ключе, здесь её НЕТ ВОВСЕ.
						//   ВЕТКА A — теневая модель закона Ф18 (сброс по записи при гейте ≠ 1).
						//   ВЕТКА B — нынешний продукт (сброс по эпохе ключа).
						//   ★ КОНТРОЛЬ: третья правка в той же (второй) эпохе обязана молчать —
						//     иначе «лечение» свелось бы к отмене одноразовости.
						{
							bool bF18Flag = false;         // признак закона Ф18
							int32 F18Fired = 0;            // сколько раз он бы предупредил
							auto F18Law = [&bF18Flag, &F18Fired](int32 Gate, bool bWriteSeen, bool bHuman)
							{
								// дословно Ф18: сброс — на ЛЮБОЙ записи в этот актор при гейте ≠ 1
								if (bWriteSeen && Gate != 1) { bF18Flag = false; }
								if (Gate == 1 && bHuman && !bF18Flag) { bF18Flag = true; ++F18Fired; }
							};
							// (1) ПЕРВОЕ ЗАЛИПАНИЕ
							CVarG->Set(1, ECVF_SetByConsole);
							K1->GlowWriterGateSuspectEpoch = INDEX_NONE;   // начало опыта
							Fired = 0;
							Arm();
							K1->PostEditChangeProperty(EvHuman);
							F18Law(1, true, true);
							const int32 F19First = Fired;
							// (2) КЛЮЧ ОПУЩЕН — и эффекта X НИКТО НЕ ТРОГАЕТ (ни правки, ни сборщика)
							CVarG->Set(-1, ECVF_SetByConsole);
							// намеренно НИ ОДНОГО PostEditChangeProperty по K1 в этой точке
							// (3) КЛЮЧ ЗАЛИП ВТОРОЙ РАЗ — сборщик поднял его и не вернул
							CVarG->Set(1, ECVF_SetByConsole);
							F18Law(1, false, true);        // для модели Ф18: записи между не было
							Arm();
							K1->PostEditChangeProperty(EvHuman);
							const int32 F19Second = Fired;
							// (4) КОНТРОЛЬ: ещё одна правка в той же эпохе — молчание
							Arm();
							K1->PostEditChangeProperty(EvHuman);
							const int32 F19Repeat = Fired;
							S += FString::Printf(TEXT("[ред-тест] С-1/Ф19 «эффект между залипаниями НЕ трогали»: первое залипание дало событий %d; ключ опущен и снова поднят БЕЗ единой правки этого эффекта; второе залипание дало событий всего %d; повтор в той же эпохе %d (эпоха гейта %d). Закон Ф18 на той же последовательности выдал бы %d — %s\n"),
								F19First, F19Second, F19Repeat,
								AAsdaEffectActor::GetGlowWriterGateEpoch(), F18Fired,
								(F19First == 1 && F19Second == 2 && F19Repeat == 2 && F18Fired == 1)
									? TEXT("верно: дефект воспроизведён (закон Ф18 промолчал бы) и закрыт эпохой ключа")
									: TEXT("★ ПРОВАЛ: залипание после нетронутого эффекта по-прежнему молчит"));
						}

						// ═══ ★★★ ВОЛНА Ф20 (С-1): ВЛАДЕЛЕЦ ПРОПУСТИЛ ЕДИНСТВЕННОЕ ОКНО ═══════════
						//   ЧТО ДОКАЗЫВАЕТСЯ. Закон Ф19 давал ровно ОДНО предупреждение на эффект
						//   за эпоху гейта. Эпоха меняется только опусканием ключа — а если ключ
						//   никто не опускает (сборщик упал, владелец поднял и забыл), то после
						//   единственного сообщения наступает тишина НАВСЕГДА. Всплывающее окно
						//   живёт 15 секунд; владелец, который его пропустил и продолжил перебирать
						//   ТОТ ЖЕ эффект, дальше теряет каждую правку молча.
						//   ВЕТКА A — теневая модель закона Ф19 (только эпоха, времени не знает).
						//   ВЕТКА B — нынешний продукт (эпоха ИЛИ истёкший интервал).
						//   ★ ВРЕМЯ ЗДЕСЬ НАСТОЯЩЕЕ: интервал укорачивается ключом
						//     `asda.Fx.Glow.GateWarnRepeatSec`, и опыт ЖДЁТ его реально
						//     (FPlatformProcess::Sleep). Подтасовки поля GlowWriterGateSuspectTimeSec
						//     нет: иначе проверялась бы арифметика теста, а не то, что продукт
						//     смотрит на часы.
						//   ★ ПРИОРИТЕТ УСТАНОВКИ — ECVF_SetByConsole, тот же, что у гейта: у
						//     консольной переменной установка меньшим приоритетом отбрасывается
						//     молча и без колбэка (ConsoleManager.cpp:274-312).
						if (IConsoleVariable* CVarR = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fx.Glow.GateWarnRepeatSec")))
						{
							const float WasR = CVarR->GetFloat();
							const float DefR = AAsdaEffectActor::GetGlowWriterGateWarnRepeatSec();
							// ── ОПЫТ 1: ПОТОК СОБЫТИЙ НЕ ДОЛЖЕН РОЖДАТЬ ПОТОК ОКОН ──
							//   Интервал — БОЕВОЙ (тот, что стоит у продукта). Перетаскивание
							//   ползунка изображается пачкой правок подряд: все они укладываются в
							//   миллисекунды, то есть в одно окно интервала, и обязаны дать РОВНО
							//   одно сообщение. Длительность пачки печатается числом — по нему и
							//   видно, что опыт не «подкрался» к границе интервала.
							CVarG->Set(1, ECVF_SetByConsole);
							K1->GlowWriterGateSuspectEpoch = INDEX_NONE;   // начало опыта
							K1->GlowWriterGateSuspectTimeSec = -1.0;
							K1->GlowWriterGateSuspectMuted = 0;
							Fired = 0;
							const int32 BurstN = 150;
							const double TBurst0 = FPlatformTime::Seconds();
							for (int32 i = 0; i < BurstN; ++i) { Arm(); K1->PostEditChangeProperty(EvHuman); }
							const double BurstSec = FPlatformTime::Seconds() - TBurst0;
							const int32 FiredBurst = Fired;
							const int32 MutedBurst = K1->GlowWriterGateSuspectMuted;

							// ── ОПЫТ 2: ЧЕРЕЗ ИНТЕРВАЛ ПРЕДУПРЕЖДЕНИЕ ОБЯЗАНО ВЕРНУТЬСЯ ──
							//   Интервал укорачивается до 0.30 с, чтобы опыт не стоял полминуты.
							//   Ключ не опускается ни разу: эпоха всё та же, и закон Ф19 здесь
							//   промолчал бы навсегда.
							const float ShortR = 0.30f;
							CVarR->Set(ShortR, ECVF_SetByConsole);
							K1->GlowWriterGateSuspectEpoch = INDEX_NONE;   // начало опыта
							K1->GlowWriterGateSuspectTimeSec = -1.0;
							K1->GlowWriterGateSuspectMuted = 0;
							Fired = 0;
							const int32 EpochAtStart = AAsdaEffectActor::GetGlowWriterGateEpoch();
							// теневая модель закона Ф19: знает только эпоху
							int32 F19Epoch = INDEX_NONE;
							int32 F19Fired = 0;
							auto F19Law = [&F19Epoch, &F19Fired](int32 EpochNow)
							{
								if (F19Epoch != EpochNow) { F19Epoch = EpochNow; ++F19Fired; }
							};
							Arm(); K1->PostEditChangeProperty(EvHuman);     // владелец правит — сообщение есть
							F19Law(AAsdaEffectActor::GetGlowWriterGateEpoch());
							const int32 FiredFirstShort = Fired;
							// ...и не заметил его. Продолжает править ТОТ ЖЕ эффект внутри интервала.
							for (int32 i = 0; i < 3; ++i)
							{
								Arm(); K1->PostEditChangeProperty(EvHuman);
								F19Law(AAsdaEffectActor::GetGlowWriterGateEpoch());
							}
							const int32 FiredInsideWindow = Fired;
							const int32 MutedInsideWindow = K1->GlowWriterGateSuspectMuted;
							// НАСТОЯЩЕЕ ОЖИДАНИЕ: окно интервала закрылось
							FPlatformProcess::Sleep(ShortR + 0.10f);
							Arm(); K1->PostEditChangeProperty(EvHuman);
							F19Law(AAsdaEffectActor::GetGlowWriterGateEpoch());
							const int32 FiredAfterWait = Fired;
							const bool bEpochUnchanged =
								(AAsdaEffectActor::GetGlowWriterGateEpoch() == EpochAtStart);

							// ── КОНТРОЛЬ ПАРОЙ: интервал ≤ 0 отключает повтор по времени ──
							//   Путь отступления владельцу («сообщения навязчивы») обязан работать.
							//   ★★★ ПОПРАВКА Ф21 (20.09.2026): ЗДЕСЬ СТОЯЛО ОЖИДАНИЕ 0.05 с — И КОНТРОЛЬ
							//     БЫЛ ПУСТ. Действовавший до этой точки интервал — ShortR = 0.30 с; за
							//     0.05 с он не истекает и сам по себе, без всякого нуля. Значит молчание
							//     получалось ОДИНАКОВО и при работающем ключе, и при полностью
							//     проигнорированном: исход контроля не зависел от того, что он проверял.
							//     Ждать надо ДОЛЬШЕ прежнего интервала — тогда у молчания остаётся ровно
							//     одно объяснение, нуль.
							//   ★ ВТОРАЯ ПОЛОВИНА ПАРЫ — доказательство, что ожидание и вправду было
							//     достаточным: интервал возвращается в ShortR, и ТА ЖЕ правка без единой
							//     новой паузы обязана ПРОЗВУЧАТЬ (с прошлого предупреждения прошло
							//     OffWaitSec > ShortR). Без этой половины «достаточность» ожидания была бы
							//     арифметикой в комментарии, а не измерением.
							const float OffWaitSec = ShortR + 0.15f;   // 0.45 с — заведомо больше ShortR (0.30 с)
							CVarR->Set(0.f, ECVF_SetByConsole);
							FPlatformProcess::Sleep(OffWaitSec);
							Arm(); K1->PostEditChangeProperty(EvHuman);
							const int32 FiredOff = Fired;              // при нуле счётчик обязан ОСТАТЬСЯ прежним
							// Интервал вернули — то же, уже отсчитанное ожидание обязано сработать
							CVarR->Set(ShortR, ECVF_SetByConsole);
							Arm(); K1->PostEditChangeProperty(EvHuman);
							const int32 FiredBack = Fired;             // обязан вырасти ровно на единицу

							CVarR->Set(WasR, ECVF_SetByConsole);
							S += FString::Printf(TEXT("[ред-тест] С-1/Ф20 «владелец пропустил единственное окно»: умолчание интервала %.1f с. ОПЫТ 1 (боевой интервал, пачка из %d правок за %.3f с): событий %d, подавлено %d — поток окон не родился. ОПЫТ 2 (интервал %.2f с, ключ НЕ опускался, эпоха %d→%s): первая правка дала %d, ещё три внутри окна — по-прежнему %d (подавлено %d), после ожидания %.2f с стало %d. ВЕТКА A (закон Ф19, только эпоха) на той же последовательности выдала бы %d. КОНТРОЛЬ ПАРОЙ (интервал опущен в 0 = повтор по времени выключен; ждали %.2f с — ДОЛЬШЕ прежнего интервала %.2f с, так что при проигнорированном ключе сообщение обязано было бы вернуться): счётчик событий ОСТАЛСЯ %d, новых сообщений 0; затем интервал возвращён в %.2f с и та же правка без единой новой паузы дала счётчик %d, новых сообщений %d — %s\n"),
								DefR, BurstN, BurstSec, FiredBurst, MutedBurst,
								ShortR, EpochAtStart, bEpochUnchanged ? TEXT("та же") : TEXT("★ сменилась"),
								FiredFirstShort, FiredInsideWindow, MutedInsideWindow,
								ShortR + 0.10f, FiredAfterWait, F19Fired,
								OffWaitSec, ShortR, FiredOff, ShortR, FiredBack, FiredBack - FiredOff,
								(FiredBurst == 1 && MutedBurst == BurstN - 1 && BurstSec < (double)DefR
								 && FiredFirstShort == 1 && FiredInsideWindow == 1 && MutedInsideWindow == 3
								 && FiredAfterWait == 2 && bEpochUnchanged && F19Fired == 1
								 && FiredOff == FiredAfterWait && FiredBack == FiredAfterWait + 1)
									? TEXT("верно: дефект воспроизведён (закон Ф19 сказал бы один раз и замолчал), закрыт повтором по времени, поток событий по-прежнему даёт одно окно, отключение работает — и контроль это РАЗЛИЧАЕТ: на том же ожидании возвращённый интервал сразу заговорил")
									: TEXT("★ ПРОВАЛ: повтор по времени не работает, превратился в поток сообщений или не отключается нулём"));
							// ★ ВЕРНУТЬ АКТОР В ТО СОСТОЯНИЕ, КОТОРОГО ЖДЁТ СЛЕДУЮЩИЙ ТЕСТ: «K1 в этой
							//   эпохе уже предупреждал, и было это только что». Обнулять номер эпохи
							//   здесь НЕЛЬЗЯ — опыт «одноразовость на актор, а не на модуль» ниже
							//   проверяет как раз молчание K1 при говорящем K2.
							K1->GlowWriterGateSuspectEpoch = AAsdaEffectActor::GetGlowWriterGateEpoch();
							K1->GlowWriterGateSuspectTimeSec = FPlatformTime::Seconds();
							K1->GlowWriterGateSuspectMuted = 0;
						}
						else
						{
							S += TEXT("[ред-тест] С-1/Ф20: ★ ПРОВАЛ — ключ asda.Fx.Glow.GateWarnRepeatSec не зарегистрирован\n");
						}

						// ═══ ★★★ ВОЛНА Ф18 (С-1): ОДНОРАЗОВОСТЬ — НА АКТОР, А НЕ НА МОДУЛЬ ═════
						//   Весь блок С-2 гонялся на ОДНОМ акторе (K1), и «один раз на актор за
						//   сеанс» оставалось непроверенным утверждением: ровно так же вёл бы себя
						//   признак, будь он статиком на весь модуль (грабля МЕЛК-4 этой же волны).
						//   Заводим ВТОРОЙ эффект и смотрим: у него счёт свой.
						{
							AAsdaEffectActor* K2 = W->SpawnActor<AAsdaEffectActor>(Cls, Sp);
							if (!K2)
							{
								S += TEXT("[ред-тест] С-1/Ф18 другой актор: пропущено (второй экземпляр не завёлся)\n");
							}
							else
							{
								// Ключ уже поднят (залипание длится), K1 своё сообщение отдал
								const int32 FiredBeforeK2 = Fired;
								K2->PostEditChangeProperty(EvHuman);          // K2 ещё не жаловался — обязан сказать
								const int32 FiredK2First = Fired;
								K2->PostEditChangeProperty(EvHuman);          // повтор по K2 — молчит
								const int32 FiredK2Repeat = Fired;
								K1->PostEditChangeProperty(EvHuman);          // K1 уже сказал — тоже молчит
								const int32 FiredK1Again = Fired;
								// ★ Ф19 (С-1): «признак стоит» = номер эпохи у актора совпал с текущей
								const int32 EpochNow = AAsdaEffectActor::GetGlowWriterGateEpoch();
								const bool bK1Marked = (K1->GlowWriterGateSuspectEpoch == EpochNow);
								const bool bK2Marked = (K2->GlowWriterGateSuspectEpoch == EpochNow);
								S += FString::Printf(TEXT("[ред-тест] С-1/Ф18 другой актор при том же залипании: до K2 событий %d, первая правка K2 дала %d, повтор по K2 %d, повтор по K1 %d; эпоха гейта %d, отметка у K1 %s, у K2 %s — %s\n"),
									FiredBeforeK2, FiredK2First, FiredK2Repeat, FiredK1Again, EpochNow,
									bK1Marked ? TEXT("стоит") : TEXT("★ снята"),
									bK2Marked ? TEXT("стоит") : TEXT("★ снята"),
									(FiredK2First == FiredBeforeK2 + 1 && FiredK2Repeat == FiredK2First
									 && FiredK1Again == FiredK2First
									 && bK1Marked && bK2Marked)
										? TEXT("верно: счёт у каждого эффекта свой, одноразовость на актор")
										: TEXT("★ ПРОВАЛ: одноразовость общая на модуль или не работает"));
								K2->GlowWriterGateSuspectEpoch = INDEX_NONE;
							}
						}

						AAsdaEffectActor::OnEmittersWriterGateSuspect().Remove(H);
						CVarG->Set(WasG, ECVF_SetByConsole);
						K1->GlowWriterGateSuspectEpoch = INDEX_NONE;
						S += FString::Printf(TEXT("[ред-тест] С-2/Ф17 контроли: ключ -1 + человеческая форма дала событий %d; ключ 1 + скриптовая форма (настоящий сборщик) дала событий %d; ключ возвращён в %d — %s\n"),
							FiredNoGate, FiredBot, CVarG->GetInt(),
							(FiredNoGate == 0 && FiredBot == 0 && CVarG->GetInt() == WasG)
								? TEXT("верно: ложных тревог нет, законная перепечка молчит")
								: TEXT("★ ПРОВАЛ: предупреждение срабатывает не там, где должно"));
					}
					else
					{
						S += TEXT("[ред-тест] С-2/Ф17: ★ ПРОВАЛ — ключ asda.Fx.Glow.EmittersWriter не зарегистрирован\n");
					}

					// ═══ ★★★ ВОЛНА Ф16 (СРЕДН-1): «KEEP SIMULATION CHANGES» — ЖЕСТ В ФОРМЕ СКРИПТА ═══
					//   ЧТО ДОКАЗЫВАЕТСЯ. Событие, которое строит Editor.cpp:1773 одноаргументным
					//   конструктором (`FPropertyChangedEvent PropertyChangedEvent(Property)`),
					//   неотличимо по форме от записи UE-Python: Property == MemberProperty ==
					//   Emitters, вид правки Unspecified. Единственное, чем оно отличается, —
					//   обстоятельство: этот путь достижим ТОЛЬКО при работающей симуляции
					//   (LevelEditorActions.cpp:2726 `GEditor->PlayWorld != NULL`).
					//   A/B: ОДНО И ТО ЖЕ событие при выключенной и включённой симуляции.
					//   ★ Признак подделывается честно: `GEditor->PlayWorld` — обычное поле
					//     редактора, мы ставим в него ПУСТОЙ транзитный мир и тут же возвращаем
					//     прежнее значение. Мир не тикается и никуда не попадает.
					if (GEditor)
					{
						FPropertyChangedEvent EvKeepSim(PropEmitters);   // ровно как Editor.cpp:1773
						EvKeepSim.SetActiveMemberProperty(PropEmitters);
						UWorld* const PrevPlay = GEditor->PlayWorld;
						GEditor->PlayWorld = nullptr;
						const bool bNoSim = AAsdaEffectActor::IsEmittersWriteFromDetailsPanel(EvKeepSim);
						UWorld* FakePlay = NewObject<UWorld>(GetTransientPackage(), NAME_None, RF_Transient);
						GEditor->PlayWorld = FakePlay;
						const bool bWithSim = AAsdaEffectActor::IsEmittersWriteFromDetailsPanel(EvKeepSim);
						GEditor->PlayWorld = PrevPlay;
						S += FString::Printf(TEXT("[ред-тест] СРЕДН-1 «Keep Simulation Changes» (Property==MemberProperty, вид Unspecified): без симуляции закон сказал «%s», при работающей симуляции «%s» — %s\n"),
							bNoSim ? TEXT("панель") : TEXT("скрипт"),
							bWithSim ? TEXT("панель") : TEXT("скрипт"),
							(!bNoSim && bWithSim) ? TEXT("верно: жест редактора отделён от записи скрипта")
								: TEXT("★ ПРОВАЛ: признак симуляции не разделяет эти два случая"));
						S += FString::Printf(TEXT("[ред-тест] СРЕДН-1 контроль: GEditor->PlayWorld возвращён в %s\n"),
							(GEditor->PlayWorld == PrevPlay) ? TEXT("прежнее значение — верно") : TEXT("★ НЕ ВОССТАНОВЛЕНО"));
					}
					else
					{
						S += TEXT("[ред-тест] СРЕДН-1: пропущено (GEditor отсутствует)\n");
					}

					// ═══ ★★★ ВОЛНА Ф16 (МЕЛК-4): ПОТОЛОК СИРОТ БОЛЬШЕ НЕ ВЫБРАСЫВАЕТ НИЧЕГО САМ ═══
					//   ЧТО ДОКАЗЫВАЕТСЯ. (A) Обычная правка поля в панели деталей — она
					//   АВТОРИТЕТНА и до этой волны включала чистку — не теряет НИ ОДНОЙ записи,
					//   сколько бы сирот ни было. (B) Перетаскивание ползунка (вид правки
					//   Interactive) — тот же ответ: ровно этот жест приходит десятками в секунду.
					//   (C) Явная команда владельца (DropOldestGlowOrphans) убирает именно
					//   излишек и именно САМЫЕ СТАРЫЕ.
					{
						const int32 Cap = AAsdaEffectActor::kEmitterGlowOrphanCap;
						const int32 Extra = 5;                      // столько записей свыше порога
						auto ArmOrphans = [K1, Cap, Extra]()
						{
							// ★ эмиттеры — к умолчанию: иначе к сиротам добавилась бы запись живого
							//   эмиттера и счёт в отчёте пришлось бы читать с поправкой
							for (FAsdaFxEmitter& E : K1->Emitters) { E.Glow = FAsdaFxEmitterGlow(); }
							K1->EmitterGlowSaved.Reset();
							for (int32 i = 0; i < Cap + Extra; ++i)
							{
								FAsdaFxEmitterGlowSaved R;
								R.Name = FString::Printf(TEXT("__asda_orphan_%02d__"), i);
								R.JebType = 0x7F;                   // тип, которого у эмиттеров нет
								R.Index = 1000 + i;                 // номер за пределами списка
								R.Glow.Source = EAsdaFxEmitterGlowSource::Manual;
								R.Glow.Spec.IntensityCd = 100.f + i;
								K1->EmitterGlowSaved.Add(R);
							}
							K1->bGlowOrphanOverflowReported = false;
						};
						// (A) правка поля в панели деталей
						ArmOrphans();
						{
							FPropertyChangedEvent Ev(PropLeaf, EPropertyChangeType::ValueSet);
							Ev.SetActiveMemberProperty(PropEmitters);
							K1->PostEditChangeProperty(Ev);
						}
						const int32 AfterPanel = K1->EmitterGlowSaved.Num();
						// (B) тик перетаскивания ползунка
						ArmOrphans();
						{
							FPropertyChangedEvent Ev(PropLeaf, EPropertyChangeType::Interactive);
							Ev.SetActiveMemberProperty(PropEmitters);
							K1->PostEditChangeProperty(Ev);
						}
						const int32 AfterDrag = K1->EmitterGlowSaved.Num();
						S += FString::Printf(TEXT("[ред-тест] МЕЛК-4 сирот заведено %d при пороге %d: после правки поля в панели осталось %d, после тика ползунка %d — %s\n"),
							Cap + Extra, Cap, AfterPanel, AfterDrag,
							(AfterPanel >= Cap + Extra && AfterDrag >= Cap + Extra)
								? TEXT("верно: ни одна запись не выброшена")
								: TEXT("★ ВЫБРОШЕН: правка в панели всё ещё чистит копию молча"));
						// (C) явная команда владельца — проба и настоящая уборка
						ArmOrphans();
						const int32 WouldDrop = K1->DropOldestGlowOrphans(/*bDryRun=*/true);
						const int32 AfterDry = K1->EmitterGlowSaved.Num();
						// ★ Ф18 (МЕЛК-3): здесь проверяется ЧТО отбрасывается, а не «спросили ли
						//   согласие». Буфера отмены в коммандлете нет, поэтому уборка без явного
						//   согласия теперь законно ОТКАЗЫВАЕТСЯ (её разбор — отдельным тестом
						//   МЕЛК-3 ниже, в блоке С-1/Ф17). Здесь даём согласие явно.
						const int32 Dropped = K1->DropOldestGlowOrphans(/*bDryRun=*/false, /*bForceWithoutUndo=*/true);
						const int32 AfterCmd = K1->EmitterGlowSaved.Num();
						// Самые старые (нулевые номера) должны уйти, самые новые — остаться
						bool bOldestGone = true, bNewestKept = false;
						// ★ имя OrphanRec, а не R: в охватывающей области уже есть FAsdaGlowEmitterRef R
						for (const FAsdaFxEmitterGlowSaved& OrphanRec : K1->EmitterGlowSaved)
						{
							if (OrphanRec.Index == 1000) { bOldestGone = false; }
							if (OrphanRec.Index == 1000 + Cap + Extra - 1) { bNewestKept = true; }
						}
						S += FString::Printf(TEXT("[ред-тест] МЕЛК-4 явная команда: проба сказала %d (копия после пробы %d), уборка сняла %d, осталось %d; самая старая %s, самая новая %s — %s\n"),
							WouldDrop, AfterDry, Dropped, AfterCmd,
							bOldestGone ? TEXT("убрана") : TEXT("★ ОСТАЛАСЬ"),
							bNewestKept ? TEXT("на месте") : TEXT("★ ПОТЕРЯНА"),
							(WouldDrop == Extra && AfterDry == Cap + Extra && Dropped == Extra
							 && AfterCmd == Cap && bOldestGone && bNewestKept)
								? TEXT("верно") : TEXT("★ ПРОВАЛ: уборка по команде работает не так, как обещано"));
						K1->EmitterGlowSaved.Reset();
						K1->bGlowOrphanOverflowReported = false;
						// (D) ВИДИМОСТЬ ДЛЯ ВЛАДЕЛЬЦА. Уборка возможна только явной командой, а о
						//     превышении порога владельцу сообщает подписчик события. Проверяем, что
						//     оба на месте: команда зарегистрирована (а не потерялась при
						//     переименовании — грабля «последняя регистрация выживает») и у события
						//     есть слушатель (его ставит склад снимков конфигуратора)
						const bool bCmd = IConsoleManager::Get().FindConsoleObject(TEXT("asda.Fx.Glow.DropOrphans")) != nullptr;
						const bool bHeard = AAsdaEffectActor::OnEmitterGlowOrphanOverflow().IsBound();
						S += FString::Printf(TEXT("[ред-тест] МЕЛК-4 видимость: команда asda.Fx.Glow.DropOrphans зарегистрирована %s; у события «сирот больше порога» есть слушатель %s — %s\n"),
							bCmd ? TEXT("да") : TEXT("★ НЕТ"), bHeard ? TEXT("да") : TEXT("★ НЕТ"),
							(bCmd && bHeard) ? TEXT("верно: молчаливой чистки нет, а о росте копии владельцу говорят")
								: TEXT("★ ПРОВАЛ: владельцу не о чем узнать или нечем убрать"));
					}

					// ═══ ★★★ ВОЛНА Ф16 (МЕЛК-1): НАБОР «НАША ТРАНЗАКЦИЯ» НЕ ПЕРЕЖИВАЕТ СВОЮ ОТМЕНУ ══
					//   ЧТО ДОКАЗЫВАЕТСЯ. Набор, наполненный отменой, чья рассылка PostUndoRedo была
					//   движком ПРОПУЩЕНА (Succeeded == false или bSuspendBroadcastPostUndoRedo —
					//   EditorServer.cpp:1181-1190), не достаётся СЛЕДУЮЩЕЙ, посторонней отмене.
					//   Зовётся ровно то, что зовёт буфер отмен: FAsdaFxGlowConfigModel::
					//   BeginUndoOperation (подписана на UTransBuffer::OnBeforeRedoUndo).
					{
						// Отмена №1: наш эффект в транзакции, но рассылки PostUndoRedo не было
						FAsdaFxGlowConfigModel::BeginUndoOperation();
						FAsdaFxGlowConfigModel::NotePostEditUndo(K1);
						const int32 AfterNote = FAsdaFxGlowConfigModel::UndoTouchedClassCount();
						// Отмена №2: посторонняя — нашего эффекта в ней нет
						FAsdaFxGlowConfigModel::BeginUndoOperation();
						const int32 AtForeign = FAsdaFxGlowConfigModel::UndoTouchedClassCount();
						const int32 ResyncedForeign = FAsdaFxGlowConfigModel::ResyncAfterUndo();
						S += FString::Printf(TEXT("[ред-тест] МЕЛК-1 набор «наша транзакция»: после отмены №1 в наборе %d класс(ов); в начале посторонней отмены №2 — %d; раздача по ней тронула %d акторов — %s\n"),
							AfterNote, AtForeign, ResyncedForeign,
							(AfterNote == 1 && AtForeign == 0 && ResyncedForeign == 0)
								? TEXT("верно: набор не стареет")
								: TEXT("★ ПРОВАЛ: набор достался чужой отмене"));
						// Контрольная пара: СВОЯ отмена набор видит и съедает
						FAsdaFxGlowConfigModel::BeginUndoOperation();
						FAsdaFxGlowConfigModel::NotePostEditUndo(K1);
						const int32 Own = FAsdaFxGlowConfigModel::UndoTouchedClassCount();
						FAsdaFxGlowConfigModel::ResyncAfterUndo();
						const int32 OwnAfter = FAsdaFxGlowConfigModel::UndoTouchedClassCount();
						S += FString::Printf(TEXT("[ред-тест] МЕЛК-1 контроль (СВОЯ отмена): в наборе было %d, после раздачи %d — %s\n"),
							Own, OwnAfter,
							(Own == 1 && OwnAfter == 0) ? TEXT("верно: набор виден своей отмене и съеден")
								: TEXT("★ ПРОВАЛ: своя отмена набор не видит"));
					}

					// ── ВЕТКА C: ЖИЗНЕННАЯ ЦЕПОЧКА «возврат в панели → Ctrl+S» ──
					//   Панель деталей чистит копию авторитетно, а следом идёт PreSave — вызов
					//   НЕавторитетный. Он обязан оставить копию пустой: иначе отменённая запись
					//   вернулась бы в .uasset на самом последнем шаге
					Arm();
					ClearByHand();
					{
						FPropertyChangedEvent Ev(PropLeaf, EPropertyChangeType::ValueSet);
						Ev.SetActiveMemberProperty(PropEmitters);
						K1->PostEditChangeProperty(Ev);          // рука владельца
					}
					const int32 AfterHand = K1->EmitterGlowSaved.Num();
					K1->SaveEmitterGlow(/*bAuthoritative=*/false);   // ← ровно то, что делает PreSave
					const int32 AfterPreSave = K1->EmitterGlowSaved.Num();
					S += FString::Printf(TEXT("[ред-тест] К-1 A/B ВЕТКА C (панель деталей, затем Ctrl+S): после возврата копия %d, после PreSave копия %d — %s\n"),
						AfterHand, AfterPreSave,
						(AfterHand == 0 && AfterPreSave == 0) ? TEXT("верно: в .uasset уходит пустая копия")
							: TEXT("★ ПРОВАЛ: отменённая запись возвращается на сохранении"));

					// ── ВЕТКА D: СТОРОЖ НЕ ОСЛАБЛЕН ──
					//   Неавторитетный вызов при пустом снимке и непустой копии обязан копию
					//   СБЕРЕЧЬ: это единственная защита от перепечки, пришедшей мимо
					//   PostEditChangeProperty (сохранение случилось раньше восстановления).
					//   Ветка нужна именно как контрольная пара к A: правка места вызова не должна
					//   была стоить ни одного случая работающей защиты
					Arm();
					ClearByHand();
					K1->SaveEmitterGlow(/*bAuthoritative=*/false);
					const int32 GuardCopy = K1->EmitterGlowSaved.Num();
					S += FString::Printf(TEXT("[ред-тест] К-1 A/B ВЕТКА D (сторож: НЕавторитетный вызов при пустом снимке): копия %d записей — %s\n"),
						GuardCopy,
						GuardCopy > 0 ? TEXT("верно: защита от перепечки не ослаблена")
							: TEXT("★ ОЧИЩЕНА: сторож сломан, перепечка сотрёт настройки владельца"));
				}
			}

			// ═══ ★★★ ВОЛНА Ф14 (МЕЛК-1): ПОТОЛОК СИРОТ И СЛАБЕЙШИЙ КЛЮЧ ══════════════════════════
			{
				AAsdaEffectActor* Mk = W->SpawnActor<AAsdaEffectActor>(Cls, Sp);
				if (!Mk || Mk->Emitters.Num() < 2)
				{
					S += TEXT("[ред-тест] МЕЛК-1: пропущено (нужен экземпляр минимум с двумя эмиттерами)\n");
				}
				else
				{
					// (а) ПОРОГ. ═══ ★★★ ВОЛНА Ф16 (МЕЛК-4): ОЖИДАНИЕ ПЕРЕВЁРНУТО ═══════════════
					//     Ф14 требовала, чтобы авторитетная запись САМА отбрасывала лишних сирот, и
					//     Ф15 оставила это «только для авторитетной». Но авторитетная запись — это
					//     и есть правка владельца в панели (и каждый тик ползунка), поэтому такой
					//     выброс — потеря настроек на самом частом жесте. Теперь потолок только
					//     ПОРОГ ПРЕДУПРЕЖДЕНИЯ, а уборка идёт по явной команде (DropOldestGlowOrphans).
					//     Здесь проверяется НОВЫЙ закон: авторитетная запись не теряет НИ ОДНОЙ
					//     записи; уборка по команде снимает ровно излишек и именно самых старых.
					const int32 Many = AAsdaEffectActor::kEmitterGlowOrphanCap + 8;
					Mk->EmitterGlowSaved.Reset();
					for (int32 i = 0; i < Many; ++i)
					{
						FAsdaFxEmitterGlowSaved O;
						O.Name = FString::Printf(TEXT("__orphan_%d__"), i);
						O.JebType = 0x7F;
						O.Index = 1000 + i;
						O.Glow.Source = EAsdaFxEmitterGlowSource::Manual;
						O.Glow.Spec.IntensityCd = (float)i;
						Mk->EmitterGlowSaved.Add(O);
					}
					Mk->bGlowOrphanOverflowReported = false;
					Mk->SaveEmitterGlow(/*bAuthoritative=*/true);
					const int32 Capped = Mk->EmitterGlowSaved.Num();
					// СТАРЫЙ ЗАКОН Ф14/Ф15 на этих же данных выбросил бы (Many − потолок) записей
					const int32 OldLawWouldDrop = Many - AAsdaEffectActor::kEmitterGlowOrphanCap;
					S += FString::Printf(TEXT("[ред-тест] МЕЛК-1/Ф16 порог сирот при АВТОРИТЕТНОЙ записи: положено %d при пороге %d, осталось %d; старый закон выбросил бы %d — %s\n"),
						Many, AAsdaEffectActor::kEmitterGlowOrphanCap, Capped, OldLawWouldDrop,
						(Capped == Many) ? TEXT("верно: правка владельца не теряет ни одной записи")
							: TEXT("★ ВЫБРОШЕН: авторитетная запись всё ещё чистит копию молча"));
					// Уборка по ЯВНОЙ команде: снимается ровно излишек, уходят самые старые
					// ★ Ф18 (МЕЛК-3): согласие `force` — буфера отмены в коммандлете нет, а здесь
					//   проверяется правило «сколько и каких записей снимается», не согласие
					const int32 CmdDropped = Mk->DropOldestGlowOrphans(/*bDryRun=*/false, /*bForceWithoutUndo=*/true);
					const int32 AfterCmd2 = Mk->EmitterGlowSaved.Num();
					const bool bOldestGone = AfterCmd2 > 0 && Mk->EmitterGlowSaved[0].Name == FString::Printf(TEXT("__orphan_%d__"), OldLawWouldDrop);
					S += FString::Printf(TEXT("[ред-тест] МЕЛК-1/Ф16 уборка по команде: снято %d, осталось %d, первая уцелевшая «%s» — %s\n"),
						CmdDropped, AfterCmd2, AfterCmd2 > 0 ? *Mk->EmitterGlowSaved[0].Name : TEXT("нет"),
						(CmdDropped == OldLawWouldDrop && AfterCmd2 == AAsdaEffectActor::kEmitterGlowOrphanCap && bOldestGone)
							? TEXT("верно: отброшены самые старые и ровно излишек")
							: TEXT("★ ПРОВАЛ: уборка по команде работает не так, как обещано"));

					// (б) СЛАБЕЙШИЙ КЛЮЧ. Запись носит имя эмиттера 1, но с ДРУГИМ типом .jeb.
					//     По голому номеру она НЕ должна достаться эмиттеру 1 — это чужие числа
					//     (прежний проход 3 отдавал: `default: bHit = R.Index == i`)
					for (FAsdaFxEmitter& E : Mk->Emitters) { E.Glow = FAsdaFxEmitterGlow(); }
					FAsdaFxEmitterGlowSaved Wrong;
					Wrong.Name = Mk->Emitters[1].Name;
					Wrong.JebType = Mk->Emitters[1].JebType ^ 0x40;   // тип другой — строгие ключи мимо
					Wrong.Index = 1;
					Wrong.Glow.Source = EAsdaFxEmitterGlowSource::Manual;
					Wrong.Glow.Spec.IntensityCd = 555.f;
					Mk->EmitterGlowSaved.Reset();
					Mk->EmitterGlowSaved.Add(Wrong);
					const int32 Back = Mk->RestoreEmitterGlow();
					const bool bNotStolen = Mk->Emitters[1].Glow.Source == EAsdaFxEmitterGlowSource::FromEffect;
					S += FString::Printf(TEXT("[ред-тест] МЕЛК-1 слабейший ключ: запись «%s» (тип 0x%X против 0x%X у эмиттера) роздана %d раз, эмиттер 1 %s — %s\n"),
						*Wrong.Name, Wrong.JebType, Mk->Emitters[1].JebType, Back,
						bNotStolen ? TEXT("остался «от эффекта»") : TEXT("★ ПОЛУЧИЛ ЧУЖИЕ ЧИСЛА"),
						(Back == 0 && bNotStolen) ? TEXT("верно: голый номер чужую запись не отдаёт") : TEXT("★ ПРОВАЛ"));

					// ═══ ★★★ ВОЛНА Ф15 (МЕЛК-1): ТОТ ЖЕ ЗАХВАТ, НО ЧЕРЕЗ ПРОХОД «НОМЕР + ТИП» ═══
					//   Контрпример ревизии: Ф14 ограничила только проход «голый номер», а проход
					//   «номер + тип .jeb» остался без ограничения и продолжал раздавать чужие
					//   числа. Эмиттеры приводятся к виду [0]{«BG»,0x5} [1]{«X»,0xE}
					//   [2]{«CFA_01»,0x4E}, запись — {«CFA_01», тип 0xE, номер 1}: строгий ключ
					//   (имя+тип) мимо, а номер и тип совпадают с эмиттером «X».
					// ★ Трёх эмиттеров у выбранного эффекта может и не быть — дописываем недостающие
					//   ПРЯМО В ТРАНЗИТНЫЙ актор своего мира (ассет и его CDO не трогаются вовсе)
					const int32 EmittersWere = Mk->Emitters.Num();
					if (Mk->Emitters.Num() < 3) { Mk->Emitters.SetNum(3); }
					{
						// Имена и типы .jeb в эмиттерах — ТРАНЗИТНОГО актора, ассет не трогаем
						const FString N0 = Mk->Emitters[0].Name, N1 = Mk->Emitters[1].Name, N2 = Mk->Emitters[2].Name;
						const uint8 T0 = Mk->Emitters[0].JebType, T1 = Mk->Emitters[1].JebType, T2 = Mk->Emitters[2].JebType;
						for (FAsdaFxEmitter& E : Mk->Emitters) { E.Glow = FAsdaFxEmitterGlow(); }
						Mk->Emitters[0].Name = TEXT("BG");     Mk->Emitters[0].JebType = 0x05;
						Mk->Emitters[1].Name = TEXT("X");      Mk->Emitters[1].JebType = 0x0E;
						Mk->Emitters[2].Name = TEXT("CFA_01"); Mk->Emitters[2].JebType = 0x4E;
						FAsdaFxEmitterGlowSaved Rec;
						Rec.Name = TEXT("CFA_01");
						Rec.JebType = 0x0E;
						Rec.Index = 1;
						Rec.Glow.Source = EAsdaFxEmitterGlowSource::Manual;
						Rec.Glow.Spec.IntensityCd = 321.f;
						Mk->EmitterGlowSaved.Reset();
						Mk->EmitterGlowSaved.Add(Rec);
						const int32 Back2 = Mk->RestoreEmitterGlow();
						const bool bXClean = Mk->Emitters[1].Glow.Source == EAsdaFxEmitterGlowSource::FromEffect;
						S += FString::Printf(TEXT("[ред-тест] МЕЛК-1 проход «номер+тип»: запись «CFA_01» (тип 0xE, номер 1) при эмиттерах BG/X/CFA_01 роздана %d раз, эмиттер «X» %s — %s\n"),
							Back2, bXClean ? TEXT("остался «от эффекта»") : TEXT("★ ПОЛУЧИЛ ЧУЖИЕ ЧИСЛА"),
							(Back2 == 0 && bXClean) ? TEXT("верно: имя записи носит другой эмиттер, по номеру она чужому не достаётся")
								: TEXT("★ ПРОВАЛ: ограничение имён до прохода «номер+тип» не дошло"));
						// Контрольная пара: та же запись, но её имени в списке эмиттеров НЕТ —
						// тогда проход «номер+тип» обязан сработать (иначе мы не ограничили, а сломали)
						for (FAsdaFxEmitter& E : Mk->Emitters) { E.Glow = FAsdaFxEmitterGlow(); }
						Mk->Emitters[2].Name = TEXT("CFA_01_renamed");
						Mk->EmitterGlowSaved.Reset();
						Mk->EmitterGlowSaved.Add(Rec);
						const int32 Back3 = Mk->RestoreEmitterGlow();
						const bool bXGot = Mk->Emitters[1].Glow.Source == EAsdaFxEmitterGlowSource::Manual
							&& FMath::IsNearlyEqual(Mk->Emitters[1].Glow.Spec.IntensityCd, 321.f);
						S += FString::Printf(TEXT("[ред-тест] МЕЛК-1 контроль (имени «CFA_01» в списке эмиттеров больше нет): роздано %d, эмиттер «X» %s — %s\n"),
							Back3, bXGot ? TEXT("числа получил") : TEXT("★ НЕ ПОЛУЧИЛ"),
							(Back3 == 1 && bXGot) ? TEXT("верно: перепечка с переименованием по-прежнему доезжает")
								: TEXT("★ ПРОВАЛ: ограничение сломало законный случай"));
						// вернуть имена и типы транзитного актора как были
						Mk->Emitters[0].Name = N0; Mk->Emitters[0].JebType = T0;
						Mk->Emitters[1].Name = N1; Mk->Emitters[1].JebType = T1;
						Mk->Emitters[2].Name = N2; Mk->Emitters[2].JebType = T2;
						for (FAsdaFxEmitter& E : Mk->Emitters) { E.Glow = FAsdaFxEmitterGlow(); }
					}
					// вернуть транзитному актору прежнюю длину списка эмиттеров
					if (EmittersWere < 3) { Mk->Emitters.SetNum(EmittersWere); }

					// ═══ ★★★ ВОЛНА Ф15 (МЕЛК-2): ПОТОЛОК НЕ СРАБАТЫВАЕТ НА СОХРАНЕНИИ ═══════════
					//   Ctrl+S идёт через PreSave → SaveEmitterGlow() БЕЗ параметра, то есть вызов
					//   НЕавторитетный. Выбрасывать на нём записи настроек владельца нельзя: он
					//   просит «запиши, что есть», а не «прибери лишнее».
					{
						const int32 Many2 = AAsdaEffectActor::kEmitterGlowOrphanCap + 8;
						Mk->EmitterGlowSaved.Reset();
						for (int32 i = 0; i < Many2; ++i)
						{
							FAsdaFxEmitterGlowSaved O;
							O.Name = FString::Printf(TEXT("__orphan2_%d__"), i);
							O.JebType = 0x7F;
							O.Index = 2000 + i;
							O.Glow.Source = EAsdaFxEmitterGlowSource::Manual;
							O.Glow.Spec.IntensityCd = (float)i;
							Mk->EmitterGlowSaved.Add(O);
						}
						Mk->SaveEmitterGlow(/*bAuthoritative=*/false);   // ← ровно то, что делает PreSave
						const int32 AfterSave = Mk->EmitterGlowSaved.Num();
						S += FString::Printf(TEXT("[ред-тест] МЕЛК-2 потолок при сохранении: положено %d сирот (потолок %d), после НЕавторитетной записи осталось %d — %s\n"),
							Many2, AAsdaEffectActor::kEmitterGlowOrphanCap, AfterSave,
							(AfterSave == Many2) ? TEXT("верно: Ctrl+S ничего не выбрасывает")
								: TEXT("★ ПРОВАЛ: обычное сохранение отбрасывает записи владельца"));
						Mk->EmitterGlowSaved.Reset();
					}

					// ═══ ★★★ ВОЛНА Ф17 (С-1): УБОРКА СИРОТ ОБЯЗАНА ОТМЕНЯТЬСЯ ═══════════════════
					//   ЧТО ДОКАЗЫВАЕТСЯ. Прежде внутри DropOldestGlowOrphans стоял голый Modify()
					//   с комментарием «уборка обязана попасть в транзакцию», а транзакции не
					//   открывал никто: во всём AsdaEffectActor.cpp было ноль FScopedTransaction.
					//   Без валидного GUndo SaveToTransactionBuffer выходит сразу
					//   (UObjectGlobals.cpp:3218) и Modify делает одно MarkPackageDirty — то есть
					//   отброшенные настройки владельца Ctrl+Z не возвращал, а пакет оставался
					//   помеченным и уходил на диск его обычным «Save All».
					//   ВЕТКА A — старый закон дословно: Modify() без открытой транзакции.
					//   ВЕТКА B — нынешний код: DropOldestGlowOrphans открывает транзакцию сам,
					//     ставит RF_Transactional и НАСТОЯЩАЯ отмена возвращает записи.
					//   ВЕТКА C — контрольная пара к флагу: объект БЕЗ RF_Transactional (так
					//     создаётся CDO блюпринта, Class.cpp:5091 — а уборка ходит именно по CDO)
					//     при открытой транзакции старым кодом в буфер не попадал бы.
					//   ★ БУФЕР ОТМЕНЫ В КОММАНДЛЕТЕ ОТСУТСТВУЕТ (GEditor->Trans == null, см. К2).
					//     Поэтому на время проверки заводится ВРЕМЕННЫЙ UTransBuffer — ровно такой
					//     же, какой редактор создаёт себе сам (EditorServer.cpp:1263-1264), — и
					//     сразу после проверки прежнее значение GEditor->Trans возвращается.
					//     Это не подделка результата: проверяется НАСТОЯЩИЙ UTransBuffer::Undo.
					if (!GEditor)
					{
						S += TEXT("[ред-тест] С-1/Ф17 уборка сирот и отмена: пропущено (GEditor отсутствует)\n");
					}
					else
					{
						UTransactor* const PrevTrans = GEditor->Trans;
						UTransBuffer* Temp = nullptr;
						if (!PrevTrans)
						{
							Temp = NewObject<UTransBuffer>();
							Temp->Initialize((SIZE_T)8 * 1024 * 1024);   // 8 МБ хватит на десяток записей копии
							Temp->AddToRoot();                           // от сборки мусора на время проверки
							GEditor->Trans = Temp;
						}
						UTransBuffer* const Buf = Cast<UTransBuffer>(GEditor->Trans);
						S += FString::Printf(TEXT("[ред-тест] С-1/Ф17 буфер отмены: %s\n"),
							PrevTrans ? TEXT("взят живой буфер редактора")
								: (Buf ? TEXT("живого не было (коммандлет) — заведён временный UTransBuffer, прежнее значение GEditor->Trans будет возвращено")
									: TEXT("★ НЕТ — временный буфер не завёлся")));
						// Набить копию сиротами; возвращает, сколько записей положено
						const int32 Many3 = AAsdaEffectActor::kEmitterGlowOrphanCap + 6;
						auto FillOrphans = [Mk, Many3]()
						{
							for (FAsdaFxEmitter& E : Mk->Emitters) { E.Glow = FAsdaFxEmitterGlow(); }
							Mk->EmitterGlowSaved.Reset();
							for (int32 i = 0; i < Many3; ++i)
							{
								FAsdaFxEmitterGlowSaved O;
								O.Name = FString::Printf(TEXT("__orphan3_%d__"), i);
								O.JebType = 0x7F;
								O.Index = 3000 + i;
								O.Glow.Source = EAsdaFxEmitterGlowSource::Manual;
								O.Glow.Spec.IntensityCd = (float)i;
								Mk->EmitterGlowSaved.Add(O);
							}
						};
						if (!Buf)
						{
							S += TEXT("[ред-тест] С-1/Ф17: пропущено (буфера отмены нет и завести не удалось)\n");
						}
						else
						{
							// ── ВЕТКА A: СТАРЫЙ ЗАКОН — Modify() без открытой транзакции ──
							FillOrphans();
							const int32 QueueBeforeA = Buf->GetQueueLength();
							const bool bModA = Mk->Modify();        // ← ровно прежняя строка уборки
							const int32 QueueAfterA = Buf->GetQueueLength();
							S += FString::Printf(TEXT("[ред-тест] С-1/Ф17 ВЕТКА A (старый закон: Modify() без FScopedTransaction): Modify вернул «%s», очередь отмен %d → %d — %s\n"),
								bModA ? TEXT("записано") : TEXT("НЕ записано"), QueueBeforeA, QueueAfterA,
								(!bModA && QueueAfterA == QueueBeforeA)
									? TEXT("дефект воспроизведён: отменять нечего, уборка была бы безвозвратной")
									: TEXT("★ ПРОВАЛ: дефект не воспроизводится, A/B бессмысленно"));

							// ── ВЕТКА B: НЫНЕШНИЙ КОД — транзакцию открывает сама уборка ──
							FillOrphans();
							const int32 FullN = Mk->EmitterGlowSaved.Num();
							const float FirstBefore = Mk->EmitterGlowSaved[0].Glow.Spec.IntensityCd;
							const int32 QueueBeforeB = Buf->GetQueueLength();
							const int32 DroppedB = Mk->DropOldestGlowOrphans(/*bDryRun=*/false);
							const int32 AfterDrop = Mk->EmitterGlowSaved.Num();
							// ★ Ф18 (МЕЛК-4): признак стал ПОЛЕМ АКТОРА (был статиком на весь модуль)
							const bool bModB = Mk->bLastOrphanDropUndoable;
							const int32 QueueAfterB = Buf->GetQueueLength();
							// НАСТОЯЩАЯ отмена: тот же UTransBuffer::Undo, что и у Ctrl+Z владельца
							const bool bUndone = Buf->Undo(/*bCanRedo=*/false);
							const int32 AfterUndo = Mk->EmitterGlowSaved.Num();
							const bool bFirstBack = AfterUndo > 0
								&& FMath::IsNearlyEqual(Mk->EmitterGlowSaved[0].Glow.Spec.IntensityCd, FirstBefore);
							S += FString::Printf(TEXT("[ред-тест] С-1/Ф17 ВЕТКА B (нынешний код): положено %d, уборка сняла %d (осталось %d), Modify вернул «%s», очередь отмен %d → %d; отмена прошла %s, записей после отмены %d, первая запись вернулась %s — %s\n"),
								FullN, DroppedB, AfterDrop,
								bModB ? TEXT("записано") : TEXT("★ НЕ записано"),
								QueueBeforeB, QueueAfterB,
								bUndone ? TEXT("да") : TEXT("★ НЕТ"), AfterUndo,
								bFirstBack ? TEXT("да") : TEXT("★ НЕТ"),
								(bModB && QueueAfterB > QueueBeforeB && bUndone && AfterUndo == FullN && bFirstBack)
									? TEXT("верно: отмена возвращает отброшенные записи целиком")
									: TEXT("★ ПРОВАЛ: уборка по-прежнему не отменяется"));

							// ── ВЕТКА C: КОНТРОЛЬНАЯ ПАРА К ФЛАГУ RF_Transactional ──
							//   Уборка ходит по CDO запечённых BP_Fx_*, а CDO блюпринта создаётся
							//   без этого флага. Имитируем: снимаем флаг с транзитного актора и
							//   смотрим, что старый код (голый Modify под открытой транзакцией) в
							//   буфер не попал бы, а нынешний — попадает, потому что ставит флаг сам.
							FillOrphans();
							Mk->ClearFlags(RF_Transactional);
							bool bModNoFlag = false;
							{
								FScopedTransaction TxC(FText::FromString(TEXT("[ред-тест] контроль RF_Transactional")));
								bModNoFlag = Mk->Modify();       // ← старый код: флаг не ставит
							}
							Mk->ClearFlags(RF_Transactional);    // снова как у CDO блюпринта
							const int32 FullC = Mk->EmitterGlowSaved.Num();
							Mk->DropOldestGlowOrphans(/*bDryRun=*/false);   // ← нынешний код ставит флаг сам
							const bool bModWithFix = Mk->bLastOrphanDropUndoable;   // ★ Ф18 (МЕЛК-4): поле актора
							const bool bFlagSet = Mk->HasAnyFlags(RF_Transactional);
							const bool bUndoneC = Buf->Undo(/*bCanRedo=*/false);
							const int32 AfterUndoC = Mk->EmitterGlowSaved.Num();
							S += FString::Printf(TEXT("[ред-тест] С-1/Ф17 ВЕТКА C (объект БЕЗ RF_Transactional, как CDO блюпринта): старый код под открытой транзакцией записал «%s»; нынешний флаг поставил %s и записал «%s»; отмена вернула %d из %d записей — %s\n"),
								bModNoFlag ? TEXT("записано") : TEXT("НЕ записано"),
								bFlagSet ? TEXT("да") : TEXT("★ НЕТ"),
								bModWithFix ? TEXT("записано") : TEXT("★ НЕ записано"),
								AfterUndoC, FullC,
								(!bModNoFlag && bFlagSet && bModWithFix && bUndoneC && AfterUndoC == FullC)
									? TEXT("верно: без флага транзакция была бы пустой, теперь флаг ставится и откат работает")
									: TEXT("★ ПРОВАЛ: половина беды (флаг) не закрыта"));

							// ── ВЕТКА D: `dry` НЕ ДОЛЖЕН порождать запись в буфере отмены ──
							FillOrphans();
							const int32 QueueBeforeD = Buf->GetQueueLength();
							const int32 DryN = Mk->DropOldestGlowOrphans(/*bDryRun=*/true);
							const int32 QueueAfterD = Buf->GetQueueLength();
							const int32 AfterDryN = Mk->EmitterGlowSaved.Num();
							S += FString::Printf(TEXT("[ред-тест] С-1/Ф17 ВЕТКА D (проба `dry`): насчитано %d, записей осталось %d, очередь отмен %d → %d — %s\n"),
								DryN, AfterDryN, QueueBeforeD, QueueAfterD,
								(AfterDryN == Many3 && QueueAfterD == QueueBeforeD)
									? TEXT("верно: проба ничего не меняет и в буфер отмены не лезет")
									: TEXT("★ ПРОВАЛ: проба оставила след"));

							// ═══ ★★★ ВОЛНА Ф18 (МЕЛК-3): ОТКАТА НЕТ — НЕ ОТБРАСЫВАЕМ ВОВСЕ ═════
							//   ЧТО ДОКАЗЫВАЕТСЯ. Волна Ф17 научилась ЗАМЕЧАТЬ недоступный откат
							//   (Modify вернул «не записано») и печатала об этом Warning — но
							//   записи после этого отбрасывала ВСЁ РАВНО. То есть в единственном
							//   опасном случае поведение не отличалось от прежнего: настройки
							//   владельца уходили безвозвратно (owner-asset-edits-never-lose).
							//   ВЕТКА E-A — старый закон Ф17 дословно: он ОТБРАСЫВАЛ. Показываем
							//     это числом, которое уборка насчитала пробой: ровно столько
							//     записей старый код и снял бы при недоступном откате.
							//   ВЕТКА E-B — нынешний закон: без согласия НИЧЕГО не тронуто, с
							//     явным `force` уборка проходит.
							//   КОНТРОЛЬНАЯ ПАРА (E-C): когда откат ДОСТУПЕН, согласие не нужно —
							//     иначе «защита» превратилась бы в лишний вопрос на каждый жест.
							//   ★ Состояние «отката нет» подделывается честно: у редактора на
							//     время опыта отнимается буфер (GEditor->Trans = nullptr, как в
							//     коммандлете), после опыта возвращается. Ровно то, что увидит
							//     функция: FScopedTransaction без буфера GUndo не ставит.
							UTransactor* const TransHere = GEditor->Trans;
							GEditor->Trans = nullptr;
							FillOrphans();
							const int32 FullE = Mk->EmitterGlowSaved.Num();
							const int32 OldLawWouldDropE = Mk->DropOldestGlowOrphans(/*bDryRun=*/true);
							Mk->bLastOrphanDropUndoable = true;   // заведомо «грязное»: отказ обязан его сбросить
							const int32 RefusedN = Mk->DropOldestGlowOrphans(/*bDryRun=*/false);
							const int32 AfterRefuse = Mk->EmitterGlowSaved.Num();
							const bool bFlagAfterRefuse = Mk->bLastOrphanDropUndoable;
							const int32 ForcedN = Mk->DropOldestGlowOrphans(/*bDryRun=*/false, /*bForceWithoutUndo=*/true);
							const int32 AfterForce = Mk->EmitterGlowSaved.Num();
							GEditor->Trans = TransHere;
							S += FString::Printf(TEXT("[ред-тест] С-1/Ф18 ВЕТКА E (откат НЕДОСТУПЕН): положено %d, старый закон Ф17 снял бы %d; без согласия снято %d (осталось %d, признак «попало в отмену» %s); с явным `force` снято %d (осталось %d) — %s\n"),
								FullE, OldLawWouldDropE, RefusedN, AfterRefuse,
								bFlagAfterRefuse ? TEXT("★ остался true") : TEXT("false — верно"),
								ForcedN, AfterForce,
								(OldLawWouldDropE > 0 && RefusedN == 0 && AfterRefuse == FullE && !bFlagAfterRefuse
								 && ForcedN == OldLawWouldDropE && AfterForce == AAsdaEffectActor::kEmitterGlowOrphanCap)
									? TEXT("верно: без отката и без согласия не теряется ни одна запись, согласие даёт уборку")
									: TEXT("★ ПРОВАЛ: уборка без отката по-прежнему отбрасывает записи молча"));
							// ── КОНТРОЛЬ E-C: откат ДОСТУПЕН — согласия не спрашиваем ──
							FillOrphans();
							const int32 FullEC = Mk->EmitterGlowSaved.Num();
							const int32 NoForceN = Mk->DropOldestGlowOrphans(/*bDryRun=*/false);
							const int32 AfterEC = Mk->EmitterGlowSaved.Num();
							const bool bFlagEC = Mk->bLastOrphanDropUndoable;
							S += FString::Printf(TEXT("[ред-тест] С-1/Ф18 контроль E-C (откат ДОСТУПЕН, согласия нет): положено %d, снято %d, осталось %d, признак «попало в отмену» %s — %s\n"),
								FullEC, NoForceN, AfterEC, bFlagEC ? TEXT("true") : TEXT("★ false"),
								(NoForceN > 0 && AfterEC == AAsdaEffectActor::kEmitterGlowOrphanCap && bFlagEC)
									? TEXT("верно: защита не мешает обычной уборке")
									: TEXT("★ ПРОВАЛ: уборка спрашивает согласие там, где откат работает"));

							// ═══ ★★★ ВОЛНА Ф19 (МЕЛК-1): КАКОЙ СЛЕД ОСТАВЛЯЛ ОТКАЗ ═════════════════
							//   ЧТО ПРОВЕРЯЕТСЯ. В законе Ф18 проверка «откат недоступен и согласия
							//   нет» стояла НИЖЕ `FScopedTransaction` и `SetFlags(RF_Transactional)`,
							//   а текст уверял, что отказ «не оставляет следов вовсе». Ревизия
							//   предположила, что след дорогой: открытие транзакции стирает СТЕК
							//   ПОВТОРА — UTransBuffer::BeginInternal (TransBuffer.h:77-90) при
							//   `UndoCount > 0` вынимает из UndoBuffer последние UndoCount записей.
							//   ★ ДОВОД ПРОВЕРЕН И НЕ ПОДТВЕРДИЛСЯ ЦЕЛИКОМ — и это видно ниже числами.
							//     Begin записи и вправду вынимает (замер ВНУТРИ транзакции это
							//     показывает), но End их ВОЗВРАЩАЕТ, если транзакция осталась ПУСТОЙ:
							//     FTransaction::IsTransient (EditorTransaction.cpp:515-527) при нуле
							//     записей отвечает «да», и UTransBuffer::End (там же, 1332-1353)
							//     выталкивает пустую транзакцию и вставляет RemovedTransactions
							//     обратно, восстанавливая UndoCount = PreviousUndoCount. На ветке
							//     отказа Ф18 в транзакцию никто ничего не клал (SetFlags записи не
							//     создаёт), поэтому Ctrl+Y у владельца уцелевал.
							//   ЧТО СЛЕДОМ ВСЁ-ТАКИ БЫЛО: флаг RF_Transactional, поставленный на
							//   объект до отказа, и временное опустошение стека повтора на время
							//   открытой транзакции (оно же — узкое место: положи в ту же транзакцию
							//   кто-нибудь хоть одну запись, и возврата уже не будет).
							//   ПОЧЕМУ ПЕРЕСТАНОВКА ВСЁ РАВНО СДЕЛАНА: отказ обязан быть пустым не
							//   «в итоге», а вовсе — без правки объекта и без хода буфера отмен.
							//   ★ ОТКАЗ ЗДЕСЬ ПОДДЕЛЫВАЕТСЯ НЕ ОТНЯТИЕМ БУФЕРА (иначе транзакция не
							//     открылась бы и у старого закона, и A/B стало бы бессмысленным), а
							//     ДРУГИМ условием того же предиката. Буфер при этом жив и
							//     работоспособен, значит транзакция открылась бы.
							//   ★★★ Ф20 (МЕЛК-1): ПОДДЕЛКА ЗАМЕНЕНА, И ВОТ ПОЧЕМУ. Прежде здесь
							//     ставился PKG_ContainsScript и стояло «ровно тот случай, ради
							//     которого условие заведено — нативный CDO из /Script/». Неверно
							//     дважды. Во-первых, PKG_ContainsScript в UE 5.8 не выставляет
							//     никто (по всему Engine/Source флаг только читается), а пакеты
							//     /Script/ получают PKG_CompiledIn (UObjectBase.cpp:215).
							//     Во-вторых — и это важнее, — ПАКЕТНЫМ ФЛАГОМ ОТКАЗ У ЭТОГО КЛАССА
							//     НЕ ПОДДЕЛАТЬ ВОВСЕ: условие Obj.cpp:1661 снимает запрет для
							//     классов с CLASS_DefaultConfig|CLASS_Config, а AActor объявлен
							//     `config=Engine`, и CLASS_Config НАСЛЕДУЕТСЯ — значит он есть и у
							//     AAsdaEffectActor. Числами это показывает отдельный опыт ниже
							//     (блок Ф20/МЕЛК-1).
							//   ПОДДЕЛКА ТЕПЕРЬ — ПЕРВОЕ УСЛОВИЕ СВЯЗКИ: UObject::CanModify
							//     (Obj.cpp:1641-1650) отвечает «нет» объекту с RF_NeedInitialization.
							//     Буфер отмены при этом цел, FScopedTransaction открылась бы
							//     (ScopedTransaction.cpp:20 на флаг объекта не смотрит), то есть
							//     A/B остаётся осмысленным ровно в прежнем виде.
							//   ВЕТКА A — порядок Ф18 дословно: сперва транзакция и флаг, проверка потом.
							//   ВЕТКА B — нынешний код: проверка первой, транзакция не открывается.
							//   ★ ПОРЯДОК ОПЫТОВ: сначала B (он обязан ничего не испортить), потом A
							//     на том же наполненном стеке повтора.
							FillOrphans();
							// Завести НАСТОЯЩУЮ запись повтора: правка под транзакцией + отмена с bCanRedo
							{
								FScopedTransaction TxSeed(FText::FromString(TEXT("[ред-тест] затравка стека повтора")));
								Mk->SetFlags(RF_Transactional);
								Mk->Modify();
								Mk->EmitterGlowSaved.RemoveAt(Mk->EmitterGlowSaved.Num() - 1);
							}
							const bool bSeedUndone = Buf->Undo(/*bCanRedo=*/true);
							const int32 RedoSeeded = Buf->GetUndoCount();
							const bool bCanRedoSeeded = Buf->CanRedo();
							// Подделать «откат недоступен», не трогая буфер отмены.
							// ★ Ф20 (МЕЛК-1): через CanModify (RF_NeedInitialization), а не через
							//   пакетный флаг — обоснование в разборе выше.
							UPackage* const OuterPkg = Mk->GetOutermost();
							const bool bPkgDirtyBefore = OuterPkg->IsDirty();
							Mk->SetFlags(RF_NeedInitialization);
							// ── ВЕТКА B: НЫНЕШНИЙ КОД ──
							Mk->ClearFlags(RF_Transactional);      // чтобы увидеть, не поставил ли его отказ
							OuterPkg->SetDirtyFlag(false);         // чтобы увидеть, не пометил ли пакет отказ
							const int32 FullF = Mk->EmitterGlowSaved.Num();
							const int32 RefusedF = Mk->DropOldestGlowOrphans(/*bDryRun=*/false);
							const int32 RedoAfterNew = Buf->GetUndoCount();
							const bool bCanRedoAfterNew = Buf->CanRedo();
							const bool bFlagAfterNew = Mk->HasAnyFlags(RF_Transactional);
							const bool bPkgDirtiedByRefuse = OuterPkg->IsDirty();
							const int32 AfterRefuseF = Mk->EmitterGlowSaved.Num();
							// ── ВЕТКА A: ПОРЯДОК Ф18 (транзакция и флаг ДО проверки) ──
							//   Замер делается ДВАЖДЫ: ВНУТРИ открытой транзакции (виден ли вынос
							//   записей повтора) и ПОСЛЕ её закрытия (вернул ли их End).
							int32 RedoInsideOld = -1;
							bool bCanRedoInsideOld = false;
							{
								FScopedTransaction TxOld(FText::FromString(TEXT("[ред-тест] порядок Ф18: транзакция до проверки")));
								Mk->SetFlags(RF_Transactional);
								RedoInsideOld = Buf->GetUndoCount();
								bCanRedoInsideOld = Buf->CanRedo();
								// проверка Ф18 стояла здесь и вернула бы 0 — транзакция осталась ПУСТОЙ
							}
							const int32 RedoAfterOld = Buf->GetUndoCount();
							const bool bCanRedoAfterOld = Buf->CanRedo();
							const bool bFlagAfterOld = Mk->HasAnyFlags(RF_Transactional);

							// ═══ ★★★ ВОЛНА Ф20 (МЕЛК-1): ПРЕДИКАТ «ОТКАТ ДОСТУПЕН» ПРОТИВ ФАКТА Modify() ══
							//   ЧТО ДОКАЗЫВАЕТСЯ. В законе Ф19 третьим членом связки bUndoReady стояло
							//   `!HasAnyPackageFlags(PKG_ContainsScript)` — предикат, списанный с
							//   SaveToTransactionBuffer (UObjectGlobals.cpp:3216), а не с того, кто здесь
							//   решает. Решает UObject::Modify (Obj.cpp:1661) — он отсекает раньше, по МАСКЕ
							//   PKG_ContainsScript | PKG_CompiledIn с исключением для классов
							//   CLASS_DefaultConfig | CLASS_Config.
							//   ОПЫТ СТАВИТСЯ НА ОТДЕЛЬНОМ ОБЪЕКТЕ, а не на нашем акторе, и вот почему:
							//   у AAsdaEffectActor ЕСТЬ CLASS_Config (AActor объявлен `config=Engine`, а этот
							//   флаг наследуется), то есть исключение из маски для него работает ВСЕГДА —
							//   пакетным флагом у него отказ не подделать вовсе. Это сам по себе НОВЫЙ ФАКТ
							//   (ревизия его не заметила) и печатается числами ниже.
							//   БЕРЁМ ПРОСТОЙ UObject в своём временном пакете и помечаем тот пакет
							//   PKG_CompiledIn — тем флагом, что в жизни стоит на /Script/ (UObjectBase.cpp:215).
							//   У UObject::StaticClass() config-флагов нет, значит исключение не срабатывает и
							//   расхождение видно в чистом виде:
							//     ВЕТКА A — предикат Ф19 дословно: «откат ДОСТУПЕН» (флага PKG_ContainsScript
							//       нет) ⇒ уборка пошла бы дальше, поставила RF_Transactional, получила от
							//       Modify() false и напечатала «выполнено по явному согласию force»,
							//       которого никто не давал;
							//     ВЕТКА B — нынешний предикат: «недоступен»;
							//     ФАКТ — что возвращает Modify() ВНУТРИ ОТКРЫТОЙ ТРАНЗАКЦИИ (именно так его
							//       зовёт продукт; без транзакции false получился бы по другой причине —
							//       GUndo == nullptr — и опыт доказывал бы не то).
							//   ★ КОНТРОЛЬНЫЕ ПАРЫ: (1) тот же объект БЕЗ флага — и предикат, и Modify()
							//     говорят «да» (иначе лечение свелось бы к отказу везде); (2) НАШ АКТОР в
							//     compiled-in пакете — предикат говорит «да» по исключению config;
							//     (3) НАСТОЯЩИЙ config-класс из /Script/ (UAsdaFxLightSettings) — предикат
							//     обязан сказать «доступен».
							//   ★ МУСОРА НЕ ОСТАЁТСЯ: пакет транзиентный (на диск не пишется), ссылка
							//     снимается сразу после опыта.
							// ★ CreatePackage, а не NewObject<UPackage>: у пакета, созданного
							//   CreatePackage, GetOutermost() — он сам, а NewObject с нулевым Outer
							//   положил бы его ВНУТРЬ транзиентного пакета, и опыт мерил бы флаги
							//   чужого пакета. RF_Transient — чтобы он не попал ни в какое сохранение.
							UPackage* const ScratchPkg = CreatePackage(TEXT("/Temp/AsdaFxGlowF20Scratch"));
							ScratchPkg->SetFlags(RF_Transient);
							ScratchPkg->AddToRoot();
							// ★ КЛАСС ДЛЯ ОПЫТА: UObject::StaticClass() НЕ ГОДИТСЯ — он помечен
							//   abstract, и создание по нему срывает ensure
							//   (UObjectGlobals.cpp:3295). Берём UAsdaItemBrowserDebug: обычный
							//   `UCLASS()` без config-спецификаторов, то есть без CLASS_Config —
							//   ровно то, что нужно, чтобы исключение из маски Obj.cpp:1661 НЕ
							//   сработало и расхождение предикатов было видно в чистом виде.
							UObject* const Scratch = NewObject<UObject>(ScratchPkg, UAsdaItemBrowserDebug::StaticClass(),
								TEXT("F20ScratchObject"), RF_Transient | RF_Transactional);
							// ВЕТКА A / ВЕТКА B при СКРИПТОВОМ пакете
							ScratchPkg->SetPackageFlags(PKG_CompiledIn);
							const bool bOldPredScript = !ScratchPkg->HasAnyPackageFlags(PKG_ContainsScript);
							const bool bNewPredScript =
								!ScratchPkg->HasAnyPackageFlags(PKG_ContainsScript | PKG_CompiledIn)
								|| Scratch->GetClass()->HasAnyClassFlags(CLASS_DefaultConfig | CLASS_Config);
							bool bModifyScript = false;
							{
								FScopedTransaction TxS(FText::FromString(TEXT("[ред-тест] ФАКТ: Modify в compiled-in пакете")));
								bModifyScript = Scratch->Modify();
							}
							const bool bScratchDirtied = ScratchPkg->IsDirty();
							// КОНТРОЛЬ 1: тот же объект без флага
							ScratchPkg->ClearPackageFlags(PKG_CompiledIn);
							const bool bNewPredPlain =
								!ScratchPkg->HasAnyPackageFlags(PKG_ContainsScript | PKG_CompiledIn)
								|| Scratch->GetClass()->HasAnyClassFlags(CLASS_DefaultConfig | CLASS_Config);
							bool bModifyPlain = false;
							{
								FScopedTransaction TxP(FText::FromString(TEXT("[ред-тест] контроль: Modify в обычном пакете")));
								bModifyPlain = Scratch->Modify();
							}
							// КОНТРОЛЬ 2: НАШ АКТОР в compiled-in пакете — исключение config работает
							const bool bActorHasConfigFlags =
								Mk->GetClass()->HasAnyClassFlags(CLASS_DefaultConfig | CLASS_Config);
							OuterPkg->SetPackageFlags(PKG_CompiledIn);
							const bool bNewPredActor =
								!OuterPkg->HasAnyPackageFlags(PKG_ContainsScript | PKG_CompiledIn)
								|| bActorHasConfigFlags;
							OuterPkg->ClearPackageFlags(PKG_CompiledIn);
							// КОНТРОЛЬ 3: настоящий config-класс из /Script/. ★ Modify на нём НЕ ЗОВЁМ:
							//   у настроек владельца свой .ini, трогать его ради теста незачем.
							const UClass* const CfgCls = UAsdaFxLightSettings::StaticClass();
							const UPackage* const CfgPkg = CfgCls->GetDefaultObject()->GetOutermost();
							const bool bCfgPkgCompiledIn = CfgPkg->HasAnyPackageFlags(PKG_CompiledIn);
							const bool bCfgClassExempt = CfgCls->HasAnyClassFlags(CLASS_DefaultConfig | CLASS_Config);
							const bool bNewPredCfg =
								!CfgPkg->HasAnyPackageFlags(PKG_ContainsScript | PKG_CompiledIn) || bCfgClassExempt;
							ScratchPkg->RemoveFromRoot();
							S += FString::Printf(TEXT("[ред-тест] МЕЛК-1/Ф20 предикат «откат доступен» против факта UObject::Modify(): отдельный UObject в пакете с PKG_CompiledIn (тот флаг, что в жизни стоит на /Script/; PKG_ContainsScript не ставит никто). ВЕТКА A (предикат Ф19 `!PKG_ContainsScript`) — «%s»; ВЕТКА B (нынешний, маска Obj.cpp:1661 + исключение config) — «%s»; ФАКТ Modify() в открытой транзакции = %s, пакет им помечен %s. КОНТРОЛЬ 1 (флаг снят): предикат «%s», факт Modify() %s. КОНТРОЛЬ 2 (НАШ актор в compiled-in пакете): config-флаги класса %s (наследованы от AActor `config=Engine`), предикат «%s» — значит пакетным флагом отказ у этого класса не получить никогда. КОНТРОЛЬ 3 (%s из /Script/): compiled-in %s, config-флаги %s, предикат «%s» — %s\n"),
								bOldPredScript ? TEXT("ДОСТУПЕН") : TEXT("недоступен"),
								bNewPredScript ? TEXT("ДОСТУПЕН") : TEXT("недоступен"),
								bModifyScript ? TEXT("true") : TEXT("false"),
								bScratchDirtied ? TEXT("★ да") : TEXT("нет — верно"),
								bNewPredPlain ? TEXT("доступен") : TEXT("★ недоступен"),
								bModifyPlain ? TEXT("true") : TEXT("★ false"),
								bActorHasConfigFlags ? TEXT("есть") : TEXT("★ нет"),
								bNewPredActor ? TEXT("доступен") : TEXT("★ недоступен"),
								*CfgCls->GetName(),
								bCfgPkgCompiledIn ? TEXT("да") : TEXT("★ нет"),
								bCfgClassExempt ? TEXT("есть") : TEXT("★ нет"),
								bNewPredCfg ? TEXT("доступен") : TEXT("★ недоступен"),
								(bOldPredScript && !bNewPredScript && !bModifyScript && !bScratchDirtied
								 && bNewPredPlain && bModifyPlain
								 && bActorHasConfigFlags && bNewPredActor
								 && bCfgPkgCompiledIn && bCfgClassExempt && bNewPredCfg)
									? TEXT("верно: дефект воспроизведён (старый предикат обещал откат там, где Modify его не даёт) и закрыт; контроли на месте")
									: TEXT("★ ПРОВАЛ: предикат по-прежнему расходится с UObject::Modify"));
							Mk->ClearFlags(RF_NeedInitialization);   // снять подделку «откат недоступен»
							Mk->ClearFlags(RF_Transactional);
							OuterPkg->SetDirtyFlag(bPkgDirtyBefore);   // вернуть признак «изменён» как был
							S += FString::Printf(TEXT("[ред-тест] МЕЛК-1/Ф19 след отказа: затравка отменена %s, записей повтора %d (Ctrl+Y %s). ВЕТКА B (нынешний код): отказ снял %d записей (осталось %d из %d), пакет помечен %s, флаг RF_Transactional поставлен %s, записей повтора %d (Ctrl+Y %s). ВЕТКА A (порядок Ф18): ВНУТРИ открытой транзакции записей повтора %d (Ctrl+Y %s), ПОСЛЕ закрытия %d (Ctrl+Y %s), флаг поставлен %s — %s\n"),
								bSeedUndone ? TEXT("да") : TEXT("★ НЕТ"),
								RedoSeeded, bCanRedoSeeded ? TEXT("да") : TEXT("★ НЕТ"),
								RefusedF, AfterRefuseF, FullF,
								bPkgDirtiedByRefuse ? TEXT("★ да") : TEXT("нет — верно"),
								bFlagAfterNew ? TEXT("★ да") : TEXT("нет — верно"),
								RedoAfterNew, bCanRedoAfterNew ? TEXT("цел") : TEXT("★ стёрт"),
								RedoInsideOld, bCanRedoInsideOld ? TEXT("★ цел") : TEXT("вынут"),
								RedoAfterOld, bCanRedoAfterOld ? TEXT("возвращён") : TEXT("★ стёрт"),
								bFlagAfterOld ? TEXT("да — вот он, след") : TEXT("★ НЕТ"),
								(bSeedUndone && RedoSeeded > 0 && bCanRedoSeeded
								 && RefusedF == 0 && AfterRefuseF == FullF && !bFlagAfterNew && !bPkgDirtiedByRefuse
								 && RedoAfterNew == RedoSeeded && bCanRedoAfterNew
								 && RedoInsideOld == 0 && !bCanRedoInsideOld
								 && RedoAfterOld == RedoSeeded && bCanRedoAfterOld && bFlagAfterOld)
									? TEXT("верно: нынешний отказ пуст (ни флага, ни хода буфера). ПОПРАВКА К РЕВИЗИИ: довод «отказ Ф18 терял стек повтора» НЕ ПОДТВЕРДИЛСЯ — Begin записи выносит, но End пустой транзакции их возвращает (IsTransient, EditorTransaction.cpp:515-527 и 1332-1353); настоящим следом был флаг RF_Transactional")
									: TEXT("★ ПРОВАЛ: замеры разошлись с разбором — см. числа выше"));

							Mk->EmitterGlowSaved.Reset();
						}
						// Вернуть буфер отмены ровно в то состояние, в каком он был до проверки
						if (Temp)
						{
							Temp->Reset(FText::FromString(TEXT("[ред-тест] временный буфер отмены снят")));
							GEditor->Trans = PrevTrans;
							Temp->RemoveFromRoot();
						}
						S += FString::Printf(TEXT("[ред-тест] С-1/Ф17 контроль: GEditor->Trans возвращён в %s\n"),
							(GEditor->Trans == PrevTrans) ? TEXT("прежнее значение — верно") : TEXT("★ НЕ ВОССТАНОВЛЕНО"));
						// ★ Набор «наша транзакция» конфигуратора мог наполниться из PostEditUndo
						//   наших отмен: у временного буфера нет подписки OnUndo, которая его съедает.
						//   Чистим явно, чтобы следующий тест начинал с пустого (и не унаследовал наш след).
						FAsdaFxGlowConfigModel::BeginUndoOperation();
					}
				}
			}

			// ═══ ★★★ ВОЛНА Ф14 (С-1): Ctrl+Z ПОСЛЕ «ВЕРНУТЬ ЭМИТТЕР КАК В АССЕТЕ» ════════════════
			//   ЧТО ДОКАЗЫВАЕТСЯ. Цепочка «правка → Вернуть как в ассете → Ctrl+Z» не должна
			//   оставлять размещённые экземпляры с розданным числом: экземпляр ≠ архетип — это
			//   дельта, которую сохранение карты впечатает в .umap.
			//   ПОЧЕМУ ОТМЕНА ИЗОБРАЖАЕТСЯ, А НЕ НАЖИМАЕТСЯ. Буфера отмены в коммандлете нет
			//   (GEditor->Trans == null, см. К2 выше). Но отмена делает с CDO РОВНО ОДНО —
			//   возвращает ему прежние числа, а экземпляров не касается; это и воспроизводится.
			//   Дальше зовётся ТА ЖЕ функция, что подписана на FEditorDelegates::PostUndoRedo.
			//   СТАРЫЙ ЗАКОН измеряется, а не пересказывается: проверяется, осталась ли запись в
			//   складе снимков, по которому прежде и шла пересинхронизация.
			if (Cdo && Fld && Fld->Prop && Cdo->Emitters.IsValidIndex(R.Index))
			{
				AAsdaEffectActor* Inst = W->SpawnActor<AAsdaEffectActor>(Cls, Sp);
				if (!Inst || !Inst->Emitters.IsValidIndex(R.Index))
				{
					S += TEXT("[ред-тест] С-1: пропущено (экземпляр не создался)\n");
				}
				else
				{
					const FAsdaFxEmitterGlow AssetGlow = Cdo->Emitters[R.Index].Glow;   // «как в ассете» сейчас
					// (1) правка: число уезжает и в CDO, и в размещённый экземпляр
					FAsdaFxLightSpec T1 = M.EmitterWork(R);
					const float Pushed = T1.IntensityCd + 21.f;
					T1.IntensityCd = Pushed;
					M.SetEmitterField(R, *Fld, T1);
					const bool bInstGot = FMath::IsNearlyEqual(Inst->Emitters[R.Index].Glow.Spec.IntensityCd, Pushed);
					// (2) «Вернуть эмиттер как в ассете» — путь, который предлагает модальный диалог
					M.RevertEmitterToAsset(R);
					const bool bTouchGone = !M.IsFxAssetEdited(R.ClassPath);
					// (3) отмена: CDO возвращается к правке, экземпляров отмена не касается
					Cdo->Emitters[R.Index].Glow.Source = EAsdaFxEmitterGlowSource::Manual;
					Cdo->Emitters[R.Index].Glow.Spec.IntensityCd = Pushed;
					const float InstBeforeResync = Inst->Emitters[R.Index].Glow.Spec.IntensityCd;
					const bool bDeltaBefore = !FMath::IsNearlyEqual(InstBeforeResync, Pushed);
					// ═══ ★★★ ВОЛНА Ф15 (С-2): ЧУЖАЯ ОТМЕНА НЕ ДОЛЖНА ТРОГАТЬ НИЧЕГО ═════════════
					//   Контрольная пара, которой у Ф14 не было. Ctrl+Z по ПОСТОРОННЕМУ действию
					//   (например, отмена передвижения актора) не зовёт PostEditUndo у нашего CDO —
					//   значит, пересинхронизации быть не должно. Прежде она шла по всему складу
					//   раздач и писала в чужие акторы вне связи с отменяемым действием.
					const int32 ResyncedForeign = FAsdaFxGlowConfigModel::ResyncAfterUndo();
					const float InstAfterForeign = Inst->Emitters[R.Index].Glow.Spec.IntensityCd;
					S += FString::Printf(TEXT("[ред-тест] С-2 (Ф15) ЧУЖАЯ отмена (наш CDO в транзакции не участвовал): пересинхронизировано %d, экземпляр %.2f → %.2f — %s\n"),
						ResyncedForeign, InstBeforeResync, InstAfterForeign,
						(ResyncedForeign == 0 && FMath::IsNearlyEqual(InstAfterForeign, InstBeforeResync))
							? TEXT("верно: чужой Ctrl+Z в чужие акторы не пишет")
							: TEXT("★ ПРОВАЛ: пересинхронизация сработала на посторонней отмене"));
					// (4) то, что делает подписка на PostUndoRedo — но теперь ТОЛЬКО после того, как
					//     движок сообщил, что объект этого эффекта был в применённой транзакции
					//     (AAsdaEffectActor::PostEditUndo → NotePostEditUndo)
					FAsdaFxGlowConfigModel::NotePostEditUndo(Cdo);
					const int32 Resynced = FAsdaFxGlowConfigModel::ResyncAfterUndo();
					const float InstAfter = Inst->Emitters[R.Index].Glow.Spec.IntensityCd;
					const bool bNoDelta = FMath::IsNearlyEqual(InstAfter, Pushed);
					S += FString::Printf(TEXT("[ред-тест] С-1 A/B СТАРЫЙ ЗАКОН (проход по складу СНИМКОВ): после «Вернуть как в ассете» запись о тронутом ассете %s ⇒ прежней пересинхронизации досталось бы %s\n"),
						bTouchGone ? TEXT("УДАЛЕНА") : TEXT("на месте"),
						bTouchGone ? TEXT("НИЧЕГО — дельта уезжает в .umap") : TEXT("что-то (дефект не воспроизведён)"));
					S += FString::Printf(TEXT("[ред-тест] С-1 A/B НОВЫЙ ЗАКОН (проход по складу РАЗДАЧ): правка доехала до экземпляра %s; после отмены экземпляр был %.2f при архетипе %.2f (дельта %s), пересинхронизировано %d, стало %.2f — %s\n"),
						bInstGot ? TEXT("да") : TEXT("★ НЕТ"),
						InstBeforeResync, Pushed, bDeltaBefore ? TEXT("есть") : TEXT("★ НЕТ (проверять нечего)"),
						Resynced, InstAfter,
						(bInstGot && bTouchGone && bDeltaBefore && bNoDelta) ? TEXT("верно: экземпляр снова равен архетипу, .umap чист")
							: TEXT("★ ПРОВАЛ: дельта «экземпляр ≠ архетип» осталась"));
					// ═══ ★★★ ВОЛНА Ф15 (С-1): ПОИСК ПО КЛЮЧУ ПОСЛЕ RENAME/MOVE АССЕТА ═══════════
					//   ЧТО ДОКАЗЫВАЕТСЯ. Ключ склада — путь класса. После переименования или
					//   переноса ассета дерево отдаёт НОВЫЙ путь, а в складе лежит СТАРЫЙ. Голый
					//   TMap::Find промахивается, и молча ломается всё: отметка «*», снимок «как в
					//   ассете» (заводится ВТОРАЯ запись уже с правленого CDO), снятие признака
					//   «изменён». Переименование изображается перекладыванием записи под заведомо
					//   несуществующий путь — для поиска это ровно тот же случай.
					{
						// Запись под НЫНЕШНИМ путём (если она есть) отложим и вернём как было
						const bool bHadLive = FAsdaFxGlowConfigModel::TouchedFx().Contains(R.ClassPath);
						FAsdaFxGlowConfigModel::FAsdaGlowFxTouch Saved;
						if (bHadLive) { Saved = FAsdaFxGlowConfigModel::TouchedFx()[R.ClassPath]; FAsdaFxGlowConfigModel::TouchedFx().Remove(R.ClassPath); }
						// Запись-зонд: ровно то, что остаётся в складе после Rename/Move — слабая
						// ссылка на ЖИВОЙ класс при УСТАРЕВШЕМ ключе. CDO при этом не трогается
						FAsdaFxGlowConfigModel::FAsdaGlowFxTouch RenameProbe;
						RenameProbe.FxClass = Cdo->GetClass();
						RenameProbe.Baseline.Add(R.Index, Cdo->Emitters[R.Index].Glow);
						RenameProbe.SavedBaseline = Cdo->EmitterGlowSaved;
						RenameProbe.bSavedBaselineTaken = true;
						const FString StaleKey = R.ClassPath + TEXT("__ПУТЬ_ДО_ПЕРЕИМЕНОВАНИЯ__");
						FAsdaFxGlowConfigModel::TouchedFx().Add(StaleKey, RenameProbe);
						// СТАРЫЙ ЗАКОН — голый поиск по ключу, ровно как было в одиннадцати местах
						const bool bOldFound = FAsdaFxGlowConfigModel::TouchedFx().Contains(R.ClassPath);
						// НОВЫЙ ЗАКОН — тот же вход, которым теперь пользуются все
						const FAsdaFxGlowConfigModel::FAsdaGlowFxTouch* Got = FAsdaFxGlowConfigModel::FindTouch(R.ClassPath);
						const bool bNewFound = Got != nullptr;
						const bool bBaselineKept = bNewFound && Got->Baseline.Contains(R.Index);
						const bool bRekeyed = FAsdaFxGlowConfigModel::TouchedFx().Contains(R.ClassPath)
							&& !FAsdaFxGlowConfigModel::TouchedFx().Contains(StaleKey);
						S += FString::Printf(TEXT("[ред-тест] С-1 (Ф15) Rename/Move: старый закон (Find по нынешнему пути) нашёл запись: %s; новый закон нашёл: %s (снимок «как в ассете» на месте: %s) и переложил её под нынешний путь: %s — %s\n"),
							bOldFound ? TEXT("да (дефект не воспроизведён)") : TEXT("НЕТ — дефект воспроизведён"),
							bNewFound ? TEXT("да") : TEXT("★ НЕТ"),
							bBaselineKept ? TEXT("да") : TEXT("★ НЕТ"),
							bRekeyed ? TEXT("да") : TEXT("★ НЕТ"),
							(!bOldFound && bNewFound && bBaselineKept && bRekeyed)
								? TEXT("верно: склад переживает переименование ассета")
								: TEXT("★ ПРОВАЛ: после Rename/Move запись склада недостижима"));
						// Вернуть склад ровно в то состояние, в каком он был до зонда
						FAsdaFxGlowConfigModel::TouchedFx().Remove(StaleKey);
						FAsdaFxGlowConfigModel::TouchedFx().Remove(R.ClassPath);
						if (bHadLive) { FAsdaFxGlowConfigModel::TouchedFx().Add(R.ClassPath, Saved); }
					}

					// ═══ ★★★ ВОЛНА Ф16 (СРЕДН-2): КОЛЛИЗИЯ ДВУХ АССЕТОВ НА ОДНОМ ПУТИ ═══════════
					//   ЧТО ДОКАЗЫВАЕТСЯ. Ф15 научила поиск переживать Rename/Move, но БЫСТРАЯ ветка
					//   отдавала запись по ключу, ничего не сверяя. Путь класса переиспользуем:
					//   ассет A уехал с пути P, ассет B переименован НА P — и запись A достаётся B.
					//   Снимки смешиваются, «Вернуть как в ассете» впишет в B числа A.
					//   ДВА ЖИВЫХ КЛАССА берутся настоящими: A — класс запечённого BP_Fx (его путь
					//   R.ClassPath), B — сам AAsdaEffectActor (путь /Script/...). Оба разрешаются
					//   FindObject, то есть проверка идёт ровно тем механизмом, что и в жизни.
					{
						UClass* ClsA = Cdo->GetClass();
						UClass* ClsB = AAsdaEffectActor::StaticClass();
						const FString PathA = ClsA->GetPathName();
						const FString PathB = ClsB->GetPathName();
						// Отложить и вернуть всё, что лежало по этим двум ключам
						const bool bHadA = FAsdaFxGlowConfigModel::TouchedFx().Contains(PathA);
						const bool bHadB = FAsdaFxGlowConfigModel::TouchedFx().Contains(PathB);
						FAsdaFxGlowConfigModel::FAsdaGlowFxTouch SaveA, SaveB;
						if (bHadA) { SaveA = FAsdaFxGlowConfigModel::TouchedFx()[PathA]; }
						if (bHadB) { SaveB = FAsdaFxGlowConfigModel::TouchedFx()[PathB]; }
						FAsdaFxGlowConfigModel::TouchedFx().Remove(PathA);
						FAsdaFxGlowConfigModel::TouchedFx().Remove(PathB);

						// Запись АССЕТА A, лежащая под путём, где теперь живёт ассет B
						const float MarkA = 1234.f;
						FAsdaFxGlowConfigModel::FAsdaGlowFxTouch ProbeA;
						ProbeA.FxClass = ClsA;
						{
							FAsdaFxEmitterGlow G; G.Source = EAsdaFxEmitterGlowSource::Manual; G.Spec.IntensityCd = MarkA;
							ProbeA.Baseline.Add(0, G);
						}
						ProbeA.bSavedBaselineTaken = true;
						FAsdaFxGlowConfigModel::TouchedFx().Add(PathB, ProbeA);

						// СТАРЫЙ ЗАКОН — голый Find по ключу: отдаёт запись A для ассета B
						const FAsdaFxGlowConfigModel::FAsdaGlowFxTouch* OldGot = FAsdaFxGlowConfigModel::TouchedFx().Find(PathB);
						const bool bOldGaveForeign = OldGot && OldGot->FxClass.Get() == ClsA;
						// НОВЫЙ ЗАКОН — тот же вход, которым пользуются все одиннадцать мест
						const FAsdaFxGlowConfigModel::FAsdaGlowFxTouch* NewGot = FAsdaFxGlowConfigModel::FindTouch(PathB);
						const bool bNewGaveForeign = NewGot != nullptr;
						// Чужая запись не выброшена, а переложена под СВОЙ путь
						const FAsdaFxGlowConfigModel::FAsdaGlowFxTouch* Moved = FAsdaFxGlowConfigModel::TouchedFx().Find(PathA);
						const bool bMovedOk = Moved && Moved->FxClass.Get() == ClsA
							&& Moved->Baseline.Contains(0)
							&& FMath::IsNearlyEqual(Moved->Baseline[0].Spec.IntensityCd, MarkA);
						S += FString::Printf(TEXT("[ред-тест] СРЕДН-2 коллизия путей (запись ассета %s лежит под путём ассета %s): старый закон отдал ЧУЖУЮ запись: %s; новый закон отдал: %s; чужая запись сохранена под своим путём: %s — %s\n"),
							*ClsA->GetName(), *ClsB->GetName(),
							bOldGaveForeign ? TEXT("да — дефект воспроизведён") : TEXT("★ ПРОВАЛ: дефект не воспроизводится, A/B бессмысленно"),
							bNewGaveForeign ? TEXT("★ ТОЖЕ ЧУЖУЮ") : TEXT("ничего — верно"),
							bMovedOk ? TEXT("да") : TEXT("★ ПОТЕРЯНА"),
							(bOldGaveForeign && !bNewGaveForeign && bMovedOk)
								? TEXT("верно: снимки двух ассетов не смешиваются")
								: TEXT("★ ПРОВАЛ: запись одного ассета достаётся другому"));

						// КОНТРОЛЬНАЯ ПАРА №1: по СВОЕМУ пути запись A по-прежнему находится
						const FAsdaFxGlowConfigModel::FAsdaGlowFxTouch* OwnGot = FAsdaFxGlowConfigModel::FindTouch(PathA);
						S += FString::Printf(TEXT("[ред-тест] СРЕДН-2 контроль 1 (свой путь): запись ассета %s найдена: %s, снимок цел: %s\n"),
							*ClsA->GetName(), OwnGot ? TEXT("да") : TEXT("★ НЕТ"),
							(OwnGot && OwnGot->Baseline.Contains(0) && FMath::IsNearlyEqual(OwnGot->Baseline[0].Spec.IntensityCd, MarkA))
								? TEXT("да") : TEXT("★ НЕТ"));

						// КОНТРОЛЬНАЯ ПАРА №2: ДУБЛЬ — две записи одного класса. Перебор обязан
						//   выбрать ДЕТЕРМИНИРОВАННО (ту, чей ключ равен нынешнему пути), а не по
						//   порядку обхода TMap, который не определён
						const FString DupKey = PathA + TEXT("__ДУБЛЬ__");
						FAsdaFxGlowConfigModel::FAsdaGlowFxTouch Dup;
						Dup.FxClass = ClsA;
						{
							FAsdaFxEmitterGlow G; G.Source = EAsdaFxEmitterGlowSource::Manual; G.Spec.IntensityCd = 4321.f;
							Dup.Baseline.Add(0, G);
						}
						FAsdaFxGlowConfigModel::TouchedFx().Add(DupKey, Dup);
						const FAsdaFxGlowConfigModel::FAsdaGlowFxTouch* PickA = FAsdaFxGlowConfigModel::FindTouch(PathA);
						const bool bPickedOwn = PickA && PickA->Baseline.Contains(0)
							&& FMath::IsNearlyEqual(PickA->Baseline[0].Spec.IntensityCd, MarkA);
						S += FString::Printf(TEXT("[ред-тест] СРЕДН-2 контроль 2 (дубль записей одного класса): выбрана запись с %s — %s\n"),
							PickA && PickA->Baseline.Contains(0) ? *FString::SanitizeFloat(PickA->Baseline[0].Spec.IntensityCd) : TEXT("<нет снимка>"),
							bPickedOwn ? TEXT("верно: взята запись под нынешним путём, выбор не зависит от порядка обхода")
								: TEXT("★ ПРОВАЛ: выбор записи при дубле недетерминирован"));

						// ═══ ★★★ ВОЛНА Ф17 (МЕЛК-2): ВЫБОР ПРИ ДУБЛЕ, КОГДА НИ ОДИН КЛЮЧ НЕ РАВЕН ═══
						//   ЧТО ДОКАЗЫВАЕТСЯ. Ф16 задала приоритет только ключу, РАВНОМУ нынешнему
						//   пути. Если такого нет (ассет переименован дважды), выбор снова падал на
						//   порядок обхода TMap — а он зависит от истории вставок и удалений.
						//   Проверка честная: ОДИН И ТОТ ЖЕ набор кладётся в склад ДВАЖДЫ, в
						//   ПРОТИВОПОЛОЖНОМ порядке вставки. Недетерминированный выбор дал бы разные
						//   ответы; детерминированный обязан дать один и тот же — лексикографически
						//   меньший ключ.
						auto MakeProbe = [](UClass* C, float MarkVal)
						{
							FAsdaFxGlowConfigModel::FAsdaGlowFxTouch P;
							P.FxClass = C;
							FAsdaFxEmitterGlow G;
							G.Source = EAsdaFxEmitterGlowSource::Manual;
							G.Spec.IntensityCd = MarkVal;
							P.Baseline.Add(0, G);
							P.bSavedBaselineTaken = true;
							return P;
						};
						auto MarkOf = [](const FAsdaFxGlowConfigModel::FAsdaGlowFxTouch* T) -> float
						{
							return (T && T->Baseline.Contains(0)) ? T->Baseline[0].Spec.IntensityCd : -1.f;
						};
						// ★ Ф17: склад приводится к ЧИСТОМУ состоянию. «Контроль 2» выше оставил в нём
						//   запись под DupKey — она тоже принадлежит классу A, и проверки ниже, которые
						//   СЧИТАЮТ совпадения по классу, мерили бы не то, что ставят. Прогон try1 это и
						//   поймал: ветка «старый закон» в проверке пустого ключа нашла DupKey и сказала
						//   «дефект не воспроизводится», хотя дефект был не при чём.
						FAsdaFxGlowConfigModel::TouchedFx().Remove(DupKey);
						{
							const FString KeyLo = PathA + TEXT("__AAA__");   // лексикографически МЕНЬШИЙ
							const FString KeyHi = PathA + TEXT("__ZZZ__");
							const float MarkLo = 111.f, MarkHi = 222.f;
							float Picked[2] = { -1.f, -1.f };
							for (int32 Pass = 0; Pass < 2; ++Pass)
							{
								FAsdaFxGlowConfigModel::TouchedFx().Remove(PathA);
								FAsdaFxGlowConfigModel::TouchedFx().Remove(KeyLo);
								FAsdaFxGlowConfigModel::TouchedFx().Remove(KeyHi);
								// Порядок вставки в проходах ПРОТИВОПОЛОЖНЫЙ — им и правится обход TMap
								if (Pass == 0)
								{
									FAsdaFxGlowConfigModel::TouchedFx().Add(KeyHi, MakeProbe(ClsA, MarkHi));
									FAsdaFxGlowConfigModel::TouchedFx().Add(KeyLo, MakeProbe(ClsA, MarkLo));
								}
								else
								{
									FAsdaFxGlowConfigModel::TouchedFx().Add(KeyLo, MakeProbe(ClsA, MarkLo));
									FAsdaFxGlowConfigModel::TouchedFx().Add(KeyHi, MakeProbe(ClsA, MarkHi));
								}
								Picked[Pass] = MarkOf(FAsdaFxGlowConfigModel::FindTouch(PathA));
							}
							FAsdaFxGlowConfigModel::TouchedFx().Remove(PathA);
							FAsdaFxGlowConfigModel::TouchedFx().Remove(KeyLo);
							FAsdaFxGlowConfigModel::TouchedFx().Remove(KeyHi);
							S += FString::Printf(TEXT("[ред-тест] МЕЛК-2/Ф17 дубль БЕЗ точного ключа (два переименования подряд): проход 1 выбрал %.0f, проход 2 (обратный порядок вставки) выбрал %.0f, ожидался меньший ключ (%.0f) — %s\n"),
								Picked[0], Picked[1], MarkLo,
								(FMath::IsNearlyEqual(Picked[0], MarkLo) && FMath::IsNearlyEqual(Picked[1], MarkLo))
									? TEXT("верно: выбор не зависит от порядка обхода TMap")
									: TEXT("★ ПРОВАЛ: выбор записи при дубле недетерминирован"));
						}

						// ═══ ★★★ ВОЛНА Ф17 (МЕЛК-2): ЗАПИСЬ ПОД ПУСТЫМ КЛЮЧОМ НЕ ТЕРЯЕТСЯ ═══════
						//   ЧТО ДОКАЗЫВАЕТСЯ. `StaleKey.IsEmpty()` служила сторожем «ключ ещё не
						//   выбран», поэтому запись, лежащая под ПУСТЫМ ключом, выбиралась и тут же
						//   объявлялась ненайденной — единственный снимок «как в ассете» пропадал
						//   молча, а EmitterEditBegin завёл бы второй, снятый с уже правленого CDO.
						{
							const float MarkEmpty = 909.f;
							FAsdaFxGlowConfigModel::TouchedFx().Remove(PathA);
							FAsdaFxGlowConfigModel::TouchedFx().Remove(FString());
							FAsdaFxGlowConfigModel::TouchedFx().Add(FString(), MakeProbe(ClsA, MarkEmpty));
							// Старый закон дословно: сторожем служила пустота самого ключа
							FString OldStale;
							for (const TPair<FString, FAsdaFxGlowConfigModel::FAsdaGlowFxTouch>& P : FAsdaFxGlowConfigModel::TouchedFx())
							{
								if (P.Value.FxClass.Get() != ClsA) { continue; }
								if (OldStale.IsEmpty() || P.Key == PathA) { OldStale = P.Key; }
							}
							const bool bOldLost = OldStale.IsEmpty();   // ⇒ старый код вернул бы nullptr
							const float GotEmpty = MarkOf(FAsdaFxGlowConfigModel::FindTouch(PathA));
							const bool bRekeyed = FAsdaFxGlowConfigModel::TouchedFx().Contains(PathA)
								&& !FAsdaFxGlowConfigModel::TouchedFx().Contains(FString());
							FAsdaFxGlowConfigModel::TouchedFx().Remove(PathA);
							FAsdaFxGlowConfigModel::TouchedFx().Remove(FString());
							S += FString::Printf(TEXT("[ред-тест] МЕЛК-2/Ф17 запись под ПУСТЫМ ключом: старый закон объявил бы её ненайденной %s; новый вернул снимок %.0f (ожидалось %.0f) и переложил под нынешний путь %s — %s\n"),
								bOldLost ? TEXT("да — дефект воспроизведён") : TEXT("★ НЕТ (дефект не воспроизводится)"),
								GotEmpty, MarkEmpty,
								bRekeyed ? TEXT("да") : TEXT("★ НЕТ"),
								(bOldLost && FMath::IsNearlyEqual(GotEmpty, MarkEmpty) && bRekeyed)
									? TEXT("верно: пустой ключ — обычный ключ, снимок не теряется")
									: TEXT("★ ПОТЕРЯНА: запись под пустым ключом по-прежнему пропадает"));
						}

						// ═══ ★★★ ВОЛНА Ф17: СОБСТВЕННЫЙ ПУТЬ УЕХАВШЕГО АССЕТА ТОЖЕ ЗАНЯТ ════════
						//   Ветка bKeyHeldByForeigner (Ф16, СРЕДН-2) была написана, но НИ РАЗУ НЕ
						//   ПРОВЕРЕНА. Случай — цепочка переименований: запись ассета A лежит под
						//   путём PathB (там теперь живёт B), а собственный путь A (PathA) занят
						//   записью самого B. Переложить нечего: обе записи настоящие, выбрасывать
						//   нельзя ни одну. Ожидание: FindTouch(PathB) отдаёт запись КЛАССА B по её
						//   нынешнему (устаревшему) ключу PathA, чужую запись A под PathB не трогает
						//   и ничего не теряет.
						{
							const float MarkForeign = 1717.f;   // запись ассета A, лежит под PathB
							const float MarkOwner = 1818.f;     // запись ассета B, лежит под PathA
							FAsdaFxGlowConfigModel::TouchedFx().Remove(PathA);
							FAsdaFxGlowConfigModel::TouchedFx().Remove(PathB);
							FAsdaFxGlowConfigModel::TouchedFx().Add(PathB, MakeProbe(ClsA, MarkForeign));
							FAsdaFxGlowConfigModel::TouchedFx().Add(PathA, MakeProbe(ClsB, MarkOwner));
							const FAsdaFxGlowConfigModel::FAsdaGlowFxTouch* GotB = FAsdaFxGlowConfigModel::FindTouch(PathB);
							const float GotMark = MarkOf(GotB);
							const bool bRightClass = GotB && GotB->FxClass.Get() == ClsB;
							// Чужая запись A осталась на месте и цела — её терять было нельзя
							const FAsdaFxGlowConfigModel::FAsdaGlowFxTouch* StillA = FAsdaFxGlowConfigModel::TouchedFx().Find(PathB);
							const bool bForeignKept = StillA && StillA->FxClass.Get() == ClsA
								&& FMath::IsNearlyEqual(MarkOf(StillA), MarkForeign);
							FAsdaFxGlowConfigModel::TouchedFx().Remove(PathA);
							FAsdaFxGlowConfigModel::TouchedFx().Remove(PathB);
							S += FString::Printf(TEXT("[ред-тест] МЕЛК-2/Ф17 собственный путь уехавшего ассета ТОЖЕ занят: отдан снимок %.0f (ожидался %.0f — запись класса %s), класс совпал %s, чужая запись под %s сохранена %s — %s\n"),
								GotMark, MarkOwner, *ClsB->GetName(),
								bRightClass ? TEXT("да") : TEXT("★ НЕТ"),
								*PathB, bForeignKept ? TEXT("да") : TEXT("★ ПОТЕРЯНА"),
								(bRightClass && FMath::IsNearlyEqual(GotMark, MarkOwner) && bForeignKept)
									? TEXT("верно: ветка «ключ занят чужим» отдаёт свою запись по старому ключу и ничего не теряет")
									: TEXT("★ ПРОВАЛ: цепочка переименований путает снимки"));
						}

						// Вернуть склад ровно в то состояние, в каком он был до зонда
						FAsdaFxGlowConfigModel::TouchedFx().Remove(DupKey);
						FAsdaFxGlowConfigModel::TouchedFx().Remove(PathA);
						FAsdaFxGlowConfigModel::TouchedFx().Remove(PathB);
						if (bHadA) { FAsdaFxGlowConfigModel::TouchedFx().Add(PathA, SaveA); }
						if (bHadB) { FAsdaFxGlowConfigModel::TouchedFx().Add(PathB, SaveB); }
					}
					// вернуть CDO ровно туда, где он был до этой проверки
					Cdo->Emitters[R.Index].Glow = AssetGlow;
					Cdo->SaveEmitterGlow(/*bAuthoritative=*/true);
				}
			}

			// Убираем за собой: мир и акторы
			W->DestroyWorld(false);

			// ═══ ★★★ РЕВИЗИЯ Ф13 (К1, 20.09.2026): A/B «АВТОСЕЙВ ПРОТИВ НАСТОЯЩЕЙ ЗАПИСИ» ═══════
			//   Зовётся ТОТ ЖЕ обработчик, что подписан на UPackage::PackageSavedWithContextEvent
			//   (FAsdaFxGlowConfigModel::HandlePackageSaved) — не его пересказ. Отличаются только
			//   флаги контекста: SAVE_FromAutosave против SAVE_None. Разослать событие глобально
			//   нельзя: на него подписаны реестр ассетов и автосейвер редактора, и поддельная
			//   рассылка сбила бы их состояние.
			//   ВЕТКА A (автосейв): .uasset не записан ⇒ снимок «как в ассете» обязан УЦЕЛЕТЬ, и
			//     «Вернуть эмиттер как в ассете» обязана вернуть ИСХОДНОЕ число.
			//   ВЕТКА B (Ctrl+S / Save All): на диске теперь новое ⇒ снимок обязан ПЕРЕСНЯТЬСЯ, и
			//     возврат уже НЕ должен выбрасывать записанную работу.
			if (Cdo && Pkg && Fld && Fld->Prop)
			{
				const FString ClassPath = R.ClassPath;
				// ★ «Как в ассете» по ходу A/B МЕНЯЕТСЯ: настоящая запись обязана переснять снимок.
				//   Поэтому ожидаемое значение возврата ведётся отдельной переменной, а не берётся
				//   один раз в начале — иначе ветка B проверяла бы не то, что проверяет
				float BaseNow = M.EmitterWork(R).IntensityCd;
				auto Edit = [&M, &R, Fld](float V)
				{
					FAsdaFxLightSpec T = M.EmitterWork(R);
					T.IntensityCd = V;
					M.SetEmitterField(R, *Fld, T);
				};
				// Контекст записи: поля структуры публичны, обработчик читает ровно их
				auto CallSaved = [Pkg](uint32 SaveFlags, bool bProcedural, const TCHAR* File)
				{
					FObjectSaveContextData D;
					D.TargetFilename = File;
					D.SaveFlags = SaveFlags;
					D.bProceduralSave = bProcedural;
					D.bSaveSucceeded = true;
					FAsdaFxGlowConfigModel::HandlePackageSaved(File, Pkg, FObjectPostSaveContext(D));
				};

				// ★★★ СТАРЫЙ ЗАКОН — ДОСЛОВНО, РЯДОМ. Отсечка волны Ф12 была одна: «процедурный сейв
				//   снимок не отменяет». Здесь она считается на ТЕХ ЖЕ контекстах, и видно, что на
				//   автосейве она давала «это настоящая запись» — то есть снимок выбрасывался.
				//   Без этой строки A/B доказывал бы только сам себя
				auto OldWouldRetake = [](uint32 SaveFlags, bool bProcedural)
				{
					(void)SaveFlags;                 // прежний код флаги записи не смотрел вовсе
					return !bProcedural;             // `if (!Pkg || Ctx.IsProceduralSave()) return;`
				};
				S += FString::Printf(TEXT("[ред-тест] К1 A/B СТАРЫЙ ЗАКОН на тех же контекстах: автосейв -> «%s», Ctrl+S -> «%s», кук -> «%s» (автосейв и был дырой)\n"),
					OldWouldRetake(SAVE_FromAutosave, false) ? TEXT("снимок ВЫБРОСИТЬ") : TEXT("не трогать"),
					OldWouldRetake(SAVE_None, false) ? TEXT("снимок ВЫБРОСИТЬ") : TEXT("не трогать"),
					OldWouldRetake(SAVE_None, true) ? TEXT("снимок ВЫБРОСИТЬ") : TEXT("не трогать"));

				// ── ВЕТКА A: автосейв. Снимок обязан уцелеть, возврат — дать исходное ──
				Edit(BaseNow + 3.f);
				CallSaved(SAVE_FromAutosave, /*bProcedural=*/false, TEXT("../Saved/Autosaves/Game/Auto1.uasset"));
				const bool bKeptAfterAuto = M.HasEmitterBaseline(R);
				M.RevertEmitterToAsset(R);
				const float AfterAuto = M.EmitterWork(R).IntensityCd;
				S += FString::Printf(TEXT("[ред-тест] К1 A/B ВЕТКА A (АВТОСЕЙВ, SAVE_FromAutosave): снимок %s; возврат дал %.2f при «как в ассете» %.2f — %s\n"),
					bKeptAfterAuto ? TEXT("уцелел") : TEXT("★ ВЫБРОШЕН"), AfterAuto, BaseNow,
					(bKeptAfterAuto && FMath::IsNearlyEqual(AfterAuto, BaseNow)) ? TEXT("верно") : TEXT("★ ПРОВАЛ: автосейв всё ещё принимается за запись ассета"));

				// ── ВЕТКА B: настоящая запись. Снимок обязан ПЕРЕСНЯТЬСЯ ──
				Edit(BaseNow + 4.f);
				CallSaved(SAVE_None, /*bProcedural=*/false, TEXT("../Content/Test/BP_Fx_Test.uasset"));
				const bool bKeptAfterSave = M.HasEmitterBaseline(R);
				const bool bPendingCleared = !M.IsEmitterPending(R);
				const float WantB = BaseNow + 4.f;
				M.RevertEmitterToAsset(R);
				const float AfterSave = M.EmitterWork(R).IntensityCd;
				S += FString::Printf(TEXT("[ред-тест] К1 A/B ВЕТКА B (Ctrl+S / Save All, SAVE_None): снимок %s, отметка * %s; возврат дал %.2f при записанных %.2f — %s\n"),
					bKeptAfterSave ? TEXT("уцелел (переснят)") : TEXT("★ ПОТЕРЯН — кнопка возврата умерла до следующей правки"),
					bPendingCleared ? TEXT("снята") : TEXT("★ ОСТАЛАСЬ"),
					AfterSave, WantB,
					FMath::IsNearlyEqual(AfterSave, WantB) ? TEXT("верно: записанная работа не выброшена") : TEXT("★ ПРОВАЛ: возврат отбросил уже записанное"));
				BaseNow = WantB;   // на диске (по версии теста) теперь это

				// ── ВЕТКА C: кук/EditorDomain. Не трогает НИЧЕГО ──
				Edit(BaseNow + 7.f);
				CallSaved(SAVE_None, /*bProcedural=*/true, TEXT("../Cooked/BP_Fx_Test.uasset"));
				const bool bCookKept = M.HasEmitterBaseline(R);
				const bool bCookPending = M.IsEmitterPending(R);
				M.RevertEmitterToAsset(R);
				const float AfterCook = M.EmitterWork(R).IntensityCd;
				S += FString::Printf(TEXT("[ред-тест] К1 A/B ВЕТКА C (кук/EditorDomain, IsProceduralSave): снимок %s, отметка * %s; возврат дал %.2f при «как в ассете» %.2f — %s\n"),
					bCookKept ? TEXT("не тронут") : TEXT("★ ВЫБРОШЕН"),
					bCookPending ? TEXT("на месте") : TEXT("★ СНЯТА — кук принят за запись ассета"),
					AfterCook, BaseNow,
					(bCookKept && bCookPending && FMath::IsNearlyEqual(AfterCook, BaseNow)) ? TEXT("верно") : TEXT("★ ПРОВАЛ"));

				// ═══ ★★★ РЕВИЗИЯ Ф13 (К2): A/B «ЧУЖАЯ ПОМЕТКА ИЗМЕНЁН» ═══════════════════════
				//   Чужая правка изображается НАСТОЯЩИМ MarkPackageDirty у CDO — тем же вызовом,
				//   которым её делает панель деталей. Событие идёт через движок до нашей подписки.
				//   Ветка A: пакет трогали только мы ⇒ возврат признак «изменён» снимает.
				//   Ветка B: между нашей правкой и возвратом пакет пометил кто-то ещё ⇒ признак
				//     обязан ОСТАТЬСЯ, иначе чужие правки исчезнут из «Save All» и вопроса при выходе.
				Pkg->SetDirtyFlag(false);
				Edit(BaseNow + 5.f);
				M.RevertEmitterToAsset(R);
				const bool bCleanA = !Pkg->IsDirty();
				const bool bForeignA = M.IsFxAssetForeignDirty(ClassPath);

				Pkg->SetDirtyFlag(false);
				// ★ Зафиксировано ИЗМЕРЕНИЕМ, а не рассуждением: пакет перед нашей правкой чист,
				//   значит у записи о тронутом ассете bWasDirtyBeforeTouch == false — и старый закон
				//   («снять, если до нас пакет не был изменён») снял бы признак безусловно
				const bool bCleanBeforeB = !Pkg->IsDirty();
				Edit(BaseNow + 6.f);
				Cdo->MarkPackageDirty();               // ← «правка владельца в панели деталей»
				const bool bForeignB = M.IsFxAssetForeignDirty(ClassPath);
				M.RevertEmitterToAsset(R);
				const bool bStillDirtyB = Pkg->IsDirty();
				const float AfterB = M.EmitterWork(R).IntensityCd;
				// ★★★ СТАРЫЙ ЗАКОН К2, ДОСЛОВНО: признак снимался по ОДНОМУ условию — «до нашей
				//   первой правки пакет не был изменён». Чужая правка ПОСЛЕ первого касания в него
				//   не входила вовсе, и здесь он дал бы «снять» — то есть потерю работы владельца
				const bool bOldWouldClear = bCleanBeforeB;   // ровно условие прежнего кода: !bWasDirtyBeforeTouch
				S += FString::Printf(TEXT("[ред-тест] К2 A/B СТАРЫЙ ЗАКОН в ветке B (условие «до первого касания пакет был чист» = %s): «%s»; новый закон — «оставить»\n"),
					bCleanBeforeB ? TEXT("да") : TEXT("нет"),
					bOldWouldClear ? TEXT("СНЯТЬ признак — чужие правки исчезают из «Save All» и из вопроса при выходе") : TEXT("оставить"));
				S += FString::Printf(TEXT("[ред-тест] К2 A/B ВЕТКА A (трогали только мы): чужая пометка %s, после возврата пакет %s — %s\n"),
					bForeignA ? TEXT("★ ЕСТЬ (не должно)") : TEXT("нет"),
					bCleanA ? TEXT("чист") : TEXT("★ ОСТАЛСЯ ИЗМЕНЁННЫМ"),
					(!bForeignA && bCleanA) ? TEXT("верно") : TEXT("★ РАСХОЖДЕНИЕ"));
				S += FString::Printf(TEXT("[ред-тест] К2 A/B ВЕТКА B (поверх нашей правки пакет пометил кто-то ещё): чужая пометка %s, после возврата пакет %s, числа эмиттера вернулись к %.2f (ожидалось %.2f) — %s\n"),
					bForeignB ? TEXT("замечена") : TEXT("★ НЕ ЗАМЕЧЕНА"),
					bStillDirtyB ? TEXT("ОСТАЛСЯ изменённым") : TEXT("★ ОЧИЩЕН — чужие правки исчезли из «Save All»"),
					AfterB, BaseNow,
					(bForeignB && bStillDirtyB && FMath::IsNearlyEqual(AfterB, BaseNow)) ? TEXT("верно: чужая работа не потеряна, а числа эмиттера вернулись") : TEXT("★ ПРОВАЛ"));

				// ═══ ★★★ ВОЛНА Ф14 (С-2, 20.09.2026): A/B «СВОЯ ПОМЕТКА ПРОТИВ ЧУЖОЙ» ═══════════
				//   ЧТО ДОКАЗЫВАЕТСЯ. Кнопка «Пометить все тронутые» ставит признак «изменён»
				//   САМА — и её собственная пометка не должна считаться правкой владельца помимо
				//   конфигуратора. Иначе bForeignDirty вставал навсегда: «Вернуть эмиттер как в
				//   ассете» переставала снимать признак, а владельцу показывалось ложное
				//   «этот ассет правили и помимо конфигуратора».
				//   ОБЕ ВЕТКИ — НАСТОЯЩИЕ ВЫЗОВЫ. Старый закон изображается тем самым
				//   MarkPackageDirty() без счётчика, что и стоял в кнопке; новый — самой кнопкой.
				//   Между ветками признак сбрасывается ТЕМ ЖЕ обработчиком записи, что и в жизни.
				{
					auto ResetForeign = [Pkg]()
					{
						FObjectSaveContextData D;
						D.TargetFilename = TEXT("<сброс признака между ветками A/B>");
						D.bSaveSucceeded = true;
						FAsdaFxGlowConfigModel::HandlePackageSaved(D.TargetFilename, Pkg, FObjectPostSaveContext(D));
						Pkg->SetDirtyFlag(false);
					};
					// ── СТАРЫЙ ЗАКОН: пометка мимо счётчика ──
					Edit(BaseNow + 8.f);           // запись о тронутом ассете обязана существовать
					ResetForeign();
					Cdo->MarkPackageDirty();       // ← ровно то, что делала кнопка
					const bool bOldForeign = M.IsFxAssetForeignDirty(ClassPath);
					// ── НОВЫЙ ЗАКОН: та же пометка, но самой кнопкой ──
					ResetForeign();
					FString MarkMsg;
					const int32 MarkedNow = M.MarkAllTouchedFxDirty(MarkMsg);
					const bool bNewForeign = M.IsFxAssetForeignDirty(ClassPath);
					S += FString::Printf(TEXT("[ред-тест] С-2 A/B СТАРЫЙ ЗАКОН (MarkPackageDirty вне счётчика): своя пометка принята за чужую: %s — %s\n"),
						bOldForeign ? TEXT("ДА") : TEXT("нет"),
						bOldForeign ? TEXT("дефект воспроизведён") : TEXT("★ ПРОВАЛ: дефект не воспроизводится, A/B бессмысленно"));
					S += FString::Printf(TEXT("[ред-тест] С-2 A/B НОВЫЙ ЗАКОН (кнопка «Пометить все тронутые», помечено %d): признак «правили помимо конфигуратора» %s — %s\n"),
						MarkedNow, bNewForeign ? TEXT("★ ВЗВЕДЁН") : TEXT("не взведён"),
						(!bNewForeign && bOldForeign) ? TEXT("верно: своя пометка чужой больше не считается") : TEXT("★ ПРОВАЛ"));
					// вернуть числа эмиттера к тому, что тест считает записанным на диск
					ResetForeign();
					M.RevertEmitterToAsset(R);
				}

				// ── вернуть ассет ровно в то состояние, в каком он был до теста ──
				if (Cdo->Emitters.Num() == Glow0.Num())
				{
					for (int32 i = 0; i < Glow0.Num(); ++i) { Cdo->Emitters[i].Glow = Glow0[i]; }
				}
				Cdo->EmitterGlowSaved = Saved0;
				bool bBack = Cdo->Emitters.Num() == Glow0.Num() && Cdo->EmitterGlowSaved.Num() == Saved0.Num();
				UScriptStruct* GS2 = FAsdaFxEmitterGlow::StaticStruct();
				for (int32 i = 0; i < Glow0.Num() && bBack; ++i) { bBack = GS2->CompareScriptStruct(&Glow0[i], &Cdo->Emitters[i].Glow, PPF_None); }
				bStateRestored = bBack;
				// ★ И СКЛАД СНИМКОВ ТОЖЕ ВЕРНУТЬ В СОГЛАСИЕ С АССЕТОМ. Он живёт на весь модуль, а не
				//   на эту функцию: оставить в нём снимок от середины A/B значило бы, что в живом
				//   редакторе владельца у эмиттера навсегда повиснет ложная отметка * и ложное
				//   «как в ассете». Пересъём делаем ТЕМ ЖЕ обработчиком записи, потом отпускаем ассет
				if (bBack)
				{
					FObjectSaveContextData DFin;
					DFin.TargetFilename = TEXT("<восстановление после теста>");
					DFin.bSaveSucceeded = true;
					FAsdaFxGlowConfigModel::HandlePackageSaved(DFin.TargetFilename, Pkg, FObjectPostSaveContext(DFin));
					M.ReleaseFxAssetIfUnchanged(ClassPath);
				}
				S += FString::Printf(TEXT("[ред-тест] Ф13: состояние ассета после A/B восстановлено: %s; записей о тронутых ассетах осталось %d\n"),
					bBack ? TEXT("да") : TEXT("★ НЕТ — признак «изменён» будет оставлен намеренно"), M.NumFxAssetsEdited());
			}
		}
		// ★★★ С4: ПРИЗНАК «ИЗМЕНЁН» СНИМАЕМ, ТОЛЬКО ЕСЛИ СОСТОЯНИЕ И ВПРАВДУ ВЕРНУЛОСЬ.
		//   Иначе тест прятал бы по-настоящему изменённый CDO от «Save All» и от вопроса при выходе —
		//   в редакторе владельца это прямая потеря его работы.
		if (Pkg && !bWasDirty)
		{
			if (bStateRestored)
			{
				Pkg->SetDirtyFlag(false);
			}
			else
			{
				S += FString::Printf(TEXT("[ред-тест] ★ С4: состояние НЕ вернулось к исходному — признак «изменён» у %s ОСТАВЛЕН НАМЕРЕННО. Проверьте ассет и при необходимости сохраните или перезагрузите его\n"),
					*Pkg->GetName());
			}
		}
		else if (Pkg && bWasDirty)
		{
			S += FString::Printf(TEXT("[ред-тест] С4: пакет %s был изменён ЕЩЁ ДО теста — признак не трогаем\n"), *Pkg->GetName());
		}
	}
	S += FString::Printf(TEXT("[ред-тест] АССЕТОВ ТРОНУТО: %d, НЕ ЗАПИСАНО: %d (ожидается «тронуто ≥ 0, не записано 0» — тест возвращает всё как было)\n"),
		M.NumFxAssetsEdited(), M.NumFxAssetsUnsaved());
	UE_LOG(LogAsdaItemBrowserTest, Display, TEXT("%s"), *S);
	return S;
}


// ════════════════════════════════════════════════════════════════════════════
// ★★★ 29.10.2026 — самопроверка переделанного просмотрщика (разбивка, карточка, кадры)
// ════════════════════════════════════════════════════════════════════════════

FString UAsdaItemBrowserDebug::DumpBreakdown()
{
	TSharedRef<FAsdaItemBrowserData> D = FAsdaItemBrowserData::Get();
	if (!D->EnsureLoaded()) { return FString::Printf(TEXT("[просмотрщик-тест] данные не загружены: %s"), *D->GetLoadError()); }

	FString S = FString::Printf(TEXT("[просмотрщик-тест] РАЗБИВКА: записей %d (item_list %d, sowell_list %d, special_itemlist %d), профессий в jobtype %d\n"),
		D->Num(), D->NumInSpace(EAsdaItemSpace::ItemList), D->NumInSpace(EAsdaItemSpace::SowelList), D->NumInSpace(EAsdaItemSpace::SpecialList), D->NumJobs());

	// Категории → подкатегории (тем же деревом, что панель: группировка по типу, без фильтров).
	FAsdaBrowserFilter F;
	TArray<TSharedPtr<FAsdaBrowserNode>> Roots;
	int32 Shown = 0;
	D->BuildTreeEx(F, Roots, Shown);
	S += FString::Printf(TEXT("== по типу: прошло %d ==\n"), Shown);
	TFunction<void(const TSharedPtr<FAsdaBrowserNode>&, int32)> Walk = [&](const TSharedPtr<FAsdaBrowserNode>& N, int32 Depth)
	{
		if (!N.IsValid() || N->Kind == EAsdaBrowserNodeKind::Item) { return; }
		S += FString::ChrN(Depth * 2, TEXT(' ')) + N->Label + TEXT("\n");
		for (const TSharedPtr<FAsdaBrowserNode>& C : N->Children) { Walk(C, Depth + 1); }
	};
	for (const TSharedPtr<FAsdaBrowserNode>& R : Roots) { Walk(R, 1); }

	// Остальные группировки — только верхний уровень (и второй у класса).
	const TCHAR* const GName[] = { TEXT("по типу"), TEXT("по классу"), TEXT("по уровню"), TEXT("по грейду") };
	for (int32 G = 1; G < (int32)EAsdaBrowserGrouping::Count; ++G)
	{
		F.Grouping = (EAsdaBrowserGrouping)G;
		D->BuildTreeEx(F, Roots, Shown);
		S += FString::Printf(TEXT("== %s: прошло %d ==\n"), GName[G], Shown);
		for (const TSharedPtr<FAsdaBrowserNode>& R : Roots)
		{
			S += TEXT("  ") + R->Label + TEXT("\n");
			if (G == 1) { for (const TSharedPtr<FAsdaBrowserNode>& C : R->Children) { if (C->Kind != EAsdaBrowserNodeKind::Item) { S += TEXT("    ") + C->Label + TEXT("\n"); } } }
		}
	}

	// Фильтры — комбинации (контроль «И»).
	struct FCase { const TCHAR* What; FAsdaBrowserFilter F; };
	TArray<FCase> Cases;
	{ FCase C{ TEXT("оружие, класс «Лучник» (12) может надеть, уровень 30…40") }; C.F.Cat = (int32)EAsdaItemCat::Weapon; C.F.Job = 12; C.F.JobMode = EAsdaJobFilterMode::Reachable; C.F.LvFrom = 30; C.F.LvTo = 40; Cases.Add(C); }
	{ FCase C{ TEXT("шлемы премиум грейд 2") }; C.F.Cat = (int32)EAsdaItemCat::Helmet; C.F.Premium = 1; C.F.Grade = 2; Cases.Add(C); }
	{ FCase C{ TEXT("ветка «Маг» (23…33), годится уровню 50") }; C.F.Job = 23; C.F.JobMode = EAsdaJobFilterMode::Branch; C.F.CharLevel = 50; Cases.Add(C); }
	{ FCase C{ TEXT("поиск «зелье»") }; C.F.Text = TEXT("зелье"); Cases.Add(C); }
	{ FCase C{ TEXT("поиск EN «potion»") }; C.F.Text = TEXT("potion"); Cases.Add(C); }
	{ FCase C{ TEXT("поиск кода «461»") }; C.F.Text = TEXT("461"); Cases.Add(C); }
	{ FCase C{ TEXT("коробки") }; C.F.Cat = (int32)EAsdaItemCat::Box; Cases.Add(C); }
	S += TEXT("== комбинации фильтров ==\n");
	for (FCase& C : Cases)
	{
		D->BuildTreeEx(C.F, Roots, Shown);
		S += FString::Printf(TEXT("  %s: прошло %d, корней %d%s\n"), C.What, Shown, Roots.Num(),
			(Roots.Num() > 0 && Roots[0]->Kind == EAsdaBrowserNodeKind::CodeHit) ? *FString::Printf(TEXT(" (первый — «%s»)"), *Roots[0]->Label) : TEXT(""));
	}
	UE_LOG(LogAsdaItemBrowserTest, Display, TEXT("%s"), *S);
	return S;
}

FString UAsdaItemBrowserDebug::DumpCard(int32 ItemCode, int32 CardKind, int32 ViewerJob, int32 ViewerLevel)
{
	AsdaItemTipStandalone::SetViewer(ViewerLevel, ViewerJob, 0);
	const EAsdaItemCardKind K = (CardKind == 1) ? EAsdaItemCardKind::Full : (CardKind == 2 ? EAsdaItemCardKind::Simple : EAsdaItemCardKind::Auto);
	FString S = FString::Printf(TEXT("[просмотрщик-тест] КАРТОЧКА %d (вид %d, зритель: профессия %d, уровень %d)\n"), ItemCode, CardKind, ViewerJob, ViewerLevel);
	for (int32 Lang = 0; Lang < 2; ++Lang)
	{
		FAsdaItemCard C;
		const bool bOk = AsdaItemTipStandalone::BuildCard(ItemCode, Lang == 0 ? EAsdaLang::Ru : EAsdaLang::En, K, C);
		S += FString::Printf(TEXT("--- %s: %s, построитель %s, ширина %.0f, подвал %d ---\n"), Lang == 0 ? TEXT("RU") : TEXT("EN"),
			bOk ? TEXT("собрана") : TEXT("НЕ собрана"), *C.Pipeline, C.M.FixedWidth, C.M.bPremiumFooter ? 1 : 0);
		S += FString::Printf(TEXT("[заголовок цвет %08X] %s\n%s\n"), C.Tip.TitleArgb, *FAsdaMarkup::StripTags(C.Tip.Title), *FAsdaMarkup::StripTags(C.Tip.Body));
		if (C.M.bPremiumFooter) { S += FString::Printf(TEXT("[подвал] %s\n"), *FAsdaMarkup::StripTags(C.M.FooterText)); }
	}
	UE_LOG(LogAsdaItemBrowserTest, Display, TEXT("%s"), *S);
	return S;
}

FString UAsdaItemBrowserDebug::RenderViewer(const FString& PngPath, int32 ItemCode, const FString& Search, int32 Space, int32 Cat,
	int32 Job, int32 JobMode, int32 LvFrom, int32 LvTo, int32 CharLevel, int32 Grade, int32 Premium, int32 Grouping,
	int32 CardKind, int32 ViewerJob, int32 ViewerLevel, int32 ExpandDepth, int32 Width, int32 Height)
{
	if (!FSlateApplication::IsInitialized()) { return TEXT("[просмотрщик-тест] Slate не поднят (нужен редактор с RHI: -ExecCmds + -RenderOffscreen)"); }
	TSharedRef<FAsdaItemBrowserData> D = FAsdaItemBrowserData::Get();
	if (!D->EnsureLoaded()) { return FString::Printf(TEXT("[просмотрщик-тест] данные не загружены: %s"), *D->GetLoadError()); }

	TSharedRef<SAsdaItemBrowser> W = SNew(SAsdaItemBrowser);
	W->SetFilterControls(Search, Space, Cat, Job, JobMode, LvFrom, LvTo, CharLevel, Grade, Premium, Grouping);
	W->SetCardControls(CardKind, ViewerJob, ViewerLevel);
	if (ItemCode > 0)
	{
		const int32 Idx = D->IndexByCode(ItemCode);
		// Выбор — как клик по строке дерева (ветки до неё раскрываются); вне дерева — карточка всё равно строится.
		if (Idx != INDEX_NONE) { W->SelectForTest(Idx); }
	}
	if (ExpandDepth >= 0) { W->ExpandAll(ExpandDepth); }

	const FVector2D Size((float)Width, (float)Height);
	// Цель — float RGBA16 в ЛИНЕЙНОМ пространстве: Slate без гамма-коррекции пишет линейный цвет, перевод в sRGB делаем
	//   сами при сохранении (8-битная цель давала то двойную гамму, то потерю тёмных тонов — проверено на кадрах).
	TStrongObjectPtr<UTextureRenderTarget2D> RT(NewObject<UTextureRenderTarget2D>());
	RT->RenderTargetFormat = RTF_RGBA16f;
	RT->ClearColor = FLinearColor::Black;
	RT->InitCustomFormat(Width, Height, PF_FloatRGBA, /*bInForceLinearGamma*/ true);
	RT->UpdateResourceImmediate(true);
	TSharedRef<FWidgetRenderer> R = MakeShared<FWidgetRenderer>(/*bUseGammaCorrection*/ false, /*bInClearTarget*/ true);
	// Несколько проходов: список строк дерева и карточка дорисовываются на тиках внутри Paint.
	for (int32 Pass = 0; Pass < 6; ++Pass)
	{
		R->DrawWidget(RT.Get(), W, Size, 0.05f);
		FlushRenderingCommands();
	}
	TArray<FLinearColor> Lin;
	if (!RT->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Lin) || Lin.Num() != Width * Height)
	{
		return TEXT("[просмотрщик-тест] не удалось прочитать render target");
	}
	TArray<FColor> Px;
	Px.SetNumUninitialized(Lin.Num());
	for (int32 i = 0; i < Lin.Num(); ++i)
	{
		FLinearColor C = Lin[i];
		C.A = 1.f;
		Px[i] = C.ToFColorSRGB();
	}
	FImageView View(Px.GetData(), Width, Height, EGammaSpace::sRGB);
	const bool bSaved = FImageUtils::SaveImageByExtension(*PngPath, View);
	const FString S = FString::Printf(TEXT("[просмотрщик-тест] КАДР %s: %s; прошло фильтр %d; выбран код %d"),
		*PngPath, bSaved ? TEXT("записан") : TEXT("НЕ записан"), W->GetShownCount(), ItemCode);
	UE_LOG(LogAsdaItemBrowserTest, Display, TEXT("%s"), *S);
	return S;
}
