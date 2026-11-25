// Copyright (c) 2026 @sitxovski (Discord)
// Самопроверка обозревателя — см. AsdaMobBrowserDebug.h.
#include "AsdaMobBrowserDebug.h"

#include "Framework/Application/SlateApplication.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/DataTable.h"
#include "Animation/AnimSequence.h"
#include "TextureResource.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "Misc/PackageName.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"
#include "AssetCompilingManager.h"
#include "ContentStreaming.h"

#include "AsdaDataTableCache.h"
#include "World/AsdaEntityText.h"
#include "Resources/AsdaAssetPaths.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "AsdaMobBrowserData.h"
#include "SAsdaMobBrowser.h"
#include "SAsdaMobPreviewViewport.h"
#include "AsdaMobPreviewClient.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsdaMobBrowserDebug)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMobBrowserTest, Log, All);

namespace AsdaMobBrowserClipBaseDbg
{
#include "AsdaMobClipBase.inl"   // та же автоген-таблица, что у игры: образец «моб без своих клипов»
}

namespace
{
	bool PkgExists(const FString& Pkg) { return !Pkg.IsEmpty() && FPackageName::DoesPackageExist(Pkg); }
	bool SkelExists(const TCHAR* Root, const FString& Name)
	{
		return !Name.IsEmpty() && PkgExists(UAsdaModelAnimSubsystem::BuildSkelPath(Root, Name));
	}
	bool StaticExists(const FString& Name)
	{
		return !Name.IsEmpty() && PkgExists(FPackageName::ObjectPathToPackageName(AsdaAssetPaths::MobStaticMeshObject(Name)));
	}

	struct FSamples { int32 Small = 0, Big = 0, Boss = 0, NoOwnClips = 0, Static = 0, Npc = 0; };

	FSamples Pick()
	{
		FSamples S;
		TSharedRef<FAsdaMobBrowserData> D = FAsdaMobBrowserData::Get();
		D->Reload();
		float MinH = TNumericLimits<float>::Max(), MaxH = -1.f;
		int32 BossHp = -1;
		TMap<int32, FString> Variant;
		for (const auto& R : AsdaMobBrowserClipBaseDbg::kAsdaMobClipBase) { Variant.Add(R.MobId, R.Variant); }
		for (const TSharedPtr<FAsdaMobEntry>& E : D->GetMobs())
		{
			if (!E->Client.bValid || E->Client.MaxHp <= 0 || E->NameRu.IsEmpty()) { continue; }   // типы с клиентской строкой
			const FString* V = Variant.Find(E->Id);
			const bool bSkel = !V && SkelExists(AsdaAssetPaths::kSkelRootMobSkel, E->Model);
			if (bSkel && E->Client.Rank != 3 && E->Client.HalfY > 0.f)
			{
				if (E->Client.HalfY < MinH) { MinH = E->Client.HalfY; S.Small = E->Id; }
				if (E->Client.HalfY > MaxH) { MaxH = E->Client.HalfY; S.Big = E->Id; }
			}
			if (bSkel && E->Client.Rank == 3 && E->Client.MaxHp > BossHp) { BossHp = E->Client.MaxHp; S.Boss = E->Id; }
			// Меш без своих клипов — клипы берёт скелетный вариант «<меш>__<база>».
			if (V && S.NoOwnClips == 0 && !SkelExists(AsdaAssetPaths::kSkelRootMobSkel, E->Model) && SkelExists(AsdaAssetPaths::kSkelRootMobSkel, *V)) { S.NoOwnClips = E->Id; }
			if (!V && S.Static == 0 && !SkelExists(AsdaAssetPaths::kSkelRootMobSkel, E->Model) && StaticExists(E->Model)) { S.Static = E->Id; }
		}
		// NPC с клипом разговора (anim_02).
		for (const TSharedPtr<FAsdaMobEntry>& E : D->GetNpcs())
		{
			if (E->Model.IsEmpty() || E->NameRu.IsEmpty()) { continue; }
			if (PkgExists(UAsdaModelAnimSubsystem::BuildSkelAnimPath(AsdaAssetPaths::kSkelRootNpc, E->Model, 2))) { S.Npc = E->Id; break; }
		}
		return S;
	}

