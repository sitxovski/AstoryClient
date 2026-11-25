// Copyright (c) 2026 @sitxovski (Discord)
#include "World/AsdaZoneSubsystem.h"
#include "AsdaPerf.h"   // R4, доля a: области замера времени игрового потока
#include "World/AsdaLocalGrid.h"   // ★ R5-fix: сетка проходимости локального игрока (одна копия на миникарту и зоны)
#include "UI/AsdaUiVisibilitySubsystem.h"   // ★ 21.10.2026: Alt+Z — флаг «интерфейс скрыт» (владелец)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "World/AsdaCoord.h"   // ★ N11 (В1 дедупа №2): тайловая сетка — одна точка
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса

#include "World/AsdaWalkGrid.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaMapTable.h"
#include "World/AsdaMapRegistry.h"   // ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (ToAssetName)
#include "Resources/AsdaSoundSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/AudioComponent.h"     // глушение легаси-BGM, запечённого в уровень
#include "Sound/AmbientSound.h"            // AAmbientSound «MapBGM» из снятого с вооружения asda_set_map_bgm.py
#include "Sound/SoundBase.h"
#include "EngineUtils.h"                   // TActorIterator
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"   // ★ 30.10.2026: павн контроллера — офлайн-фолбэк IsLocalPlayerInPeaceZoneNow
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#if WITH_EDITOR
#include "TextureCompiler.h"               // дождаться асинхронной сборки текстуры (иначе плейсхолдер)
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaZone, Log, All);

// ★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА — ОСОЗНАННОЕ, ПО ПРОСЬБЕ ПОЛЬЗОВАТЕЛЯ (2026-08-01).
//   В ОРИГИНАЛЕ баннер названия локации на ходу показывается ТОЛЬКО при входе В ГОРОД. Доказано
//   дизасмом mapUI_DrawMapEnterBanner@0xDA2EF0 (client/system/AsdaStory.exe, ImageBase 0x400000):
//       0xDA2FCE  and eax, 0x80          ; флаги зоны
//       0xDA2FD3  je  0xDA3240           ; НЕ город → в хвост
//       0xDA3240  mov [eax+0x178AC], ecx ; ветка «не город» ЦЕЛИКОМ: только обновление кеша зоны
//       0xDA324F  ret                    ;   …и выход. Ничего не рисуется.
//   Плюс показ там всегда берёт ГОРОДСКОЙ вариант текстуры (0xDA2FFE `push 0`).
//   Пользователь попросил показывать надпись и при ВЫХОДЕ из города в поле (с обычной текстурой).
//   Переключатель ниже возвращает оригинальное поведение: asda.Zone.BannerOnLeaveTown 0.
static ASDA_DEV_CVAR(int32, CVarZoneBannerOnLeaveTown,
	TEXT("asda.Zone.BannerOnLeaveTown"),
	1,
	TEXT("Баннер названия локации при ВЫХОДЕ из города в поле.\n")
	TEXT("  1 (по умолчанию) — показывать, симметрично входу в город (ОТСТУПЛЕНИЕ от оригинала, по просьбе пользователя);\n")
	TEXT("  0 — как в оригинале: на ходу баннер только при входе в город (mapUI_DrawMapEnterBanner@0xDA2EF0)."),
	ECVF_Default);

// Штатный JUI-контрол 9000 «VILLAGE_NAME» (gamemain) в виде сгенерированного Widget Blueprint.
static const TCHAR* kBannerWidgetPath = TEXT("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_VILLAGE_NAME.WBP_VILLAGE_NAME_C");
// Текстуры названий локаций — Data/NPC/Image (эталон TextureCache_GetOrReload("Data/NPC/Image", name)).
static const TCHAR* kNameTexDirs[] = {   // ★ волна 2 п. 7: корни — реестр путей
	AsdaAssetPaths::kNpcImageRoot,
	AsdaAssetPaths::kNpcPortraitRoot,
};

void UAsdaZoneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAsdaZoneSubsystem::Deinitialize()
{
	HideBanner();
	BannerWidget = nullptr;
	BannerImage  = nullptr;
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaZoneSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// ⚠ Кэш зоны уже пересчитывается САМ: `Tick` сверяет `LM->CurrentMapId` с `ZoneMapId` и зовёт
	//   `ResetForMap`. Здесь только гасим баннер названия локации — его виджет принадлежит
	//   умершему миру, и «висящий» показ перенёсся бы на следующую карту.
	HideBanner();
	BannerPhase = 0;
	BannerPhaseT = 0.f;
	bBannerAwaitingLayout = false;
	BannerLayoutTries = 0;
	if (AsdaIsFullSessionReset(Reason))
	{
		CachedZoneFlags = kZoneUnset;
		ZoneMapId = -1;
		LastTileX = INT32_MIN; LastTileZ = INT32_MIN;
		bPendingFirstBanner = false;
		bPendingFirstTown = false;
	}
}

void UAsdaZoneSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.Zone");   // R4, доля a: область замера (AsdaPerf.h; в Shipping пусто)
	TickBanner(DeltaTime);
	TickZone(DeltaTime);
	// Отложенный баннер «вход на карту»: показать, как только снят загрузочный экран.
	if (bPendingFirstBanner)
	{
		UGameInstance* GI = GetGameInstance();
		const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
		if (!LM || !LM->IsLoadingScreenVisible())
		{
			bPendingFirstBanner = false;
			ShowLocationBanner(bPendingFirstTown);
		}
	}
}

// ── Сброс состояния при входе на новую карту (эталон: g_CachedZoneAttribFlags = 100 в конце загрузки) ──
void UAsdaZoneSubsystem::ResetForMap(int32 MapId)
{
	ZoneMapId       = MapId;
	CachedZoneFlags = kZoneUnset;
	LastTileX = LastTileZ = INT32_MIN;
	BannerGuard = 0.f;
	bPendingFirstBanner = false;
	bPendingFirstTown   = false;
	HideBanner();
	// Виджет живёт в вьюпорте мира — после травела он снимается, ссылку сбрасываем.
	BannerWidget = nullptr;
	BannerImage  = nullptr;
	// ★ Один поток BGM: снести «MapBGM», если уровень ещё несёт запечённый авто-BGM (см. ниже).
	SilenceLegacyLevelBgm();
}

// ── ★ Единственность потока BGM: убрать запечённый в уровень авто-проигрываемый BGM ──────────────
//   ИСТОРИЯ БАГА: снятый с вооружения Tools/asda_set_map_bgm.py клал в КАЖДЫЙ уровень актор
//   AAmbientSound с меткой «MapBGM» (auto_activate=1, looping, 2D, без затухания) и звуком из
//   /Game/Audio/BGM. Он стартовал сам при загрузке уровня — ПАРАЛЛЕЛЬНО зонному треку, отсюда
//   «два трека одновременно» (напр. в поле alpen: запечённый S_BGM_AlpeonTown + зонный
//   S_BGM_AlpeonField). Эталон держит РОВНО ОДИН стрим (CSoundStream: StreamStop → StreamPlay),
//   поэтому актор удаляется из уровней (Tools/asda_remove_legacy_map_bgm.py), а это — рантайм-
//   страховка на случай неперезапечённого уровня.
void UAsdaZoneSubsystem::SilenceLegacyLevelBgm()
{
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World) { return; }
	int32 Killed = 0;
	for (TActorIterator<AAmbientSound> It(World); It; ++It)
	{
		AAmbientSound* A = *It;
		if (!IsValid(A)) { continue; }
		UAudioComponent* AC = A->GetAudioComponent();
		USoundBase* S = AC ? AC->Sound : nullptr;
		if (!S) { continue; }
		const FString Path = S->GetPathName();
		const FString Name = S->GetName();
		// Только музыкальные слои: папка легаси-BGM либо имя трека S_BGM_*. 3D-эмиттеры карты
		//   (Pos.POS, S_OBJ_*/S_c_*) запечены в уровень акторами `MapSound_*` (`Tools/asda_place_sounds.py`) — их не трогаем;
		//   громкость эффектов им ставит `UAsdaSoundSubsystem` (★ 05.11.2026, ключ `asda.Sound.LevelAmbientSlider`).
		if (!Path.StartsWith(TEXT("/Game/Audio/BGM/")) && !Name.StartsWith(TEXT("S_BGM_"))) { continue; }
		UE_LOG(LogAsdaZone, Warning,
			TEXT("[зона] в уровне остался запечённый BGM-актор '%s' (звук %s) — глушу; перезапеки уровень Tools/asda_remove_legacy_map_bgm.py"),
			*A->GetName(), *Path);
		AC->Stop();
		A->Destroy();
		++Killed;
	}
	if (Killed > 0)
	{
		UE_LOG(LogAsdaZone, Warning, TEXT("[зона] заглушено запечённых BGM-акторов уровня: %d"), Killed);
	}
}

const UAsdaWalkGrid* UAsdaZoneSubsystem::GetPlayerGrid(AAsdaPlayerCharacter*& OutPlayer) const
{
	// ★ R5-fix (13.11.2026, чистка): тело дословно повторяло `UAsdaMinimapSubsystem::GetGrid` — общая `AsdaLocalGrid::Get`.
	return AsdaLocalGrid::Get(GetGameInstance(), OutPlayer);
}

