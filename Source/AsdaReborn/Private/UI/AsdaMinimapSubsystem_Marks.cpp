// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMinimapSubsystem_Marks.cpp — ★ R3-c 12.11.2026 (разрез AsdaMinimapSubsystem.cpp, дословный перенос): метки, точка назначения, GM-телепорт, клик по карте, описания, дев-команды.
//   Общее для частей — в AsdaMinimapSubsystem_Private.h; шапка и реверс — в AsdaMinimapSubsystem.cpp.
#include "UI/AsdaMinimapSubsystem.h"


#include "GameFramework/SpringArmComponent.h"   // фолбэк угла конуса: мировой yaw стрелы камеры

#include "Test/AsdaSelfTestKit.h"          // ★ 18.10.2026: каркас самотеста (отказ онлайн, итог, quit)
#include "AsdaMinimapSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaMinimapSubsystem*.cpp

// ── Внешние списки ───────────────────────────────────────────────────────────────────────────────
void UAsdaMinimapSubsystem::SetMarks(EAsdaMapMark Kind, const TArray<FAsdaMapMark>& InMarks)
{
	Marks.FindOrAdd((uint8)Kind) = InMarks;
}

void UAsdaMinimapSubsystem::ClearMarks(EAsdaMapMark Kind) { Marks.Remove((uint8)Kind); }

// ★ Диагностика (волна 1-Б): канва слоя маркеров (тело здесь — в заголовке UCanvasPanel неполный тип).
UCanvasPanel* UAsdaMinimapSubsystem::GetMarkLayer(bool bBig) const { return bBig ? BigMarkLayer.Get() : MarkLayer.Get(); }

// ★ Диагностика (волна 1-Б): видимые квады пула с кистью текстуры этого типа — то, что видит игрок.
int32 UAsdaMinimapSubsystem::CountDrawnMarks(EAsdaMapMark Kind, bool bBig, TArray<UImage*>* OutImages) const
{
	const TObjectPtr<UTexture2D>* T = MarkTex.Find((uint8)Kind);
	if (!T || !*T) { return 0; }
	const TArray<TObjectPtr<UImage>>& Pool = bBig ? BigMarkPool : MarkPool;
	int32 N = 0;
	for (const TObjectPtr<UImage>& Img : Pool)
	{
		if (!Img || Img->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
		if (Img->GetBrush().GetResourceObject() != T->Get()) { continue; }
		++N;
		if (OutImages) { OutImages->Add(Img.Get()); }
	}
	return N;
}

void UAsdaMinimapSubsystem::SetDestinationTile(int32 TileX, int32 TileY, bool bActive)
{
	DestTileX = TileX; DestTileZ = TileY; bDestActive = bActive;
}

// ── ★ 09.11.2026 [ПОРТ] GM-телепорт Ctrl+ЛКМ (разбор — шапка `UAsdaGmOriginalSubsystem::TryMapClickTeleport`) ──────────
//   Ctrl читается у контроллера (как Ctrl+Shift+Alt+Q входа в GM-меню). true — клик забрал GM-телепорт (ход/автопуть не нужны);
//   false — Ctrl не зажат, ключ выключен или персонаж не GM_: клик идёт обычным путём, ничего лишнего не отправляется.
bool UAsdaMinimapSubsystem::TryGmMapTeleport(AAsdaPlayerCharacter* P, int32 ClickMapId, const FVector2D& Tile, const UAsdaWalkGrid* G, bool bBig)
{
	const APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	if (!PC || !(PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl))) { return false; }
	UAsdaGmOriginalSubsystem* Gm = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaGmOriginalSubsystem>() : nullptr;
	if (!Gm) { return false; }
	const int32 Tx = FMath::FloorToInt(Tile.X), Tz = FMath::FloorToInt(Tile.Y);
	// Размер своей карты в клетках — сетка проходимости (.TER); для чужой карты он не нужен (там 6010 не уходит).
	const EAsdaGmMapTp R = Gm->TryMapClickTeleport(ClickMapId, MapId, Tx, Tz, G ? G->Width : 0, G ? G->Height : 0, bBig);
	UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] Ctrl+клик (%s) карта=%d тайл=(%d,%d) → GM-телепорт: %d"),
		bBig ? TEXT("карта мира") : TEXT("мини-карта"), ClickMapId, Tx, Tz, (int32)R);
	if (R == EAsdaGmMapTp::NotGm) { return false; }
	++GmTeleportClickCount;
	return true;
}