	// Имитация мыши через вьюпорт-клиент (тот же вход, что от Slate).
	void SimKey(FAsdaMobPreviewClient& C, const FKey& Key, EInputEvent Ev)
	{
		C.InputKey(FInputKeyEventArgs::CreateSimulated(Key, Ev, 1.f, -1, INPUTDEVICEID_NONE, false, C.Viewport));
	}
	void SimAxis(FAsdaMobPreviewClient& C, const FKey& Key, float Delta)
	{
		C.InputAxis(FInputKeyEventArgs::CreateSimulated(Key, IE_Axis, Delta, -1, INPUTDEVICEID_NONE, false, C.Viewport));
	}

	FName ColOf(const FString& S)
	{
		if (S.Equals(TEXT("Lvl"), ESearchCase::IgnoreCase) || S.Equals(TEXT("Level"), ESearchCase::IgnoreCase)) { return SAsdaMobBrowser::ColLevel; }
		if (S.Equals(TEXT("ID"), ESearchCase::IgnoreCase)) { return SAsdaMobBrowser::ColId; }
		return SAsdaMobBrowser::ColName;
	}

	// Временная строка таблицы — ТОЛЬКО в памяти: добавить, проверить, убрать; флаг «изменён» пакета возвращается как был.
	struct FTempRow
	{
		UDataTable* T = nullptr;
		FName Key;
		bool bWasDirty = false;
		bool Add(UDataTable* InT, FName InKey, const FString& Name, const FString& Model)
		{
			T = InT; Key = InKey;
			if (!T || !T->GetRowStruct() || T->FindRowUnchecked(Key)) { T = nullptr; return false; }
			bWasDirty = T->GetOutermost()->IsDirty();
			const UScriptStruct* RS = T->GetRowStruct();
			uint8* Mem = (uint8*)FMemory::Malloc(RS->GetStructureSize(), RS->GetMinAlignment());
			RS->InitializeStruct(Mem);
			if (FStrProperty* P = FindFProperty<FStrProperty>(RS, TEXT("Name"))) { P->SetPropertyValue_InContainer(Mem, Name); }
			if (FStrProperty* P = FindFProperty<FStrProperty>(RS, TEXT("Model"))) { P->SetPropertyValue_InContainer(Mem, Model); }
			T->AddRow(Key, *reinterpret_cast<FTableRowBase*>(Mem));
			RS->DestroyStruct(Mem);
			FMemory::Free(Mem);
			return T->FindRowUnchecked(Key) != nullptr;
		}
		~FTempRow()
		{
			if (!T) { return; }
			T->RemoveRow(Key);
			if (!bWasDirty) { T->GetOutermost()->SetDirtyFlag(false); }   // ассет на диске не меняется и не помечается
		}
	};
}

FString UAsdaMobBrowserDebug::PickSamples()
{
	const FSamples S = Pick();
	return FString::Printf(TEXT("small=%d;big=%d;boss=%d;noownclips=%d;static=%d;npc=%d"), S.Small, S.Big, S.Boss, S.NoOwnClips, S.Static, S.Npc);
}