// ★ 30.10.2026 [ОРИГИНАЛ]: признак зоны в момент решения — та же формула клетки, что в `TickZone` (обоснование — в заголовке).
bool UAsdaZoneSubsystem::IsLocalPlayerInPeaceZoneNow() const
{
	AAsdaPlayerCharacter* Player = nullptr;
	const UAsdaWalkGrid* Grid = GetPlayerGrid(Player);
	if (!Player)
	{
		// Офлайн-мир (без 4003): свой павн в реестр мира не записан — павн контроллера (как `UAsdaFishingSubsystem::SelfChar`).
		UGameInstance* GI = GetGameInstance();
		APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
		Player = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
		Grid = Player ? Player->WalkGrid.Get() : nullptr;
	}
	if (!Player || !Grid) { return IsInPeaceZone(); }
	const FIntPoint Tile = AsdaCoord::GameToTile(Player->GetGamePosXZ());
	return Grid->IsTilePeaceZone(Tile.X, Tile.Y);
}

void UAsdaZoneSubsystem::TickZone(float /*DeltaTime*/)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
	if (!LM || LM->CurrentMapId < 0) { return; }

	if (LM->CurrentMapId != ZoneMapId) { ResetForMap(LM->CurrentMapId); }

	AAsdaPlayerCharacter* Player = nullptr;
	const UAsdaWalkGrid* Grid = GetPlayerGrid(Player);
	if (!Player || !Grid) { return; }   // ждём спавна павна и загрузки сетки карты

	// Эталон пересчитывает зону в апдейте позиции — при СМЕНЕ клетки (100 игровых единиц).
	const FVector2D G = Player->GetGamePosXZ();
	const FIntPoint Tile = AsdaCoord::GameToTile(G);   // ★ N11: та же формула (FloorToInt(x / 100)), одна точка
	const int32 Tx = Tile.X;
	const int32 Tz = Tile.Y;
	if (Tx == LastTileX && Tz == LastTileZ) { return; }
	LastTileX = Tx; LastTileZ = Tz;

	const int32 Flags = Grid->IsTilePeaceZone(Tx, Tz) ? 0x80 : 0x00;
	if (Flags == CachedZoneFlags) { return; }

	const bool  bTown  = (Flags & 0x80) != 0;
	const bool  bFirst = (CachedZoneFlags == kZoneUnset);   // первый расчёт на карте = конец её загрузки
	const int32 Prev   = CachedZoneFlags;
	CachedZoneFlags = Flags;

	const FName Bgm = ResolveBgm(ZoneMapId, bTown);
	UAsdaSoundSubsystem* Snd = GI->GetSubsystem<UAsdaSoundSubsystem>();
	if (Snd)
	{
		if (bFirst)
		{
			// ResetForMap мог сработать ещё на уровне загрузки (CurrentMapId ставится до OpenLevel) —
			//   повторяем проверку уже в целевом мире, чтобы страховка «один поток BGM» точно отработала.
			SilenceLegacyLevelBgm();
			// game_main.c:3404-3418 — мгновенный стоп + старт БЕЗ фейда (a3=0).
			Snd->StopMusic(0.f);
			if (!Bgm.IsNone()) { Snd->PlayMusic(Bgm, /*bLoop*/true, /*FadeSec*/0.f); }
		}
		else
		{
			// entity_char.c:20988-20999 — cSoundSet_QueueOrSetByIndex(dev, idx, 16, 16): фейд-очередь.
			if (!Bgm.IsNone()) { Snd->QueueMusic(Bgm, kBgmFadeSec, kBgmFadeSec); }
			else               { Snd->StopMusic(kBgmFadeSec); }
		}
	}

	// Баннер названия локации:
	//   • конец загрузки карты — npcwin_DrawMapNpcImage: показывается ВСЕГДА, вариант по зоне;
	//     ★ ждём снятия загрузочного экрана (он полноэкранный, ZOrder 30000): у нас первый расчёт
	//       зоны происходит по спавну павна, т.е. может опередить RevealWorld на несколько кадров,
	//       и первые кадры проявления баннера ушли бы под картинку загрузки;
	//   • на ходу — mapUI_DrawMapEnterBanner: ТОЛЬКО при входе в город и не чаще 8 с
	//     (свой таймер 0x1F40; вход на карту его НЕ взводит — эталон дёргает баннер напрямую).
	const bool bEnteredTown = bTown  && ((Prev & 0x80) == 0) && !bFirst;
	// ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (см. шапку файла, CVar asda.Zone.BannerOnLeaveTown): выход город→поле.
	//   В оригинале показа здесь НЕТ; включено по просьбе пользователя, текстура — обычная (не «_town»),
	//   анимация та же (2 с проявление → 7 с затухание), гард ОБЩИЙ на оба направления (8000 мс),
	//   чтобы ходьба по границе города не спамила баннером.
	const bool bLeftTown    = !bTown && ((Prev & 0x80) != 0) && !bFirst
	                          && CVarZoneBannerOnLeaveTown.GetValueOnGameThread() != 0;
	if (bFirst)
	{
		bPendingFirstBanner = true;
		bPendingFirstTown   = bTown;
	}
	bool bBannerShownNow = false;   // факт показа именно сейчас (гард мог его отменить) — для лога
	if (!bFirst && (bEnteredTown || bLeftTown) && BannerGuard <= 0.f)
	{
		ShowLocationBanner(bTown);   // bTown=false → ResolveNameTexture вернёт обычную NameTex
		BannerGuard = kBannerGuardSec;
		bBannerShownNow = true;
	}

	// Хвост строки поясняет судьбу баннера при выходе в поле: показ там — наше отступление от эталона,
	//   выключаемое CVar asda.Zone.BannerOnLeaveTown 0 (тогда поведение оригинала: показа нет).
	const TCHAR* Note = TEXT("");
	if (!bFirst && !bTown)
	{
		if (bBannerShownNow)  { Note = TEXT(" (баннер показан — ОТСТУПЛЕНИЕ от оригинала, asda.Zone.BannerOnLeaveTown=1)"); }
		else if (bLeftTown)   { Note = TEXT(" (баннер пропущен — гард 8 с)"); }
		else                  { Note = TEXT(" (баннер не показан — как в оригинале, mapUI_DrawMapEnterBanner@0xDA2EF0)"); }
	}
	UE_LOG(LogAsdaZone, Log, TEXT("[зона] %s: карта=%d(%s) клетка=(%d,%d) BGM=%s название=%s%s"),
		bFirst ? TEXT("вход на карту") : (bTown ? TEXT("вход в город") : TEXT("выход в поле")),
		ZoneMapId, *LM->GetMapDirName(ZoneMapId), Tx, Tz,
		Bgm.IsNone() ? TEXT("—(слот карты пуст)") : *Bgm.ToString(),
		*ResolveNameTexture(ZoneMapId, bTown), Note);
}