// ── Клик по карте: пиксель → тайл → world = 100*t+50 → идти (world_map.c:2596-2612) ──────────────
void UAsdaMinimapSubsystem::HandleMapClick(const FVector2D& LocalPx, bool bBig)
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastClickTime < 0.25) { return; }          // троттл 250 мс (g_dwBigMapClickThrottle)
	LastClickTime = Now;

	AAsdaPlayerCharacter* P = nullptr;
	const UAsdaWalkGrid* G = GetGrid(P);
	if (!P || !G) { return; }

	FVector2D Tile;
	if (bBig)
	{
		// ★ 10.11.2026 [ОРИГИНАЛ] обучение: `CBigMap_RenderAndHandleClick` (ui_core.c:5287) — клик по полю после троттла → байт 74.
		AsdaTutorial::Mark(this, EAsdaTutorialFlag::BigMapClick);
		// Эталон: tile = scrollOff + local * (mapTexSize/mapDrawW); прокрутки у поля 512×512 нет.
		Tile = LocalPx / FMath::Max(BigMapTileToPx(), KINDA_SMALL_NUMBER);
		FVector2D O; float S;
		if (bDisplayTown && TownCrop(DisplayMapId, O, S)) { Tile = Tile / S + O; }

		// ★ 09.11.2026 GM-телепорт: Ctrl+ЛКМ по карте мира — клетка выбранного региона (`DisplayMapId`). Не GM — дальше как было.
		if (TryGmMapTeleport(P, DisplayMapId, Tile, G, true)) { return; }

		// ★ Показана ЧУЖАЯ карта → это не «идти», а ЗАЯВКА НА МЕЖКАРТОВЫЙ ПЕРЕХОД
		//   (эталон beginMoveToTile@0xFBE140: тайл ≤ 512, ставит цель mapId/tileX/tileY; исполняет
		//    её система авто-маршрута через порталы — она портируется отдельно).
		if (DisplayMapId != MapId)
		{
			const int32 Ttx = FMath::FloorToInt(Tile.X);
			const int32 Tty = FMath::FloorToInt(Tile.Y);
			// ★ ЕДИНАЯ точка взвода автопути — та же, что зовут кнопки маршрута журнала заданий.
			//   Раньше поля состояния писались здесь напрямую, и исполнителя у заявки не было.
			BeginMapTravel(DisplayMapId, Ttx, Tty);
			return;
		}
	}
	else
	{
		const FVector2D Pos = P->GetGamePosXZ();
		const float Scale = WorldToTexelScale(G);
		const FVector2D Scroll = ScrollTexels(FVector2D(Pos.X * Scale, Pos.Y * Scale));
		const FVector2D Texel = Scroll + LocalPx / MarkPxPerTexel();
		Tile = Texel / (Scale * 100.f);                   // тексел → тайл
		// ★ 09.11.2026 GM-телепорт: Ctrl+ЛКМ по мини-карте — клетка своей карты. Не GM — дальше как было.
		if (TryGmMapTeleport(P, MapId, Tile, G, false)) { return; }
	}
	const int32 Tx = FMath::FloorToInt(Tile.X);
	const int32 Tz = FMath::FloorToInt(Tile.Y);
	const FVector2D Dst(100.f * Tx + 50.f, 100.f * Tz + 50.f);
	if (!G->IsTileWalkable(Tx, Tz))
	{
		// ★ ЭТАЛОН (world_map.c:2620/4979, ui_core.c:5328): пик по проходимой геометрии не удался →
		//   loc 1013 «Недоступная область» золотым −11514 на 10 с в СИСТЕМНЫЙ ЧАТ + error.wav.
		//   Было: только UE_LOG — игрок не видел НИЧЕГО. Теперь через единый маршрутизатор.
		if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
		{
			FAsdaMsgParams MP;
			MP.Category = EAsdaMsgCategory::MoveRefused;
			MP.LocId = 1013;
			// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — нет строки 1013 → «[#1013]» (общая точка UAsdaMessageSubsystem).
			Msg->Post(MP);
		}
		UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] клик вне проходимой зоны: тайл=(%d,%d) → нотис 1013"), Tx, Tz);
		return;
	}
	SetDestinationTile(Tx, Tz, true);
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(UGameplayStatics::GetPlayerController(P, 0)))
	{
		PC->MoveToGamePoint(Dst);
	}
	UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] клик=(%.0f,%.0f)px → тайл=(%d,%d) → мир=(%.0f,%.0f)"),
		LocalPx.X, LocalPx.Y, Tx, Tz, Dst.X, Dst.Y);
}