FString UAsdaMobBrowserDebug::SelfTest()
{
	FString Out = TEXT("[обозреватель] САМОПРОВЕРКА\n");
	int32 Checks = 0, Errors = 0;
	const auto Check = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (!bOk) { ++Errors; }
		Out += FString::Printf(TEXT("  [%s] %s\n"), bOk ? TEXT("OK") : TEXT("ОШИБКА"), *What);
	};
	if (!FSlateApplication::IsInitialized()) { return TEXT("[обозреватель] Slate не поднят"); }

	TSharedRef<FAsdaMobBrowserData> D = FAsdaMobBrowserData::Get();
	Check(D->Reload() && D->GetLoadError().IsEmpty(), FString::Printf(TEXT("таблицы игры прочитаны (%s)"), *D->GetLoadError()));
	int32 NClient = 0, NEn = 0, NNpcRec = 0, NNpcEn = 0;
	for (const TSharedPtr<FAsdaMobEntry>& E : D->GetMobs())
	{
		NClient += E->Client.bValid; NEn += !E->NameEn.IsEmpty();
	}
	for (const TSharedPtr<FAsdaMobEntry>& E : D->GetNpcs()) { NNpcRec += E->Npc.bValid; NNpcEn += !E->NameEn.IsEmpty(); }
	const UDataTable* MobNames = AsdaDt::Cached(AsdaEntityText::kMobNamesPath);
	const UDataTable* NpcNames = AsdaDt::Cached(AsdaEntityText::kNpcNamesPath);
	Out += FString::Printf(TEXT("  мобов %d (строк DT_MobNames %d): со строкой DT_MobStats %d, EN-имён %d\n"),
		D->GetMobs().Num(), MobNames ? MobNames->GetRowMap().Num() : -1, NClient, NEn);
	Out += FString::Printf(TEXT("  NPC %d (строк DT_NpcNames %d): DT_NpcQuestNpc %d, EN %d\n"), D->GetNpcs().Num(), NpcNames ? NpcNames->GetRowMap().Num() : -1, NNpcRec, NNpcEn);
	Check(MobNames && D->GetMobs().Num() == MobNames->GetRowMap().Num(), TEXT("список мобов = строки DT_MobNames"));
	Check(D->GetNpcs().Num() > 0, TEXT("список NPC не пуст"));

	TSharedRef<SAsdaMobBrowser> W = SNew(SAsdaMobBrowser);
	W->SetEnglish(false);
	// ── группы ──
	Check(W->IsGroupExpanded(EAsdaBrowserKind::Mob) && !W->IsGroupExpanded(EAsdaBrowserKind::Npc), TEXT("при открытии раскрыта группа Mobs"));
	W->ExpandGroup(EAsdaBrowserKind::Npc);
	Check(W->IsGroupExpanded(EAsdaBrowserKind::Npc) && !W->IsGroupExpanded(EAsdaBrowserKind::Mob), TEXT("раскрыл NPC — Mobs свернулась"));
	Check(W->GetSelected().IsValid() && W->GetSelected()->Kind == EAsdaBrowserKind::Npc, TEXT("раскрыл NPC — выбран NPC"));
	W->ExpandGroup(EAsdaBrowserKind::Mob);
	Check(W->IsGroupExpanded(EAsdaBrowserKind::Mob) && W->GetSelected()->Kind == EAsdaBrowserKind::Mob, TEXT("раскрыл Mobs — выбран моб"));

	const auto Ordered = [&](EAsdaBrowserKind K, FName Col, bool bDesc, bool bEn) -> bool
	{
		const TArray<int32> Ids = W->GetShownIds(K);
		for (int32 i = 1; i < Ids.Num(); ++i)
		{
			const TSharedPtr<FAsdaMobEntry> A = D->Find(K, Ids[i - 1]), B = D->Find(K, Ids[i]);
			int32 C = 0;
			if (Col == SAsdaMobBrowser::ColLevel) { C = A->DisplayLevel() - B->DisplayLevel(); }
			else if (Col == SAsdaMobBrowser::ColId) { C = A->Id - B->Id; }
			else
			{
				// Независимая сверка: нижний регистр по ICU, затем обычное сравнение строк (порядок без учёта регистра у кириллицы).
				C = FAsdaMobBrowserData::SearchKey(A->DisplayName(bEn)).Compare(FAsdaMobBrowserData::SearchKey(B->DisplayName(bEn)));
				if (C != 0 && FAsdaMobBrowserData::CompareNames(A->DisplayName(bEn), B->DisplayName(bEn)) == 0) { C = 0; }
				if (C > 0 && FAsdaMobBrowserData::CompareNames(A->DisplayName(bEn), B->DisplayName(bEn)) < 0) { C = 0; }   // «ё»/знаки: коллация языка
			}
			if (bDesc ? C < 0 : C > 0) { return false; }
		}
		return Ids.Num() == D->GetList(K).Num();
	};
	for (EAsdaBrowserKind K : { EAsdaBrowserKind::Mob, EAsdaBrowserKind::Npc })
	{
		const TCHAR* G = K == EAsdaBrowserKind::Mob ? TEXT("Mobs") : TEXT("NPC");
		Check(Ordered(K, SAsdaMobBrowser::ColName, false, false), FString::Printf(TEXT("%s: по умолчанию по алфавиту, все записи"), G));
		W->SetSort(K, SAsdaMobBrowser::ColLevel, EColumnSortMode::Ascending);  Check(Ordered(K, SAsdaMobBrowser::ColLevel, false, false), FString::Printf(TEXT("%s: Lvl по возрастанию"), G));
		W->SetSort(K, SAsdaMobBrowser::ColLevel, EColumnSortMode::Descending); Check(Ordered(K, SAsdaMobBrowser::ColLevel, true, false), FString::Printf(TEXT("%s: Lvl по убыванию"), G));
		W->SetSort(K, SAsdaMobBrowser::ColName, EColumnSortMode::Descending);  Check(Ordered(K, SAsdaMobBrowser::ColName, true, false), FString::Printf(TEXT("%s: имя по убыванию"), G));
		W->SetSort(K, SAsdaMobBrowser::ColId, EColumnSortMode::Ascending);     Check(Ordered(K, SAsdaMobBrowser::ColId, false, false), FString::Printf(TEXT("%s: ID по возрастанию"), G));
		W->SetSort(K, SAsdaMobBrowser::ColName, EColumnSortMode::Ascending);
	}
	W->SetSearchText(TEXT("290"));
	Check(W->GetShownIds(EAsdaBrowserKind::Mob).Contains(290), FString::Printf(TEXT("поиск по ID «290»: мобов %d"), W->GetShownIds(EAsdaBrowserKind::Mob).Num()));
	W->SetSearchText(TEXT("волк"));
	Check(W->GetShownIds(EAsdaBrowserKind::Mob).Num() > 0, FString::Printf(TEXT("поиск «волк»: мобов %d"), W->GetShownIds(EAsdaBrowserKind::Mob).Num()));
	W->SetSearchText(TEXT("Волк"));
	Check(W->GetShownIds(EAsdaBrowserKind::Mob).Num() > 0, FString::Printf(TEXT("поиск «Волк» (заглавная) находит «…волк…»: мобов %d"), W->GetShownIds(EAsdaBrowserKind::Mob).Num()));
	W->SetSearchText(FString());

	// ── модели и клипы ──
	const FSamples S = Pick();
	Out += FString::Printf(TEXT("  образцы: маленький %d, большой %d, босс %d, без своих клипов %d, статический %d, NPC %d\n"),
		S.Small, S.Big, S.Boss, S.NoOwnClips, S.Static, S.Npc);
	const auto CheckClips = [&](EAsdaBrowserKind K, int32 Id)
	{
		if (Id <= 0) { Check(false, TEXT("образец не найден")); return; }
		Check(W->SelectEntry(K, Id) && W->GetSelected()->Id == Id && W->IsGroupExpanded(K), FString::Printf(TEXT("выбор %d раскрывает его группу"), Id));
		const FAsdaMobModelAssets& A = W->GetCurrentAssets();
		TSharedPtr<SAsdaMobPreviewViewport> V = W->GetViewport();
		Check(A.Skel != nullptr && V->HasSkeletal(), FString::Printf(TEXT("  %d: скелетная модель %s%s, клипов %d, текстур-оверрайдов %d"), Id, *A.SkelModel,
			A.bClipBaseVariant ? TEXT(" (вариант клипов базы)") : TEXT(""), A.Clips.Num(), A.MaterialOverrides.Num()));
		UAnimSequence* Cur = V->GetCurrentClip();
		Check(Cur && (Cur->GetName().EndsWith(TEXT("anim_00")) || Cur->GetName().EndsWith(TEXT("_Anim"))),
			FString::Printf(TEXT("  %d: по умолчанию играет покой (%s)"), Id, Cur ? *Cur->GetName() : TEXT("ничего")));
		int32 Played = 0;
		for (const FAsdaMobClip& C : A.Clips)
		{
			Played += W->SelectClipIndex(C.Index) && V->GetCurrentClip() && V->GetCurrentClip()->GetName().EndsWith(FString::Printf(TEXT("anim_%02d"), C.Index));
		}
		Check(Played == A.Clips.Num(), FString::Printf(TEXT("  %d: выбор каждого клипа ставит его (%d из %d)"), Id, Played, A.Clips.Num()));
		W->SelectClipIndex(0);
	};
	CheckClips(EAsdaBrowserKind::Mob, S.Small);
	CheckClips(EAsdaBrowserKind::Mob, S.Big);
	CheckClips(EAsdaBrowserKind::Mob, S.Boss);
	CheckClips(EAsdaBrowserKind::Mob, S.NoOwnClips);
	CheckClips(EAsdaBrowserKind::Npc, S.Npc);
	if (S.Static > 0)
	{
		W->SelectEntry(EAsdaBrowserKind::Mob, S.Static);
		Check(W->GetViewport()->HasStatic() && W->GetCurrentAssets().Clips.Num() == 0, FString::Printf(TEXT("статический моб %d: модель-фолбэк, клипов нет"), S.Static));
	}

	// ── мышь ──
	W->SelectEntry(EAsdaBrowserKind::Mob, S.Boss > 0 ? S.Boss : S.Small);
	TSharedPtr<SAsdaMobPreviewViewport> V = W->GetViewport();
	TSharedPtr<FAsdaMobPreviewClient> C = V->GetPreviewClient();
	const float Yaw0 = V->GetModelYaw();
	SimAxis(*C, EKeys::MouseX, 60.f);
	Check(FMath::IsNearlyEqual(V->GetModelYaw(), Yaw0), TEXT("движение мыши без кнопки модель не крутит"));
	SimKey(*C, EKeys::LeftMouseButton, IE_Pressed);
	SimAxis(*C, EKeys::MouseX, 60.f);
	SimKey(*C, EKeys::LeftMouseButton, IE_Released);
	Check(FMath::IsNearlyEqual(V->GetModelYaw(), Yaw0 + 60.f * FAsdaMobPreviewClient::kDegPerPixel, 0.01f),
		FString::Printf(TEXT("ЛКМ + 60 пикс. по X → поворот %.1f°"), V->GetModelYaw() - Yaw0));
	const float Z0 = V->GetZoom();
	SimKey(*C, EKeys::MouseScrollUp, IE_Pressed);
	Check(V->GetZoom() < Z0, FString::Printf(TEXT("колесо вверх приближает (%.2f → %.2f)"), Z0, V->GetZoom()));
	const float P0 = V->GetCameraPitch();
	SimKey(*C, EKeys::RightMouseButton, IE_Pressed);
	SimAxis(*C, EKeys::MouseY, -20.f);
	SimKey(*C, EKeys::RightMouseButton, IE_Released);
	Check(!FMath::IsNearlyEqual(V->GetCameraPitch(), P0), FString::Printf(TEXT("ПКМ + мышь по Y наклоняет камеру (%.1f → %.1f)"), P0, V->GetCameraPitch()));
	V->ResetView();
	Check(FMath::IsNearlyZero(V->GetModelYaw()) && FMath::IsNearlyEqual(V->GetZoom(), 1.f), TEXT("«Reset view» возвращает вид"));

	// ── язык ──
	W->SetEnglish(true);
	Check(Ordered(EAsdaBrowserKind::Mob, SAsdaMobBrowser::ColName, false, true), TEXT("EN: мобы по алфавиту английских имён"));
	W->SetEnglish(false);

	// ── «-» и панель ──
	W->SelectEntry(EAsdaBrowserKind::Mob, 1);
	const FString MobStats = W->DescribeStatsForTest();
	Check(MobStats.Contains(TEXT("Level: ")) && MobStats.Contains(TEXT("Rank: ")) && !MobStats.Contains(TEXT("Attack:")) && !MobStats.Contains(TEXT("Defense:")), TEXT("моб 1: клиентские поля на панели, серверных нет"));
	Out += TEXT("  --- панель моба 1 ---\n") + MobStats + TEXT("\n");
	if (S.Npc > 0)
	{
		W->SelectEntry(EAsdaBrowserKind::Npc, S.Npc);
		const FString NpcStats = W->DescribeStatsForTest();
		Check(NpcStats.Contains(TEXT("Level: -")) && NpcStats.Contains(TEXT("HP: -")), TEXT("NPC: уровень и HP — «-» (в таблицах NPC их нет)"));
		Out += FString::Printf(TEXT("  --- панель NPC %d ---\n"), S.Npc) + NpcStats + TEXT("\n");
	}

	// ── «живость»: строка, добавленная в DataTable в памяти, видна в новом окне; после удаления — пропадает ──
	{
		UDataTable* MT = AsdaDt::Cached(AsdaEntityText::kMobNamesPath);
		UDataTable* NT = AsdaDt::Cached(AsdaEntityText::kNpcNamesPath);
		const int32 TestMobRow = 8999, TestNpcId = 8999;   // вне диапазона игры (мобов 854, NPC ~200)
		{
			FTempRow RM, RN;
			const bool bAddM = RM.Add(MT, FName(*FString::FromInt(TestMobRow)), TEXT("ТЕСТ живости моб"), TEXT("-"));
			const bool bAddN = RN.Add(NT, FName(*FString::FromInt(TestNpcId)), TEXT("ТЕСТ живости NPC"), TEXT("-"));
			TSharedRef<SAsdaMobBrowser> W2 = SNew(SAsdaMobBrowser);   // «переоткрыл окно»
			Check(bAddM && W2->GetShownIds(EAsdaBrowserKind::Mob).Contains(TestMobRow + 1), TEXT("живость: строка, добавленная в DT_MobNames, появилась в новом окне"));
			Check(bAddN && W2->GetShownIds(EAsdaBrowserKind::Npc).Contains(TestNpcId), TEXT("живость: строка, добавленная в DT_NpcNames, появилась в новом окне"));
			W2->SelectEntry(EAsdaBrowserKind::Mob, TestMobRow + 1);
			Check(W2->DescribeStatsForTest().Contains(TEXT("HP: -")), TEXT("живость: у новой записи без данных — «-»"));
		}
		TSharedRef<SAsdaMobBrowser> W3 = SNew(SAsdaMobBrowser);
		Check(!W3->GetShownIds(EAsdaBrowserKind::Mob).Contains(TestMobRow + 1) && !W3->GetShownIds(EAsdaBrowserKind::Npc).Contains(TestNpcId),
			TEXT("живость: строки убраны, ассеты не помечены изменёнными"));
		Check(!MT->GetOutermost()->IsDirty() && !NT->GetOutermost()->IsDirty(), TEXT("пакеты DT_MobNames / DT_NpcNames не «изменены»"));
	}

	Out += FString::Printf(TEXT("[обозреватель] ПРОВЕРОК %d, ОШИБОК %d\n"), Checks, Errors);
	UE_LOG(LogAsdaMobBrowserTest, Display, TEXT("%s"), *Out);
	return Out;
}