// ── Данные карты из DT_MapTable ──────────────────────────────────────────────────────────────────

FName UAsdaZoneSubsystem::ResolveBgm(int32 MapId, bool bTown) const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const FAsdaMapRow* Row = LM ? LM->GetMapRow(MapId) : nullptr;
	if (!Row) { return NAME_None; }
	const FString& S = bTown ? Row->BgmTown : Row->BgmField;
	// Пусто = соответствующий слот в заголовке .TER не заполнен (флаг 0x2F70/0x2F74 == 0) → тишина.
	return S.IsEmpty() ? NAME_None : FName(*S);
}

FString UAsdaZoneSubsystem::ResolveNameTexture(int32 MapId, bool bTown) const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const FAsdaMapRow* Row = LM ? LM->GetMapRow(MapId) : nullptr;
	if (!Row) { return FString(); }
	// Свитч Map_SetNameTextureByID для карт без городского варианта возвращает ТУ ЖЕ строку → фолбэк.
	if (bTown && !Row->NameTexTown.IsEmpty()) { return Row->NameTexTown; }
	return Row->NameTex;
}

UTexture2D* UAsdaZoneSubsystem::LoadNameTexture(const FString& BaseName) const
{
	if (BaseName.IsEmpty()) { return nullptr; }
	// Имена в оригинале содержат пробелы ("name_Mountain Flamion") — в UE это подчёркивание.
	const FString Asset = AsdaMapRegistry::ToAssetName(BaseName);   // ★ волна 2: одна точка приведения имени
	for (const TCHAR* Dir : kNameTexDirs)
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), Dir, *Asset, *Asset);
		if (UTexture2D* T = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return T;
		}
	}
	UE_LOG(LogAsdaZone, Warning, TEXT("[зона] текстура названия '%s' не найдена"), *Asset);
	return nullptr;
}

// ── Баннер названия локации ──────────────────────────────────────────────────────────────────────