// ── Диагностика карты мира ───────────────────────────────────────────────────────────────────────
FString UAsdaMinimapSubsystem::DescribeWorldMap() const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	FString AreaName = TEXT("—");
	int32 i = 0;
	for (const FAsdaWorldMapArea& A : WorldMapAreas)
	{
		if (A.Continent != SelContinent) { continue; }
		if (i == SelArea && TS) { AreaName = TS->GetTextById(A.TextId); break; }
		++i;
	}
	return FString::Printf(
		TEXT("[карта мира] окно=%s | своя карта=%d | показана=%d (%s) | континент=%d область=%d «%s» ")
		TEXT("| тайл→px=%.3f | курсор=%s тайл=(%d,%d) | переход: %s"),
		bBigMapVisible ? TEXT("открыто") : TEXT("закрыто"), MapId, DisplayMapId,
		bDisplayTown ? TEXT("город") : TEXT("поле"), SelContinent, SelArea, *AreaName,
		BigMapTileToPx(), bHoverInMap ? TEXT("над картой") : TEXT("вне"), HoverTile.X, HoverTile.Y,
		bTravelActive ? *FString::Printf(TEXT("карта=%d тайл=(%d,%d)"), TravelMapId, TravelTile.X, TravelTile.Y)
		              : TEXT("нет"));
}

// ── Диагностика ──────────────────────────────────────────────────────────────────────────────────
FString UAsdaMinimapSubsystem::DescribeState() const
{
	AAsdaPlayerCharacter* P = nullptr;
	const UAsdaWalkGrid* G = GetGrid(P);
	if (!P || !G) { return TEXT("[миникарта] игрок/сетка не готовы"); }
	const float Scale = WorldToTexelScale(G);
	const FVector2D Pos = P->GetGamePosXZ();
	const FVector2D Texel(Pos.X * Scale, Pos.Y * Scale);
	const FVector2D Scroll = ScrollTexels(Texel);
	const FVector2D Px = (Texel - Scroll) * MarkPxPerTexel();
	return FString::Printf(
		TEXT("[миникарта] карта=%d режим=%s зум=%s зона=%s окно=%.0fpx видно=%.0f тексел ")
		TEXT("| игрок=(%.0f,%.0f) тайл=(%d,%d) → тексел=(%.2f,%.2f) scroll=(%.1f,%.1f) → пиксель=(%.1f,%.1f)"),
		MapId, bExpanded ? TEXT("развёрнутая") : TEXT("компактная"), bZoom ? TEXT("вкл") : TEXT("выкл"),
		bTownMode ? TEXT("город") : TEXT("поле"), MapPx(), ViewTexels(),
		Pos.X, Pos.Y, FMath::FloorToInt(Pos.X / 100.f), FMath::FloorToInt(Pos.Y / 100.f),
		Texel.X, Texel.Y, Scroll.X, Scroll.Y, Px.X, Px.Y);
}