FString UAsdaMobBrowserDebug::RenderBrowser(const FString& PngPath, bool bNpc, int32 Id, int32 ClipIndex, float DragPixels, float ClipTime,
	const FString& SortColumn, bool bDescending, const FString& Search, bool bEnglish, int32 Width, int32 Height)
{
	if (!FSlateApplication::IsInitialized()) { return TEXT("[обозреватель] Slate не поднят (нужен редактор с RHI: -ExecCmds + -RenderOffscreen)"); }
	const EAsdaBrowserKind K = bNpc ? EAsdaBrowserKind::Npc : EAsdaBrowserKind::Mob;
	TSharedRef<SAsdaMobBrowser> W = SNew(SAsdaMobBrowser);
	W->SetEnglish(bEnglish);
	W->SetSort(K, ColOf(SortColumn), bDescending ? EColumnSortMode::Descending : EColumnSortMode::Ascending);
	if (!Search.IsEmpty()) { W->SetSearchText(Search); }
	W->ExpandGroup(K);
	if (Id > 0) { W->SelectEntry(K, Id); }
	if (ClipIndex >= 0) { W->SelectClipIndex(ClipIndex); }
	TSharedPtr<SAsdaMobPreviewViewport> V = W->GetViewport();
	TSharedPtr<FAsdaMobPreviewClient> C = V.IsValid() ? V->GetPreviewClient() : nullptr;
	if (C.IsValid() && !FMath::IsNearlyZero(DragPixels))
	{
		SimKey(*C, EKeys::LeftMouseButton, IE_Pressed);
		SimAxis(*C, EKeys::MouseX, DragPixels);
		SimKey(*C, EKeys::LeftMouseButton, IE_Released);
	}

	// Кадр — как увидит пользователь через секунду: шейдеры и текстуры досборки, мипы догружены.
	FAssetCompilingManager::Get().FinishAllCompilation();
	IStreamingManager::Get().StreamAllResources(5.f);
	const FVector2D Size((float)Width, (float)Height);
	TStrongObjectPtr<UTextureRenderTarget2D> RT(NewObject<UTextureRenderTarget2D>());
	RT->RenderTargetFormat = RTF_RGBA16f;
	RT->ClearColor = FLinearColor::Black;
	RT->InitCustomFormat(Width, Height, PF_FloatRGBA, /*bInForceLinearGamma*/ true);
	RT->UpdateResourceImmediate(true);
	TSharedRef<FWidgetRenderer> R = MakeShared<FWidgetRenderer>(/*bUseGammaCorrection*/ false, /*bInClearTarget*/ true);
	// Проходы: раскладка (вьюпорт получает размер) → тик сцены превью (поза клипа) → отрисовка 3D в цель вьюпорта → снова Slate.
	for (int32 Pass = 0; Pass < 6; ++Pass)
	{
		R->DrawWidget(RT.Get(), W, Size, 0.f);
		FlushRenderingCommands();
		if (C.IsValid())
		{
			C->Tick(Pass == 1 ? FMath::Max(ClipTime, 0.001f) : 0.001f);
			if (C->Viewport && C->Viewport->GetSizeXY().X > 0) { C->Viewport->Draw(/*bShouldPresent*/ false); }
			FlushRenderingCommands();
		}
	}
	R->DrawWidget(RT.Get(), W, Size, 0.f);
	FlushRenderingCommands();

	TArray<FLinearColor> Lin;
	if (!RT->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Lin) || Lin.Num() != Width * Height)
	{
		return TEXT("[обозреватель] не удалось прочитать render target");
	}
	TArray<FColor> Px;
	Px.SetNumUninitialized(Lin.Num());
	for (int32 i = 0; i < Lin.Num(); ++i)
	{
		FLinearColor L = Lin[i];
		L.A = 1.f;
		Px[i] = L.ToFColorSRGB();
	}
	FImageView View(Px.GetData(), Width, Height, EGammaSpace::sRGB);
	const bool bSaved = FImageUtils::SaveImageByExtension(*PngPath, View);
	UAnimSequence* Cur = V.IsValid() ? V->GetCurrentClip() : nullptr;
	const TSharedPtr<FAsdaMobEntry> Sel = W->GetSelected();
	const FString Res = FString::Printf(TEXT("[обозреватель] КАДР %s: %s; %s %d; клип %s; поворот %.1f°; вьюпорт %dx%d; строк группы %d"),
		*PngPath, bSaved ? TEXT("записан") : TEXT("НЕ записан"), bNpc ? TEXT("NPC") : TEXT("моб"), Sel.IsValid() ? Sel->Id : 0,
		Cur ? *Cur->GetName() : TEXT("-"), V.IsValid() ? V->GetModelYaw() : 0.f,
		(C.IsValid() && C->Viewport) ? C->Viewport->GetSizeXY().X : 0, (C.IsValid() && C->Viewport) ? C->Viewport->GetSizeXY().Y : 0,
		W->GetShownIds(K).Num());
	UE_LOG(LogAsdaMobBrowserTest, Display, TEXT("%s"), *Res);
	return Res;
}