bool UAsdaZoneSubsystem::EnsureBannerWidget()
{
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World) { return false; }
	if (BannerWidget && BannerWidget->IsInViewport() && BannerImage) { return true; }

	BannerWidget = nullptr;
	BannerImage  = nullptr;

	UClass* Cls = LoadClass<UUserWidget>(nullptr, kBannerWidgetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Cls)
	{
		UE_LOG(LogAsdaZone, Warning, TEXT("[зона] нет виджета JUI 9000 VILLAGE_NAME: %s"), kBannerWidgetPath);
		return false;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) { return false; }
	UUserWidget* W = CreateWidget<UUserWidget>(PC, Cls);
	if (!W) { return false; }
	// AlwaysTop="1" у контрола 9000 — держим баннер поверх игрового HUD (но ниже загрузочного экрана,
	//   ZOrder 30000: он полноэкранный и снимается раньше, чем баннер показывается).
	W->AddToViewport(kBannerZOrder);
	// ★ 21.10.2026 (Alt+Z): прячется вместе с интерфейсом — у оригинала это окно JUI блока бита 128 (render_core.c:1482).
	UAsdaUiVisibilitySubsystem::TrackFor(this, W);
	BannerWidget = W;

	// Штатное дерево WBP_VILLAGE_NAME (проверено Tools/_probe_village_name_widget.py):
	//   RootCanvas → VILLAGE_NAME_0_0 (Image, план скина) + VILLAGE_NAME (TextBlock, подпись контрола).
	//   Подпись у контрола 9000 пустая (CaptionText="") — сворачиваем, рисуется только картинка.
	if (W->WidgetTree)
	{
		BannerImage = Cast<UImage>(W->WidgetTree->FindWidget(TEXT("VILLAGE_NAME_0_0")));
		if (!BannerImage) { BannerImage = Cast<UImage>(W->WidgetTree->FindWidget(TEXT("VILLAGE_NAME_0"))); }
		W->WidgetTree->ForEachWidget([this](UWidget* Child)
		{
			if (!BannerImage) { BannerImage = Cast<UImage>(Child); }
			if (Child && Child->IsA<UTextBlock>()) { Child->SetVisibility(ESlateVisibility::Collapsed); }
		});
	}
	if (!BannerImage)
	{
		UE_LOG(LogAsdaZone, Warning, TEXT("[зона] в WBP_VILLAGE_NAME не найден UImage"));
		return false;
	}
	return true;
}

void UAsdaZoneSubsystem::ShowLocationBanner(bool bTownVariant)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const int32 MapId = LM ? LM->CurrentMapId : ZoneMapId;

	const FString TexName = ResolveNameTexture(MapId, bTownVariant);
	UTexture2D* Tex = LoadNameTexture(TexName);
	if (!Tex)
	{
		UE_LOG(LogAsdaZone, Warning, TEXT("[зона] баннер: карта=%d текстура=%s виджет=НЕ ПОКАЗАН (текстура не загрузилась)"),
			MapId, TexName.IsEmpty() ? TEXT("—") : *TexName);
		return;
	}
	if (!EnsureBannerWidget())
	{
		UE_LOG(LogAsdaZone, Warning, TEXT("[зона] баннер: карта=%d текстура=%s виджет=НЕТ (WBP_VILLAGE_NAME не создан)"),
			MapId, *TexName);
		return;
	}

#if WITH_EDITOR
	// ★ ПРИЧИНА «первый показ мелкий и битый»: пока текстура собирается асинхронно, движок подсовывает
	//   ПЛЕЙСХОЛДЕР — UTexture::IsCompiling() в Engine/Classes/Engine/Texture.h:2045 буквально
	//   `return IsDefaultTexture();`. GetSizeX/GetSizeY в этот момент отдают размер плейсхолдера,
	//   и баннер раскладывается по мусорным числам (а рисуется дефолтная текстура — «изуродован»).
	//   Со второго показа текстура уже собрана, отсюда «второй раз хорошо». Ждём сборки явно.
	if (Tex->IsCompiling())
	{
		FTextureCompilingManager::Get().FinishCompilation({ Tex });
	}
#endif
	// Мипов у name_*.TEX нет и стриминг выключен (Tools/asda_fix_name_textures_pixelexact.py),
	//   но на всякий случай гарантируем резидентность полного разрешения до первого кадра.
	Tex->WaitForStreaming();

	// ★ ЛОГИЧЕСКИЙ размер: баннер рисуется в пикселях исходной картинки, иначе после апскейла
	//   он занял бы вдвое больше экрана.
	const FIntPoint LogSize = AsdaUi::LogicalAtlasSize(Tex);
	const FVector2D Size((float)LogSize.X, (float)LogSize.Y);
	if (Size.X < 1.f || Size.Y < 1.f)
	{
		UE_LOG(LogAsdaZone, Warning, TEXT("[зона] баннер: карта=%d текстура=%s размер=%.0fx%.0f — не показываю"),
			MapId, *TexName, Size.X, Size.Y);
		return;
	}
	BannerTexSize = Size;

	// Кисть = вся текстура целиком (эталон CControl_DrawTextureRect(ctrl, tex, 0,0, texW, texH)).
	// ★ ГРАБЛИ (причина невидимого баннера): SetUVRegion(FBox2f(0,0)) НЕ «отключает» UV — конструктор
	//   FBox2f(Min,Max) ставит bIsValid=true, а FSlateElementBatcher при валидном регионе берёт
	//   SizeUV = BrushUV.GetSize() = (0,0) → все 4 вершины квада читают ОДИН тексель (левый верхний,
	//   у name_*.TEX он полностью прозрачный) → баннер рисуется, но невидим. Полная текстура =
	//   НЕ трогать UVRegion вовсе (дефолт FBox2f(ForceInit) имеет bIsValid=false).
	FSlateBrush Brush;
	Brush.SetResourceObject(Tex);
	Brush.ImageSize = Size;
	Brush.DrawAs    = ESlateBrushDrawType::Image;
	Brush.TintColor = FSlateColor(FLinearColor::White);
	BannerImage->SetBrush(Brush);
	BannerImage->SetColorAndOpacity(FLinearColor::White);
	BannerImage->SetVisibility(ESlateVisibility::HitTestInvisible);

	// Раскладка (см. ApplyBannerLayout — эталонные целые пиксели).
	++BannerShowCount;
	BannerLayoutTries = 0;
	bBannerAwaitingLayout = true;      // показываем ТОЛЬКО когда компоновка Slate дала верные размеры
	BannerPhase  = 0;
	BannerPhaseT = 0.f;
	BannerWidget->SetRenderOpacity(0.f);
	// Hidden (НЕ Collapsed): виджет участвует в компоновке, но ни одного кадра не видно, пока
	//   геометрия не совпадёт с размером текстуры — иначе первый кадр уходит на экран «мелким».
	BannerWidget->SetVisibility(ESlateVisibility::Hidden);
	ApplyBannerLayout();

	UE_LOG(LogAsdaZone, Log,
		TEXT("[зона] баннер показ #%d: карта=%d(%s) текстура=%s(%dx%d) виджет=создан ожидание компоновки, %s"),
		BannerShowCount, MapId, LM ? *LM->GetMapDirName(MapId) : TEXT("?"), *TexName,
		Tex->GetSizeX(), Tex->GetSizeY(), *DescribeBanner());
}