FString UAsdaMinimapSubsystem::DescribeProbe(float GameX, float GameZ) const
{
	AAsdaPlayerCharacter* P = nullptr;
	const UAsdaWalkGrid* G = GetGrid(P);
	if (!P || !G) { return TEXT("[миникарта] игрок/сетка не готовы"); }
	const float Scale = WorldToTexelScale(G);
	const FVector2D Pos = P->GetGamePosXZ();
	const FVector2D Scroll = ScrollTexels(FVector2D(Pos.X * Scale, Pos.Y * Scale));
	const FVector2D Texel(GameX * Scale, GameZ * Scale);
	const FVector2D Px = (Texel - Scroll) * MarkPxPerTexel();
	return FString::Printf(TEXT("[миникарта] мир=(%.0f,%.0f) → тексел=(%.3f,%.3f) → пиксель=(%.2f,%.2f)%s"),
		GameX, GameZ, Texel.X, Texel.Y, Px.X, Px.Y,
		(Px.X >= 0.f && Px.Y >= 0.f && Px.X < MapPx() && Px.Y < MapPx()) ? TEXT("") : TEXT("  [вне окна]"));
}

// ── ★ Диагностика конуса обзора (Asda.Minimap.Pov) ───────────────────────────────────────────────
//   Печатает ИСТОЧНИКИ угла и угол, реально лежащий на квадах `miniMyDir`. Квад ищется по текстуре
//   в пуле (нового состояния подсистемы не заводим — правило «при пересоздании дерева сбрасывать
//   ВСЕ привязанные кэши» этим не нарушается: кэшировать нечего).
FString UAsdaMinimapSubsystem::DescribePov() const
{
	AAsdaPlayerCharacter* P = nullptr;
	const UAsdaWalkGrid* G = GetGrid(P);
	if (!P || !G) { return TEXT("[миникарта/POV] павна нет — карта ещё не активна"); }

	const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(P->GetController());
	const float ConeYaw = AsdaCameraConeYaw(P);
	const float ArmYaw  = P->CameraArm ? P->CameraArm->GetComponentRotation().Yaw : 0.f;
	const float FaceYaw = P->GetFacingYawUE();

	const TObjectPtr<UTexture2D>* ConeT = MarkTex.Find((uint8)EAsdaMapMark::PlayerDir);
	auto QuadAngle = [&](const TArray<TObjectPtr<UImage>>& Pool) -> FString
	{
		if (!ConeT || !*ConeT) { return TEXT("текстуры miniMyDir нет"); }
		for (const TObjectPtr<UImage>& Img : Pool)
		{
			if (!Img || Img->GetBrush().GetResourceObject() != ConeT->Get()) { continue; }
			if (Img->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
			return FString::Printf(TEXT("%.2f°"), Img->GetRenderTransform().Angle);
		}
		return TEXT("квад не нарисован");
	};

	return FString::Printf(
		TEXT("[миникарта/POV] орбит-камера(g_fCameraYaw)=%.2f°  стрела камеры=%.2f°  курс персонажа=%.2f°")
		TEXT("  |  конус мини-карты=%s  конус карты мира=%s%s"),
		ConeYaw, ArmYaw, FaceYaw,
		*QuadAngle(MarkPool), *QuadAngle(BigMarkPool),
		PC ? TEXT("") : TEXT("  [ВНИМАНИЕ: павном рулит не AAsdaPlayerController — угол взят у стрелы]"));
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ── Консольные команды ───────────────────────────────────────────────────────────────────────────
static FAutoConsoleCommandWithWorldAndArgs GAsdaMinimapState(
	TEXT("Asda.Minimap"),
	TEXT("Состояние мини-карты: карта/режим/зум/зона + позиция игрока → тексел → пиксель. "
	     "С аргументами <gameX> <gameZ> — проверка формулы на произвольной точке."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
			if (!MM) { UE_LOG(LogAsdaMinimap, Warning, TEXT("[миникарта] подсистема недоступна")); return; }
			if (Args.Num() >= 2)
			{
				UE_LOG(LogAsdaMinimap, Log, TEXT("%s"),
					*MM->DescribeProbe(FCString::Atof(*Args[0]), FCString::Atof(*Args[1])));
				return;
			}
			UE_LOG(LogAsdaMinimap, Log, TEXT("%s"), *MM->DescribeState());
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMinimapMode(
	TEXT("Asda.Minimap.Mode"),
	TEXT("Asda.Minimap.Mode <expand 0|1> [zoom 0|1] — развернуть/свернуть и зум мини-карты."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
			if (!MM || Args.Num() < 1) { return; }
			MM->SetExpanded(FCString::Atoi(*Args[0]) != 0);
			if (Args.Num() >= 2) { MM->SetZoom(FCString::Atoi(*Args[1]) != 0); }
			UE_LOG(LogAsdaMinimap, Log, TEXT("%s"), *MM->DescribeState());
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ── ★ Карта мира: консоль ────────────────────────────────────────────────────────────────────────
static FAutoConsoleCommandWithWorldAndArgs GAsdaWorldMap(
	TEXT("Asda.WorldMap"),
	TEXT("Карта мира (1710 MAXMAP_WIN). Без аргументов — состояние. "
	     "toggle | open | close | mypos | continent <0|1> | area <индекс>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
			if (!MM) { UE_LOG(LogAsdaMinimap, Warning, TEXT("[карта мира] подсистема недоступна")); return; }
			if (Args.Num() >= 1)
			{
				const FString& C = Args[0];
				if (C.Equals(TEXT("toggle"), ESearchCase::IgnoreCase)) { MM->ToggleWorldMap(); }
				else if (C.Equals(TEXT("open"),  ESearchCase::IgnoreCase)) { MM->SetBigMapVisible(true); }
				else if (C.Equals(TEXT("close"), ESearchCase::IgnoreCase)) { MM->SetBigMapVisible(false); }
				else if (C.Equals(TEXT("mypos"), ESearchCase::IgnoreCase)) { MM->ShowMyPosition(); }
				else if (C.Equals(TEXT("continent"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
				{
					MM->SelectContinent(FCString::Atoi(*Args[1]));
				}
				else if (C.Equals(TEXT("area"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
				{
					MM->SelectArea(FCString::Atoi(*Args[1]));
				}
			}
			UE_LOG(LogAsdaMinimap, Log, TEXT("%s"), *MM->DescribeWorldMap());
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★ Диагностика межкартового автопути (порт `beginMoveToTile` + исполнитель из главного тика).
static FAutoConsoleCommandWithWorldAndArgs GAsdaMapTravel(
	TEXT("Asda.Map.Travel"),
	TEXT("Asda.Map.Travel — состояние межкартового автопути.\n")
	TEXT("Asda.Map.Travel <карта> <x> <y> — взвести заявку (эталон beginMoveToTile@0xFBE140).\n")
	TEXT("Asda.Map.Travel cancel — снять заявку."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
			if (!MM) { UE_LOG(LogAsdaMinimap, Warning, TEXT("[автопуть] подсистема недоступна")); return; }
			if (Args.Num() >= 1 && Args[0].Equals(TEXT("cancel"), ESearchCase::IgnoreCase))
			{
				MM->CancelTravelRequest();
			}
			else if (Args.Num() >= 3)
			{
				MM->BeginMapTravel(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]), FCString::Atoi(*Args[2]));
			}
			UE_LOG(LogAsdaMinimap, Log, TEXT("%s"), *MM->DescribeMapTravel());
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // дев-команда, в Shipping не регистрируется
// ★ 18.10.2026 САМОТЕСТ «заявка автопути переживает портал»: взвести заявку → сброс «смена карты» (как PostLoadMap после
//   портала) → заявка ЖИВА; сброс «разрыв связи» → заявка СНЯТА. Регрессия К-3 (02.09): MapChanged снимал заявку,
//   и поход к цели задания не продолжался после перехода. Только офлайн (`-AsdaNoNet`), наружу ничего не шлёт.
static FAutoConsoleCommandWithWorldAndArgs GAsdaMapTravelResetTest(
	TEXT("asda.Map.TravelResetTest"),
	TEXT("asda.Map.TravelResetTest [quit] — заявка автопути переживает сброс «смена карты» и снимается «разрывом»"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			if (!GI) { AsdaSelfTestKit::Refused(World, TEXT("asda.Map.TravelResetTest"), KA.bQuit, TEXT("нет GameInstance")); return; }
			if (AsdaSelfTestKit::RefuseStart(GI, TEXT("asda.Map.TravelResetTest"), KA)) { return; }
			UAsdaMinimapSubsystem* MM = GI->GetSubsystem<UAsdaMinimapSubsystem>();
			UAsdaSessionResetSubsystem* RS = GI->GetSubsystem<UAsdaSessionResetSubsystem>();
			if (!MM || !RS) { AsdaSelfTestKit::Refused(World, TEXT("asda.Map.TravelResetTest"), KA.bQuit, TEXT("нет подсистемы карты/сброса")); return; }
			int32 Ok = 0, Fail = 0;
			auto Check = [&](bool b, const TCHAR* What)
			{
				(b ? Ok : Fail)++;
				UE_LOG(LogAsdaMinimap, Log, TEXT("[автопуть-тест] %s: %s"), What, b ? TEXT("ok") : TEXT("ПРОВАЛ"));
			};
			MM->BeginMapTravel(1, 100, 100);
			Check(MM->IsTravelRequested(), TEXT("заявка взведена"));
			RS->Broadcast(EAsdaSessionResetReason::MapChanged);
			Check(MM->IsTravelRequested(), TEXT("после сброса «смена карты» заявка жива"));
			RS->Broadcast(EAsdaSessionResetReason::Disconnected);
			Check(!MM->IsTravelRequested(), TEXT("после сброса «разрыв связи» заявка снята"));
			MM->CancelTravelRequest();
			AsdaSelfTestKit::Finish(World, TEXT("asda.Map.TravelResetTest"), KA.bQuit, LogAsdaMinimap,
				TEXT("[автопуть-тест] asda.Map.TravelResetTest"), Ok, Fail);
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMinimapBig(
	TEXT("Asda.Minimap.BigMap"),
	TEXT("Asda.Minimap.BigMap <0|1> — закрыть/открыть большую карту (1710 MAXMAP_WIN)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
			if (!MM) { return; }
			MM->SetBigMapVisible(Args.Num() >= 1 ? FCString::Atoi(*Args[0]) != 0 : true);
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★ Доказательство вращения конуса живьём: повернуть камеру ПКМ и вызвать команду ещё раз —
//   «орбит-камера» и оба «конуса» обязаны измениться синхронно, «курс персонажа» — нет.
static FAutoConsoleCommandWithWorld GAsdaMinimapPov(
	TEXT("Asda.Minimap.Pov"),
	TEXT("Углы конуса обзора: орбит-камера (g_fCameraYaw) / стрела камеры / курс персонажа "
	     "+ угол на квадах miniMyDir мини-карты и карты мира."),
	FConsoleCommandWithWorldDelegate::CreateStatic(
		[](UWorld* World)
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
			if (!MM) { UE_LOG(LogAsdaMinimap, Warning, TEXT("[миникарта] подсистема недоступна")); return; }
			UE_LOG(LogAsdaMinimap, Log, TEXT("%s"), *MM->DescribePov());
		}));
#endif // ASDA_DEV_CONSOLE