FString UAsdaMobBrowserDebug::RenderDiag(const FString& PngPath, bool bNpc, int32 Id, int32 ViewMode, bool bNoMirror, float Yaw, int32 Width, int32 Height)
{
	if (!FSlateApplication::IsInitialized()) { return TEXT("[обозреватель] Slate не поднят"); }
	const EAsdaBrowserKind K = bNpc ? EAsdaBrowserKind::Npc : EAsdaBrowserKind::Mob;
	TSharedRef<SAsdaMobBrowser> W = SNew(SAsdaMobBrowser);
	W->SelectEntry(K, Id);
	TSharedPtr<SAsdaMobPreviewViewport> V = W->GetViewport();
	TSharedPtr<FAsdaMobPreviewClient> C = V->GetPreviewClient();
	V->SetDebugNoMirror(bNoMirror);
	V->SetModelYaw(Yaw);
	V->SetDebugViewMode(ViewMode);
	FAssetCompilingManager::Get().FinishAllCompilation();
	IStreamingManager::Get().StreamAllResources(5.f);
	TStrongObjectPtr<UTextureRenderTarget2D> RT(NewObject<UTextureRenderTarget2D>());
	RT->RenderTargetFormat = RTF_RGBA16f;
	RT->InitCustomFormat(Width, Height, PF_FloatRGBA, true);
	RT->UpdateResourceImmediate(true);
	TSharedRef<FWidgetRenderer> R = MakeShared<FWidgetRenderer>(false, true);
	const FVector2D Size((float)Width, (float)Height);
	for (int32 Pass = 0; Pass < 6; ++Pass)
	{
		R->DrawWidget(RT.Get(), W, Size, 0.f);
		FlushRenderingCommands();
		C->Tick(0.001f);
		if (C->Viewport && C->Viewport->GetSizeXY().X > 0) { C->Viewport->Draw(false); }
		FlushRenderingCommands();
	}
	R->DrawWidget(RT.Get(), W, Size, 0.f);
	FlushRenderingCommands();
	TArray<FLinearColor> Lin;
	if (!RT->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Lin)) { return TEXT("нет пикселей"); }
	TArray<FColor> Px;
	Px.SetNumUninitialized(Lin.Num());
	for (int32 i = 0; i < Lin.Num(); ++i) { FLinearColor L = Lin[i]; L.A = 1.f; Px[i] = L.ToFColorSRGB(); }
	FImageView View(Px.GetData(), Width, Height, EGammaSpace::sRGB);
	const bool bOk = FImageUtils::SaveImageByExtension(*PngPath, View);
	return FString::Printf(TEXT("[обозреватель] ДИАГ %s: %s; режим %d, без зеркала %d"), *PngPath, bOk ? TEXT("записан") : TEXT("НЕ записан"), ViewMode, bNoMirror ? 1 : 0);
}