// ── Раскладка баннера: ПИКСЕЛЬ-В-ПИКСЕЛЬ, как в оригинале ────────────────────────────────────────
//   Эталон npcwin_DrawMapNpcImage@0xDA3330 (npc_mob_pet.c:3696):
//       CControl_DrawTextureRect(ctrl, name, 0, 0, texW, texH);
//       (vtbl+20)(ctrl, texW, texH);                       // размер контрола ЯВНО из ТЕКСТУРЫ
//       x = (int)((double)g_ScreenW * 0.5  − (double)(w / 2));   // w/2 — ЦЕЛОЧИСЛЕННОЕ деление,
//       y = (int)((double)g_ScreenH * 0.25 − (double)(h / 2));   //   результат кладётся в int
//       uiContainerConfigItemAndShow(ctrl, x, y, w, h, 100.0f, 100.0f, ...);   // масштаб 100→100 = 1:1
//   То есть координаты — ЦЕЛЫЕ ПИКСЕЛИ ЭКРАНА, картинка не масштабируется под разрешение вообще.
//   Поэтому здесь: анкор (0,0) + целочисленный offset (а НЕ анкор 0.5/0.25 с выравниванием 0.5 —
//   тот даёт дробную позицию при нечётной ширине вьюпорта и полупиксельный сдвиг/ресемплинг),
//   размер = размер текстуры, у проекта DPI-кривая константная 1.0 (1 slate-юнит = 1 пиксель),
//   но делим на фактический DPI и округляем, чтобы 1:1 держалось и при ином масштабе.
void UAsdaZoneSubsystem::ApplyBannerLayout()
{
	if (!BannerWidget || !BannerImage || BannerTexSize.X < 1.f) { return; }

	float Dpi = 1.f;
	FVector2D Vp(0.f, 0.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Vp);
		Dpi = GEngine->GameViewport->GetDPIScale();
	}
	if (Dpi <= 0.f) { Dpi = 1.f; }
	BannerVpSize = Vp;
	BannerDpi    = Dpi;

	// Размер в единицах Slate: столько, чтобы НА ЭКРАНЕ вышло ровно texW×texH ПИКСЕЛЕЙ.
	const FVector2D SizeSlate(BannerTexSize.X / Dpi, BannerTexSize.Y / Dpi);
	// Позиция — ровно формула эталона в пикселях экрана, с целочисленным w/2 и целым результатом.
	const int32 PxX = FMath::FloorToInt(Vp.X * 0.5f  - (float)((int32)BannerTexSize.X / 2));
	const int32 PxY = FMath::FloorToInt(Vp.Y * 0.25f - (float)((int32)BannerTexSize.Y / 2));
	const FVector2D PosSlate((float)PxX / Dpi, (float)PxY / Dpi);

	// Дерево WBP_VILLAGE_NAME плоское (RootCanvas → VILLAGE_NAME_0_0), но на случай группы —
	//   якорим верхний узел ветки, а картинку внутри растягиваем на него целиком.
	UWidget* Top = BannerImage;
	while (Top->GetParent() && Top->GetParent() != BannerWidget->WidgetTree->RootWidget)
	{
		Top = Top->GetParent();
	}
	if (UCanvasPanelSlot* TopSlot = AsdaUi::SlotOf(Top))
	{
		TopSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));   // левый верх — координаты в пикселях
		TopSlot->SetAlignment(FVector2D::ZeroVector);
		TopSlot->SetAutoSize(false);                          // размер НЕ из измерения виджета
		TopSlot->SetPosition(PosSlate);
		TopSlot->SetSize(SizeSlate);
		TopSlot->SetZOrder(100);
	}
	if (Top != BannerImage)
	{
		if (UCanvasPanelSlot* ImgSlot = AsdaUi::SlotOf(BannerImage))
		{
			ImgSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			ImgSlot->SetOffsets(FMargin(0.f));
		}
		Top->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	// Желаемый размер картинки задаём явно — чтобы он не зависел ни от измерения, ни от браша.
	BannerImage->SetDesiredSizeOverride(SizeSlate);
}

