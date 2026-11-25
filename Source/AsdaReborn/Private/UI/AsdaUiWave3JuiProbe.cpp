// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОПРОВЕРКА ВОЛНЫ 3 АУДИТА ДУБЛЕЙ (15.09.2026, каркасы окон JUI) — ПУТЁМ ИГРОКА (`-game`,
//   закон `ui-check-game-path-geometry`). Только диагностика: пакеты не шлёт, состояние систем не пишет,
//   кроме `asda.UI.JuiCloseAll` (снос окон — ровно то, что делает вход в мир `RevealWorld`; нужен, чтобы
//   прогнать ветку «кэш окна протух → DropStale → пересоздать»). Лог сравнивается «до/после» волны.
//
//   asda.UI.JuiWinDump <метка> [id…|all] — по каждому окну (all — все окна реестра): класс, видимость корня, z в реестре, во вьюпорте ли,
//                                        `IsWindowLive`, `IsWindowVisible`, родитель/внешний объект, прямоугольник;
//                                        затем зоны, верхний слой и обработчики колеса оконной подсистемы.
//   asda.UI.JuiSlot <узел>            — все живые узлы с этим именем: окно, класс, видимость, оффсеты слота канвы
//                                        (Left/Top/Right/Bottom — ими пишет заливка полос), экранный прямоугольник.
//   asda.UI.JuiCloseAll               — `UAsdaWindowSubsystem::CloseAll()`.

// ★ Ревизия 15.09: только не-Shipping — `asda.UI.JuiCloseAll` меняет состояние окон
#if !UE_BUILD_SHIPPING
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Widget.h"
#include "Components/Image.h"                 // ★ В1-B: кисть картинки в `asda.UI.JuiSlot`
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "AssetRegistry/AssetRegistryModule.h"           // ★ В2-B: перепись якорей всех WBP (`asda.UI.AnchorCensus`)
#include "Blueprint/WidgetBlueprintGeneratedClass.h"     // ★ В2-B: дерево-архетип класса WBP (без создания экземпляра)
#include "Components/Button.h"                           // ★ В2-B: число привязок кнопок (`asda.UI.TreeCensus`)
#include "Components/CheckBox.h"                         // ★ В2-B: то же у флажков
#include "Components/PanelWidget.h"                      // ★ В2-B: дети корня
#include "Misc/FileHelper.h"                             // ★ В2-B: список размеров для `asda.UI.WholeIconCensus`
#include "Resources/AsdaTextureSubsystem.h"              // ★ В2-B: загрузка текстур тем же путём, что окна
#include "Resources/AsdaAssetPaths.h"                    // ★ В2-B: `JuiTextureObject`

DEFINE_LOG_CATEGORY_STATIC(LogAsdaWave3Jui, Log, All);

namespace
{
	// Окна, чьи `Ensure*` / зоны / меню / полосы трогает волна 3 (+ контрольные, которых она не трогает).
	const TCHAR* const kProbeWindows[] = {
		TEXT("Constellation_S_WIN"), TEXT("SHELLITEM_STORE2_WIN"), TEXT("EXTRACT_WIN"), TEXT("ItemOptionValue_WIN"),
		TEXT("Item_Initializ_Win"), TEXT("Find_Party_WIn"), TEXT("INVEN_Repair_Win"), TEXT("Title_Win"),
		TEXT("HONOR_itemmall_Win"), TEXT("ICON_mssageBOX"), TEXT("NPCSHOP_WIN"), TEXT("WARE_WIN"), TEXT("Warehouse_Win"),
		TEXT("COMMUNITY_WIN"), TEXT("CLAN_MAKING_WIN"), TEXT("CLAN_DELETE_BOX"), TEXT("PARTYTAKE_WIN"),
		TEXT("PARTYBOX_WIN"), TEXT("PARTY_WIN"), TEXT("Follow_Another_win"), TEXT("HAIRSHOP_WIN"),
		TEXT("BOOSTER_SHOP_WIN"), TEXT("SHELLITEM_STORE_WIN"), TEXT("Premium_Menu"), TEXT("SHELLITEM_PURCHASE_BOX"),
		TEXT("BOOSTING_WIN"), TEXT("BOOSTER_PREV_WIN"), TEXT("Package_Contents_Win"), TEXT("TRANTOOL_WIN"),
		TEXT("Open_Item_Win"), TEXT("Opend_Item_Win"), TEXT("QUEST_WIN"), TEXT("MINISTATE_WIN"), TEXT("CHARAINFO_WIN"),
		TEXT("INVEN_WIN") };