// Фактическая геометрия баннера уже после прохода компоновки Slate (для лога/консоли).
FString UAsdaZoneSubsystem::DescribeBanner() const
{
	if (!BannerWidget)
	{
		return TEXT("виджет=нет");
	}
	const FGeometry& G = BannerImage ? BannerImage->GetCachedGeometry() : BannerWidget->GetCachedGeometry();
	const FVector2D LocalSize = G.GetLocalSize();
	const FVector2D AbsPos    = FVector2D(G.GetAbsolutePosition());
	return FString::Printf(
		TEXT("виджет=создан вьюпорт=%s фаза=%d размер=%.0fx%.0f позиция=(%.0f,%.0f) альфа=%.2f видимость=%d"),
		BannerWidget->IsInViewport() ? TEXT("да") : TEXT("НЕТ"), BannerPhase,
		LocalSize.X, LocalSize.Y, AbsPos.X, AbsPos.Y,
		BannerWidget->GetRenderOpacity(), (int32)BannerWidget->GetVisibility());
}

void UAsdaZoneSubsystem::TickBanner(float DeltaTime)
{
	if (BannerGuard > 0.f) { BannerGuard = FMath::Max(0.f, BannerGuard - DeltaTime); }
	if (!BannerWidget) { return; }

	// ── Ожидание валидной компоновки перед ПЕРВЫМ видимым кадром ──
	//   Известные грабли проекта: первый рендер идёт до prepass Slate, измерения нулевые/неверные
	//   (лечили догоняющим Refresh на плашках над головой, changelog п.117). Здесь баннер висит
	//   Hidden и раскладка применяется каждый тик, пока фактическая геометрия не совпадёт с
	//   размером ТЕКСТУРЫ. Только тогда включаем видимость и стартуем проявление.
	if (bBannerAwaitingLayout)
	{
		ApplyBannerLayout();
		++BannerLayoutTries;
		const FVector2D L = BannerImage ? BannerImage->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
		const FVector2D Want(BannerTexSize.X / FMath::Max(BannerDpi, KINDA_SMALL_NUMBER),
		                     BannerTexSize.Y / FMath::Max(BannerDpi, KINDA_SMALL_NUMBER));
		const bool bValid = FMath::IsNearlyEqual(L.X, Want.X, 0.5f) && FMath::IsNearlyEqual(L.Y, Want.Y, 0.5f);
		if (!bValid && BannerLayoutTries < kBannerLayoutMaxTries) { return; }
		bBannerAwaitingLayout = false;
		BannerWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		BannerPhase  = 1;                 // проявление 0 → 255 за 2000 мс (objStartTimedAction(0,255,2000))
		BannerPhaseT = 0.f;
		const FString Line = FString::Printf(
			TEXT("[зона] баннер показ #%d СТАРТ (тиков ожидания=%d, геометрия %s): ожидалось размер=%.0fx%.0f позиция=(%d,%d) масштаб=%.2f вьюпорт=%.0fx%.0f | фактически %s"),
			BannerShowCount, BannerLayoutTries, bValid ? TEXT("совпала") : TEXT("НЕ совпала"),
			Want.X, Want.Y,
			FMath::FloorToInt(BannerVpSize.X * 0.5f  - (float)((int32)BannerTexSize.X / 2)),
			FMath::FloorToInt(BannerVpSize.Y * 0.25f - (float)((int32)BannerTexSize.Y / 2)),
			BannerDpi, BannerVpSize.X, BannerVpSize.Y, *DescribeBanner());
		if (bValid) { UE_LOG(LogAsdaZone, Log,     TEXT("%s"), *Line); }
		else        { UE_LOG(LogAsdaZone, Warning, TEXT("%s"), *Line); }
		return;
	}
	if (BannerPhase == 0) { return; }

	// Размер окна изменился на живом баннере — пересчитать целые пиксели (позиция от вьюпорта).
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D Vp(0.f, 0.f);
		GEngine->GameViewport->GetViewportSize(Vp);
		if (!Vp.Equals(BannerVpSize, 0.5)) { ApplyBannerLayout(); }
	}

	BannerPhaseT += DeltaTime;
	if (BannerPhase == 1)
	{
		const float A = FMath::Clamp(BannerPhaseT / kBannerFadeInSec, 0.f, 1.f);
		BannerWidget->SetRenderOpacity(A);
		if (BannerPhaseT >= kBannerFadeInSec)
		{
			BannerPhase = 2; BannerPhaseT = 0.f;   // uiWindowMsgHandler msg 24 / id 9000 / arg 3
		}
	}
	else
	{
		const float A = FMath::Clamp(1.f - BannerPhaseT / kBannerFadeOutSec, 0.f, 1.f);
		BannerWidget->SetRenderOpacity(A);
		if (BannerPhaseT >= kBannerFadeOutSec) { HideBanner(); }
	}
}

void UAsdaZoneSubsystem::HideBanner()
{
	BannerPhase = 0; BannerPhaseT = 0.f;
	bBannerAwaitingLayout = false;
	if (BannerWidget)
	{
		BannerWidget->SetRenderOpacity(0.f);
		BannerWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

// Принудительный показ (консоль): снимаем гард и отложенный показ, рисуем немедленно.
void UAsdaZoneSubsystem::ForceShowBanner(bool bTownVariant)
{
	BannerGuard = 0.f;
	bPendingFirstBanner = false;
	BannerPhase = 0; BannerPhaseT = 0.f;
	bBannerAwaitingLayout = false;
	ShowLocationBanner(bTownVariant);
	// Консольный показ должен быть виден сразу: прокручиваем ожидание компоновки принудительно,
	//   компоновка уже прошла (виджет живой), поэтому геометрия валидна на первом же прогоне.
	TickBanner(0.f);
}

FString UAsdaZoneSubsystem::DescribeState() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
	const int32 MapId = LM ? LM->CurrentMapId : -1;
	const bool bTown = IsInPeaceZone();
	return FString::Printf(
		TEXT("[зона] карта=%d(%s) клетка=(%d,%d) зона=%s BGM(играет)=%s BGM(город)=%s BGM(поле)=%s название=%s ")
		TEXT("название(город)=%s название(поле)=%s баннер-при-выходе=%d(%s) гард=%.1fс"),
		MapId, LM ? *LM->GetMapDirName(MapId) : TEXT("?"), LastTileX, LastTileZ,
		CachedZoneFlags == kZoneUnset ? TEXT("не определена") : (bTown ? TEXT("ГОРОД") : TEXT("поле")),
		Snd ? *Snd->GetMusicTrack().ToString() : TEXT("?"),
		*ResolveBgm(MapId, true).ToString(), *ResolveBgm(MapId, false).ToString(),
		*ResolveNameTexture(MapId, bTown),
		*ResolveNameTexture(MapId, true), *ResolveNameTexture(MapId, false),
		CVarZoneBannerOnLeaveTown.GetValueOnGameThread(),
		CVarZoneBannerOnLeaveTown.GetValueOnGameThread() != 0 ? TEXT("отступление от оригинала") : TEXT("как в оригинале"),
		BannerGuard);
}

// ── Консоль: проверка вживую ─────────────────────────────────────────────────────────────────────

static UAsdaZoneSubsystem* GetZoneSub(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaZoneSubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaZoneStatus(
	TEXT("Asda.Zone.Status"),
	TEXT("Состояние зоны: карта, клетка, город/поле, треки BGM обоих слотов, текстура названия."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UAsdaZoneSubsystem* Z = GetZoneSub(World)) { UE_LOG(LogAsdaZone, Display, TEXT("%s"), *Z->DescribeState()); }
		else { UE_LOG(LogAsdaZone, Warning, TEXT("[зона] подсистема недоступна")); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaZoneBanner(
	TEXT("Asda.Zone.Banner"),
	TEXT("Немедленно показать баннер названия локации + напечатать его состояние. Аргумент 1 = городской вариант, 0 = обычный."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		const bool bTown = Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : true;
		UAsdaZoneSubsystem* Z = GetZoneSub(World);
		if (!Z)
		{
			UE_LOG(LogAsdaZone, Warning, TEXT("[зона] подсистема недоступна"));
			return;
		}
		UE_LOG(LogAsdaZone, Display, TEXT("%s"), *Z->DescribeState());
		UE_LOG(LogAsdaZone, Display, TEXT("[зона] баннер ДО: %s"), *Z->DescribeBanner());
		Z->ForceShowBanner(bTown);   // сбрасывает гард 8 с и отложенный показ → рисуем сейчас
		UE_LOG(LogAsdaZone, Display, TEXT("[зона] баннер ПОСЛЕ: %s"), *Z->DescribeBanner());
	}));
#endif // ASDA_DEV_CONSOLE