	// Оконная подсистема мира консольной команды.
	UAsdaWindowSubsystem* Wave3WindowSub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	}

	// Одна строка состояния окна реестра.
	void DumpOneWindow(UAsdaWindowSubsystem* S, const FString& Label, const FName Id)
	{
		UUserWidget* Win = S->FindWindow(Id);
		if (!Win)
		{
			UE_LOG(LogAsdaWave3Jui, Log, TEXT("[волна3] %s %s: в реестре нет"), *Label, *Id.ToString());
			return;
		}
		FVector2D Mn, Mx;
		const bool bRect = AsdaUi::WidgetAbsRect(Win, Mn, Mx);
		UE_LOG(LogAsdaWave3Jui, Log,
			TEXT("[волна3] %s %s: класс=%s видимость=%d z=%d вьюпорт=%d живо=%d видно=%d родитель=%s внешний=%s прямоугольник=%s (%.0f,%.0f)-(%.0f,%.0f)"),
			*Label, *Id.ToString(), *Win->GetClass()->GetName(), (int32)Win->GetVisibility(), S->GetWindowZOrder(Id),
			Win->IsInViewport() ? 1 : 0, S->IsWindowLive(Win) ? 1 : 0, S->IsWindowVisible(Id) ? 1 : 0,
			Win->GetParent() ? *Win->GetParent()->GetName() : TEXT("нет"),
			Win->GetOuter() ? *Win->GetOuter()->GetClass()->GetName() : TEXT("нет"),
			bRect ? TEXT("да") : TEXT("нет"), Mn.X, Mn.Y, Mx.X, Mx.Y);
	}

	FAutoConsoleCommandWithWorldAndArgs GWave3JuiWinDump(
		TEXT("asda.UI.JuiWinDump"),
		TEXT("asda.UI.JuiWinDump <метка> [id…] — состояние окон волны 3 (класс, видимость, z, вьюпорт, живо, видно, родитель, прямоугольник) + зоны/верхний слой/колесо."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
		{
			UAsdaWindowSubsystem* S = Wave3WindowSub(W);
			if (!S) { UE_LOG(LogAsdaWave3Jui, Warning, TEXT("[волна3] нет оконной подсистемы")); return; }
			const FString Label = A.Num() > 0 ? A[0] : FString(TEXT("-"));
			// ★ 11.10.2026 (волна В0 аудита дублей №2): `all` — все окна реестра по имени (эталон «до» для волн В1–В3).
			if (A.Num() == 2 && A[1] == TEXT("all"))
			{
				TArray<FName> Ids = S->GetRegisteredWindowIds();
				Ids.Sort([](const FName& L, const FName& R) { return L.ToString() < R.ToString(); });
				for (const FName& Id : Ids) { DumpOneWindow(S, Label, Id); }
			}
			else if (A.Num() > 1)
			{
				for (int32 i = 1; i < A.Num(); ++i) { DumpOneWindow(S, Label, FName(*A[i])); }
			}
			else
			{
				for (const TCHAR* Id : kProbeWindows) { DumpOneWindow(S, Label, FName(Id)); }
			}
			UE_LOG(LogAsdaWave3Jui, Log, TEXT("[волна3] %s ЗОНЫ:\n%s\n%s\n%s"), *Label,
				*S->DebugUiZones(), *S->DebugTopmost(), *S->DebugWheelHandlers());
		}));

	FAutoConsoleCommandWithWorldAndArgs GWave3JuiSlot(
		TEXT("asda.UI.JuiSlot"),
		TEXT("asda.UI.JuiSlot <узел> — живые узлы с этим именем: окно, класс, видимость, оффсеты слота канвы, экранный прямоугольник."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
		{
			if (!W || A.Num() < 1) { return; }
			// `ИМЯ*` — все узлы, чьё имя начинается с `ИМЯ` (имена планов полос импортёр нумерует обходом).
			const bool bPrefix = A[0].EndsWith(TEXT("*"));
			const FString Name = bPrefix ? A[0].LeftChop(1) : A[0];
			int32 Found = 0;
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* UW = *It;
				if (!UW || UW->GetWorld() != W || !UW->WidgetTree) { continue; }
				UW->WidgetTree->ForEachWidget([&](UWidget* Wd)
				{
					if (!Wd) { return; }
					const FString WdName = Wd->GetName();
					if (bPrefix ? !WdName.StartsWith(Name, ESearchCase::CaseSensitive) : WdName != Name) { return; }
					++Found;
					FString Off = TEXT("слота канвы нет");
					if (const UCanvasPanelSlot* CS = AsdaUi::SlotOf(Wd))
					{
						const FMargin O = CS->GetOffsets();
						Off = FString::Printf(TEXT("оффсеты L=%.2f T=%.2f R=%.2f B=%.2f"), O.Left, O.Top, O.Right, O.Bottom);
					}
					FVector2D Mn, Mx;
					const bool bRect = AsdaUi::WidgetAbsRect(Wd, Mn, Mx);
					// ★ 11.10.2026 (дедуп №2, В1-B): у картинки — ещё и КИСТЬ (ресурс, UV, ImageSize, способ рисования):
					//   вынос иконок U4a–c доказывается равенством кистей «до/после».
					FString Brush;
					if (const UImage* Img = Cast<UImage>(Wd))
					{
						const FSlateBrush& B = Img->GetBrush();
						const UObject* Res = B.GetResourceObject();
						const FBox2f UV = B.GetUVRegion();
						Brush = FString::Printf(TEXT(" кисть=%s uv=(%.6f,%.6f)-(%.6f,%.6f)%s размер=%.2fx%.2f рисовать=%d"),
							Res ? *Res->GetPathName() : TEXT("нет"), UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y,
							UV.bIsValid ? TEXT("") : TEXT("(весь)"), B.ImageSize.X, B.ImageSize.Y, (int32)B.DrawAs);
					}
					UE_LOG(LogAsdaWave3Jui, Log, TEXT("[волна3] slot %s (%s в %s): видимость=%d %s прямоугольник=%s (%.0f,%.0f)-(%.0f,%.0f)%s"),
						*Name, *Wd->GetClass()->GetName(), *UW->GetClass()->GetName(), (int32)Wd->GetVisibility(), *Off,
						bRect ? TEXT("да") : TEXT("нет"), Mn.X, Mn.Y, Mx.X, Mx.Y, *Brush);
				});
			}
			if (Found == 0) { UE_LOG(LogAsdaWave3Jui, Log, TEXT("[волна3] slot %s: узла нет ни в одном окне"), *Name); }
		}));

	// ═══════════ ★ ДЕДУП №2, В2-B (11.10.2026): ДИАГНОСТИКА ИСПРАВЛЕНИЙ U15 / U4d / сброса сессии ═══════════

	// Строка якорей слота канвы: «POINT» или «SPAN» по каждой оси + анкоры и оффсеты.
	FString AnchorText(const UCanvasPanelSlot* CS)
	{
		const FAnchors A = CS->GetAnchors();
		const FMargin O = CS->GetOffsets();
		const bool bSx = !FMath::IsNearlyEqual(A.Minimum.X, A.Maximum.X);
		const bool bSy = !FMath::IsNearlyEqual(A.Minimum.Y, A.Maximum.Y);
		return FString::Printf(TEXT("X=%s Y=%s анкоры=(%.3f,%.3f)-(%.3f,%.3f) оффсеты L=%.2f T=%.2f R=%.2f B=%.2f"),
			bSx ? TEXT("SPAN") : TEXT("POINT"), bSy ? TEXT("SPAN") : TEXT("POINT"),
			A.Minimum.X, A.Minimum.Y, A.Maximum.X, A.Maximum.Y, O.Left, O.Top, O.Right, O.Bottom);
	}

	// Растянут ли слот хотя бы по одной оси.
	bool IsSpanSlot(const UCanvasPanelSlot* CS)
	{
		const FAnchors A = CS->GetAnchors();
		return !FMath::IsNearlyEqual(A.Minimum.X, A.Maximum.X) || !FMath::IsNearlyEqual(A.Minimum.Y, A.Maximum.Y);
	}

	// `asda.UI.AnchorCensus [WBP…]` — ПЕРЕПИСЬ ЯКОРЕЙ ПО АРХЕТИПАМ (экземпляры не создаются, окна не открываются, данные не нужны).
	//   Без аргументов: каждый WBP под /Game/AsdaAssets/UI — число детей корня и сколько из них SPAN (+ строка на каждого SPAN-ребёнка),
	//   и сколько SPAN-узлов во всём дереве. С аргументами (имена WBP без пути): ещё и КАЖДЫЙ ребёнок корня и каждый SPAN-узел дерева
	//   с родителем. Нужна U15: сдвиг окна «всем детям корня Left/Top» безопасен, только если SPAN-детей нет (`00_PLAN.md` §5 В2).
	FAutoConsoleCommandWithWorldAndArgs GW2BAnchorCensus(
		TEXT("asda.UI.AnchorCensus"),
		TEXT("asda.UI.AnchorCensus [WBP…] — якоря детей корня всех WBP (архетипы); с именами — подробно по этим WBP."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			TSet<FString> Detail;
			for (const FString& S : Args) { Detail.Add(S); }
			IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			TArray<FAssetData> Assets;
			AR.GetAssetsByPath(FName(TEXT("/Game/AsdaAssets/UI")), Assets, /*bRecursive*/true);
			Assets.Sort([](const FAssetData& L, const FAssetData& R) { return L.PackageName.LexicalLess(R.PackageName); });
			int32 NWbp = 0, NWithSpanRoot = 0, NSpanRootTotal = 0, NFound = 0;
			for (const FAssetData& AD : Assets)
			{
				const FString Name = AD.AssetName.ToString();
				if (!Name.StartsWith(TEXT("WBP_"))) { continue; }
				const FString ClassPath = FString::Printf(TEXT("%s.%s_C"), *AD.PackageName.ToString(), *Name);
				UWidgetBlueprintGeneratedClass* G = Cast<UWidgetBlueprintGeneratedClass>(
					LoadObject<UClass>(nullptr, *ClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet));
				UWidgetTree* T = G ? G->GetWidgetTreeArchetype() : nullptr;
				if (!T) { continue; }
				++NWbp;
				const bool bDetail = Detail.Contains(Name);
				if (bDetail) { ++NFound; }
				UPanelWidget* Root = Cast<UPanelWidget>(T->RootWidget);
				int32 NChild = 0, NSpan = 0;
				TArray<FString> SpanLines;
				if (Root)
				{
					for (int32 i = 0; i < Root->GetChildrenCount(); ++i)
					{
						UWidget* C = Root->GetChildAt(i);
						const UCanvasPanelSlot* CS = C ? Cast<UCanvasPanelSlot>(C->Slot) : nullptr;
						if (!CS) { continue; }
						++NChild;
						const bool bSpan = IsSpanSlot(CS);
						if (bSpan) { ++NSpan; }
						if (bSpan || bDetail)
						{
							SpanLines.Add(FString::Printf(TEXT("[якоря] %s корень/%s (%s): %s"), *Name, *C->GetName(),
								*C->GetClass()->GetName(), *AnchorText(CS)));
						}
					}
				}
				int32 NSpanTree = 0;
				TArray<FString> TreeLines;
				T->ForEachWidget([&](UWidget* W)
				{
					const UCanvasPanelSlot* CS = W ? Cast<UCanvasPanelSlot>(W->Slot) : nullptr;
					if (!CS || !IsSpanSlot(CS)) { return; }
					++NSpanTree;
					if (bDetail)
					{
						TreeLines.Add(FString::Printf(TEXT("[якоря] %s дерево/%s (%s, родитель %s): %s"), *Name, *W->GetName(),
							*W->GetClass()->GetName(), W->GetParent() ? *W->GetParent()->GetName() : TEXT("нет"), *AnchorText(CS)));
					}
				});
				if (NSpan > 0) { ++NWithSpanRoot; NSpanRootTotal += NSpan; }
				UE_LOG(LogAsdaWave3Jui, Log, TEXT("[якоря] %s: корень=%s детей корня %d, SPAN среди них %d; SPAN-узлов в дереве %d"),
					*Name, Root ? *Root->GetClass()->GetName() : TEXT("нет"), NChild, NSpan, NSpanTree);
				for (const FString& L : SpanLines) { UE_LOG(LogAsdaWave3Jui, Log, TEXT("%s"), *L); }
				for (const FString& L : TreeLines) { UE_LOG(LogAsdaWave3Jui, Log, TEXT("%s"), *L); }
			}
			UE_LOG(LogAsdaWave3Jui, Log, TEXT("[якоря] ИТОГ: WBP %d, с SPAN-детьми корня %d (всего SPAN-детей %d); подробно запрошено %d, найдено %d"),
				NWbp, NWithSpanRoot, NSpanRootTotal, Detail.Num(), NFound);
		}));

	// `asda.UI.TreeCensus <метка>` — ПЕРЕПИСЬ ЖИВЫХ ОКОН РЕЕСТРА: адрес экземпляра, число узлов дерева, число привязок кнопок и флажков.
	//   Доказательство исправления сброса сессии: окно, пересобранное поверх старого дерева, сохраняет АДРЕС, а узлы/привязки растут
	//   (клоны, вторые реле); окно, закрытое при сбросе и созданное заново, получает НОВЫЙ адрес и те же числа, что при первой сборке.
	FAutoConsoleCommandWithWorldAndArgs GW2BTreeCensus(
		TEXT("asda.UI.TreeCensus"),
		TEXT("asda.UI.TreeCensus <метка> — по каждому окну реестра: адрес, узлов в дереве, привязок кнопок/флажков."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
		{
			UAsdaWindowSubsystem* S = Wave3WindowSub(W);
			if (!S) { UE_LOG(LogAsdaWave3Jui, Warning, TEXT("[дерево] нет оконной подсистемы")); return; }
			const FString Label = A.Num() > 0 ? A[0] : FString(TEXT("-"));
			TArray<FName> Ids = S->GetRegisteredWindowIds();
			Ids.Sort([](const FName& L, const FName& R) { return L.ToString() < R.ToString(); });
			for (const FName& Id : Ids)
			{
				UUserWidget* Win = S->FindWindow(Id);
				if (!Win || !Win->WidgetTree) { continue; }
				TArray<UWidget*> All;
				Win->WidgetTree->GetAllWidgets(All);
				int32 NBind = 0, NOrphan = 0;
				for (UWidget* Wd : All)
				{
					if (!Wd) { continue; }
					if (Wd != Win->WidgetTree->RootWidget && !Wd->GetParent()) { ++NOrphan; }
					if (const UButton* B = Cast<UButton>(Wd))
					{
						NBind += B->OnClicked.GetAllObjects().Num() + B->OnPressed.GetAllObjects().Num()
						       + B->OnReleased.GetAllObjects().Num() + B->OnHovered.GetAllObjects().Num()
						       + B->OnUnhovered.GetAllObjects().Num();
					}
					else if (const UCheckBox* C = Cast<UCheckBox>(Wd))
					{
						NBind += C->OnCheckStateChanged.GetAllObjects().Num();
					}
				}
				UE_LOG(LogAsdaWave3Jui, Log, TEXT("[дерево] %s %s: экземпляр=%p узлов=%d без_родителя=%d привязок=%d"),
					*Label, *Id.ToString(), Win, All.Num(), NOrphan, NBind);
			}
			UE_LOG(LogAsdaWave3Jui, Log, TEXT("[дерево] %s ИТОГ: окон в реестре %d"), *Label, Ids.Num());
		}));

	// `asda.UI.WholeIconCensus <путь.tsv>` — U4d: логический размер текстур «целиком» (эталон `DrawTextureRect(tex, 0, 0, W, H)`).
	//   Файл: строки «имя<TAB>ширина<TAB>высота» (размеры исходников оригинала). По каждому имени — `LogicalAtlasSize` у текстуры, взятой
	//   ОБОИМИ путями окон (`LoadTextureCached(JuiTextureObject)` — хотбар/окно навыков, `LoadUITexture` — хранитель/портреты).
	//   Расхождение «логический ≠ исходный» значило бы, что крой по эталону покажет не то, что «текстура целиком».
	FAutoConsoleCommandWithWorldAndArgs GW2BWholeIconCensus(
		TEXT("asda.UI.WholeIconCensus"),
		TEXT("asda.UI.WholeIconCensus <путь.tsv> — логический размер текстур против размеров исходников (U4d)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld*)
		{
			TArray<FString> Lines;
			if (A.Num() < 1 || !FFileHelper::LoadFileToStringArray(Lines, *A[0]))
			{
				UE_LOG(LogAsdaWave3Jui, Warning, TEXT("[иконки32] нет файла списка: %s"), A.Num() > 0 ? *A[0] : TEXT("(не задан)"));
				return;
			}
			UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
			if (!TexMgr) { UE_LOG(LogAsdaWave3Jui, Warning, TEXT("[иконки32] нет менеджера текстур")); return; }
			int32 NOk = 0, NBad = 0, NMiss = 0;
			for (const FString& L : Lines)
			{
				TArray<FString> F;
				L.ParseIntoArray(F, TEXT("\t"));
				if (F.Num() < 3) { continue; }
				const FIntPoint Want(FCString::Atoi(*F[1]), FCString::Atoi(*F[2]));
				UTexture2D* T1 = TexMgr->LoadTextureCached(AsdaAssetPaths::JuiTextureObject(F[0]));
				UTexture2D* T2 = TexMgr->LoadUITexture(FName(*F[0]));
				if (!T1 && !T2) { ++NMiss; UE_LOG(LogAsdaWave3Jui, Log, TEXT("[иконки32] %s: текстуры нет ни одним путём"), *F[0]); continue; }
				const FIntPoint S1 = T1 ? AsdaUi::LogicalAtlasSize(T1) : FIntPoint(-1, -1);
				const FIntPoint S2 = T2 ? AsdaUi::LogicalAtlasSize(T2) : FIntPoint(-1, -1);
				const bool bOk = (!T1 || S1 == Want) && (!T2 || S2 == Want);
				if (bOk) { ++NOk; }
				else
				{
					++NBad;
					UE_LOG(LogAsdaWave3Jui, Log, TEXT("[иконки32] РАСХОЖДЕНИЕ %s: исходник %dx%d, кэш %dx%d, UI %dx%d"),
						*F[0], Want.X, Want.Y, S1.X, S1.Y, S2.X, S2.Y);
				}
			}
			UE_LOG(LogAsdaWave3Jui, Log, TEXT("[иконки32] ИТОГ: совпало %d, расхождений %d, нет текстуры %d"), NOk, NBad, NMiss);
		}));

	// `asda.UI.FrameFormulaCheck` — U19: три прежних расчёта позиции рамки над ячейкой против общей точки на ЖИВЫХ деревьях.
	//   Для каждого окна реестра: каждый узел со слотом канвы — «рамка», каждый узел с картинкой — «ячейка» (без мутаций):
	//   (1) ItemOption/Repair/Warehouse/HuntReport/ExtractShop: CanvasRootPos(ячейка) − (CanvasRootPos(рамка) − свой угол);
	//   (2) PetHatchery: CanvasRootPos(ячейка) − CanvasRootPos(родитель рамки);
	//   (3) Disassembly: угол слота ячейки — только если у рамки и ячейки один родитель (иначе формула неприменима).
	//   `AsdaUi::PlaceFrameOver` считает по (1). Печать — число пар и расхождений > 1e-3.
	FAutoConsoleCommandWithWorldAndArgs GW2BFrameFormulaCheck(
		TEXT("asda.UI.FrameFormulaCheck"),
		TEXT("asda.UI.FrameFormulaCheck — U19: прежние формулы рамки выбора против общей на живых окнах реестра."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			UAsdaWindowSubsystem* S = Wave3WindowSub(W);
			if (!S) { return; }
			int64 NPairs = 0, NBad12 = 0, NSame = 0, NBad13 = 0;
			for (const FName& Id : S->GetRegisteredWindowIds())
			{
				UUserWidget* Win = S->FindWindow(Id);
				if (!Win || !Win->WidgetTree) { continue; }
				TArray<UWidget*> All;
				Win->WidgetTree->GetAllWidgets(All);
				TArray<UWidget*> Cells;
				for (UWidget* Wd : All) { if (Cast<UImage>(Wd) && AsdaUi::SlotOf(Wd)) { Cells.Add(Wd); } }
				if (Cells.Num() > 64) { Cells.SetNum(64); }   // пары растут квадратично — 64 ячеек на окно достаточно
				for (UWidget* F : All)
				{
					const UCanvasPanelSlot* FS = F ? AsdaUi::SlotOf(F) : nullptr;
					if (!FS) { continue; }
					const FVector2D Own(FS->GetOffsets().Left, FS->GetOffsets().Top);
					const FVector2D PR1 = AsdaUi::CanvasRootPos(F) - Own;
					const FVector2D PR2 = F->GetParent() ? AsdaUi::CanvasRootPos(F->GetParent()) : FVector2D::ZeroVector;
					for (UWidget* C : Cells)
					{
						const FVector2D CR = AsdaUi::CanvasRootPos(C);
						const FVector2D P1 = CR - PR1, P2 = CR - PR2;
						++NPairs;
						if (!P1.Equals(P2, 1e-3)) { ++NBad12; }
						if (C->GetParent() == F->GetParent())
						{
							++NSame;
							const UCanvasPanelSlot* CS = AsdaUi::SlotOf(C);
							const FVector2D P3(CS->GetOffsets().Left, CS->GetOffsets().Top);
							if (!P1.Equals(P3, 1e-3)) { ++NBad13; }
						}
					}
				}
			}
			UE_LOG(LogAsdaWave3Jui, Log, TEXT("[рамка] ИТОГ: пар %lld, расхождений (1)≠(2) %lld; пар с общим родителем %lld, расхождений (1)≠(3) %lld"),
				NPairs, NBad12, NSame, NBad13);
		}));

	FAutoConsoleCommandWithWorldAndArgs GWave3JuiCloseAll(
		TEXT("asda.UI.JuiCloseAll"),
		TEXT("asda.UI.JuiCloseAll — снести все окна (как вход в мир), чтобы прогнать ветку «кэш окна протух»."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			if (UAsdaWindowSubsystem* S = Wave3WindowSub(W))
			{
				S->CloseAll();
				UE_LOG(LogAsdaWave3Jui, Log, TEXT("[волна3] CloseAll выполнен"));
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
