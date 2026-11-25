// Copyright (c) 2026 @sitxovski (Discord)
#include "World/AsdaLoadManagerSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "ContentStreaming.h"          // ★ IStreamingManager — прогрев мипов перед снятием экрана
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса
#include "World/AsdaGroundTrace.h"
#include "World/AsdaWalkGrid.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Styling/SlateBrush.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"   // ★ тестовый крюк Asda.Load.CurrentMap (см. конец файла)
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Math/RandomStream.h"
#include "UObject/UObjectGlobals.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "AsdaNetSubsystem.h"   // ★ B3: отправка 4001 по загрузке целевого уровня
#include "Text/AsdaTextSubsystem.h"   // ★ Русские имена карт: локтаблица Map_info (общий источник с мини-картой/квестлогом)
#include "World/AsdaMapRegistry.h"   // ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (путь DA_WalkGrid)
#include "Engine/AssetManager.h"          // ★ load_hitch: FStreamableManager — асинхронная предзагрузка «первого раза»
#include "Engine/StreamableManager.h"
#include "PipelineStateCache.h"           // ★ load_hitch: PipelineStateCache::NumActivePrecacheRequests (прекэш PSO)
#include "RenderingThread.h"              // ★ load_hitch: FlushRenderingCommands перед ожиданием прекэша PSO
#include "World/AsdaPlayerCharacter.h"    // ★ load_hitch: AsdaCharPreviewBase — общая точка базы превью-скелета
#include "World/AsdaWorldSubsystem.h"     // ★ load_hitch: kAsdaEntryFxJeb / kAsdaEntrySound — эффект и звук появления
#include "UI/AsdaInventorySubsystem.h"    // ★ load_hitch: путь класса окна инвентаря
#include "UI/AsdaWindowSubsystem.h"       // ★ волна В6 ремесла: звуки 5 менеджера сообщений на шаге загрузки (game_main.c:3547)
#include "Characters/AsdaCharAssembly.h"  // ★ charsel_preload: общие точки путей частей (кисти)
#include "Engine/SkeletalMesh.h"          // ★ charsel_preload: материалы мешей → их текстуры (заказ полных мипов)
#include "Engine/StaticMesh.h"
#include "Engine/StreamableRenderAsset.h"
#include "Resources/AsdaTextureSubsystem.h" // ★ charsel_preload: единый реестр владения флагом полных мипов
#include "Text/AsdaLang.h"                   // [ПОРТ] 09.10.2026 (мультиязычность): запасное имя карты на языке сессии
#include "Blueprint/GameViewportSubsystem.h"  // ★ 31.10.2026 затемнение: виджет экрана, который травел не снимает
#include "Framework/Application/SlateApplication.h"   // ★ 31.10.2026 затемнение: TakeScreenshot — застывший кадр мира
#include "Widgets/SWindow.h"
#include "Camera/PlayerCameraManager.h"       // ★ 31.10.2026 затемнение: камера смотрит на персонажа
#include "Containers/Ticker.h"                // ★ 31.10.2026 затемнение: шаг по тикеру ядра

DEFINE_LOG_CATEGORY_STATIC(LogAsdaLoad, Log, All);

// Путь DataTable-реестра карт (создаётся headless из дампа g_aMapDirNames).
static const TCHAR* GMapTablePath = TEXT("/Game/AsdaAssets/Data/DT_MapTable.DT_MapTable");
// Пустой concrete-виджет (наполняется в рантайме, как WBP_ResPicker).
static const TCHAR* GLoadWidgetClass = TEXT("/Game/AsdaAssets/UI/Dev/WBP_LoadingScreen.WBP_LoadingScreen_C");
// Реверс CMap_LoadLoadingScreen: текстуры карт лежат в Textures/Maps/<dir>/load (пер-карта, инлайн-путь).
// ★★★ ЕДИНСТВЕННЫЙ фолбэк оригинала при пустой папке карты — `map\load\start_logo.TEX`
//   (`CMap_LoadLoadingScreen@0x975650`, `RE/etalon/world_map.c:212-218` и :155-160). Имя ассета
//   в порту — с заглавной «S», как сам файл клиента (`map\load\Start_logo.TEX`).
static const TCHAR* GTexStartLogo = AsdaLoadScreen::kStartLogoTex;   // ★ В1-C F16: одна строка с заставкой логина
// Выделенный УРОВЕНЬ загрузки: переход GO→L_Loading выгружает login; на L_Loading грузим целевую карту.
static const TCHAR* GLoadingLevelName = TEXT("L_Loading");

// ══════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ ЭКРАННОЕ ЗАТЕМНЕНИЕ СМЕНЫ КАРТЫ (31.10.2026, баг 5 игроков и владельца)
// ══════════════════════════════════════════════════════════════════════════════════════════════════
//  ЖАЛОБА. Загрузочный экран появляется и исчезает обрывом; после загрузки на секунду видно небо без земли.
//
//  ОРИГИНАЛ (эталон, перепроверено по коду). Затемнение — объект `g_nMainLoopState` (он же держит картинку загрузки):
//    кадровый шаг `uiColorFadeUpdate@0x97A020` (ui_core.c:1349-1436) зовётся в конце `CRenderer_RenderFrame` /
//    `CRenderer_RenderWorldFrame` (render_core.c:837, :1575 — ПОСЛЕ окон, т.е. поверх всего UI). Поле +3 — время режима,
//    мс (`sub_97A4A0` прибавляет кадровую дельту `getZ(g_dwDisplayOptionFlags)` = мс кадра, `timerUpdateHighRes`);
//    не 0 = режим идёт (`setScaleOne@0x9666A0` пишет 1.0 = старт). Поле +9 — режим (`set_o9@0x9666D0`). Длительность
//    ВСЕХ режимов — 1500 мс, кривая линейная, по окончании +3 = 0, +9 = 5:
//      режим 0: чёрный квад, альфа 1 − t/1500 (`uiDrawFullscreenTexturedQuad`, цвет alpha<<24);
//      режим 3: картинка загрузки (текстура +4, UV V 0..0.75, `fxRenderTexturedFullscreenQuad@0x97B250`), альфа t/1500;
//      режим 4: та же картинка, альфа 1 − t/1500.
//    Режимы 1/2 в клиенте не включаются никем (вызовов `set_o9(…,1|2)` нет).
//    Кто включает: режим 3 — 6004 и 6203 (`Net_RecvPacketDispatch_Loop` network.c:739/1279), своё 4055
//      (`netHandler_EnterWorld@0xF43050` network.c:42680), порталы/каналы (`Recv_PortalChannelChange@0xF45BD0`,
//      `netHandleChannelPortal@0xF468A0`, `netHandleChannelTransfer@0xF49660`, `netPortalChannelTransfer@0xF49EE0`,
//      `portalHandleChannelWarp@0xF464C0`, `portalChannelTransferBegin@0xF49C40`); режим 4 — конец загрузки, переход
//      в g_GameState=1 (`MainLoop_Tick_SoundUpdate` game_main.c:3431 и :3850) и старт (`System_InitAll` network.c:3303);
//      режим 0 — своё 4056 (`GameNet_OnLocalPlayerMapEntry@0xF43390` game_main.c:12962) и 6025
//      (`char_onLocalPlayerMapEntry@0xF49090` entity_char.c:53205).
//    Пока идёт загрузка (state 11), кадр рисует только `uiRenderLoadingBar` — шаг затемнения не идёт; экран снимается
//      лишь когда шаги 11..13+N догрузили патчи вокруг игрока (`CMap_StreamVisiblePatches` в цикле до готовности,
//      game_main.c:3201-3208) и камера поставлена на персонажа (`cameraUpdate` на шаге 11). Затем режим 4 растворяет
//      картинку над уже готовым миром.
//
//  ПОРТ ДО ПРАВКИ. Затемнения не было вовсе (картинка ставилась и снималась за кадр). Хуже того, виджет экрана
//    (владелец — GameInstance) движок снимал САМ при травеле L_Loading → целевая карта (`UGameViewportSubsystem::
//    HandleRemoveWorld`, `bAutoRemoveOnWorldRemoved`): онлайн до прихода 4003 (журнал 28.10 17:05:58.881 → 59.524,
//    ≈0.6 с) игрок видел новую карту БЕЗ персонажа, камерой контроллера по умолчанию — «небо без земли».
//
//  СТАЛО (ключи ниже, 0 = прежнее поведение):
//    [ИСПРАВЛЕНИЕ ПОРТА] `asda.Load.KeepScreenOnTravel` — экран ставится во вьюпорт с bAutoRemoveOnWorldRemoved=false и
//      висит до 4003, как в оригинале до конца загрузки.
//    [ОРИГИНАЛ] `asda.Load.FadeSec` (1.5 = 1500 мс эталона): режим 4 — картинка растворяется над миром; режим 0 — чёрный
//      рассеивается на своё 4056/6025; режим 3 — картинка проявляется.
//    [ИСПРАВЛЕНИЕ] режим 3 стартует в BeginMapLoad (момент, когда порт покидает мир) над ЗАСТЫВШИМ последним кадром: травел
//      на L_Loading уничтожает мир сразу (откладывать его после 6004 нельзя — пакеты нового канала), и без кадра-подложки
//      проявление шло бы над пустым уровнем. В оригинале при 6004 картинка вставала обрывом (загрузка рисует её без шага
//      затемнения), плавно — только когда клиент ещё ждал сервер в мире (порталы).
//    [ИСПРАВЛЕНИЕ] `asda.Load.RevealFrames` / `asda.Load.RevealMaxSec` — перед растворением картинка ещё держится
//      непрозрачной, пока не отрисованы первые кадры мира с камерой у персонажа (аналог готовности патчей + cameraUpdate).
//    [ПОРТ] режим 0 не перебивает загрузку и растворение её картинки: в оригинале 4056 при загрузке перекрывается режимом 4.
//    [ИСПРАВЛЕНИЕ] шаг затемнения берёт дельту кадра не больше 0.1 с: подвисание (загрузка уровня) не «съедает» затемнение.
static ASDA_DEV_CVAR(int32, CVarAsdaLoadKeepScreenOnTravel,
	TEXT("asda.Load.KeepScreenOnTravel"), 1,
	TEXT("1 = экран загрузки переживает травел на целевую карту и висит до 4003 (без «неба без земли»); 0 = как было (движок снимал его травелом)."),
	ECVF_Default);
static ASDA_DEV_CVAR(float, CVarAsdaLoadFadeSec,
	TEXT("asda.Load.FadeSec"), 1.5f,
	TEXT("Длительность экранного затемнения смены карты, с (эталон 1500 мс): проявление/растворение картинки загрузки, чёрный на своё 4056/6025. 0 = обрыв, как было."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarAsdaLoadRevealFrames,
	TEXT("asda.Load.RevealFrames"), 3,
	TEXT("Сколько кадров мира отрисовать под непрозрачной картинкой (камера у персонажа) перед её растворением; 0 = сразу."),
	ECVF_Default);
static ASDA_DEV_CVAR(float, CVarAsdaLoadRevealMaxSec,
	TEXT("asda.Load.RevealMaxSec"), 1.0f,
	TEXT("Потолок ожидания готовности мира перед растворением картинки загрузки, с."),
	ECVF_Default);

// Длительность затемнения (0 = выключено).
static float AsdaFadeSec() { return FMath::Max(0.f, CVarAsdaLoadFadeSec.GetValueOnGameThread()); }

void UAsdaLoadManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	MapTable = LoadObject<UDataTable>(nullptr, GMapTablePath);
	if (!MapTable) { UE_LOG(LogAsdaLoad, Warning, TEXT("DT_MapTable не загружен (%s) — реестр карт пуст"), GMapTablePath); }
	// Слушаем завершение любого травела: на L_Loading запускаем загрузку целевой карты, на целевом — снимаем экран.
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UAsdaLoadManagerSubsystem::OnPostLoadMap);
	// ★ 31.10.2026 затемнение: своё 4056 (воскрешение/перенос на месте) → режим 0 оригинала. Сетевая подсистема — раньше нас.
	if (UAsdaNetSubsystem* Net = Collection.InitializeDependency<UAsdaNetSubsystem>())
	{
		WorldReadyHandle = Net->OnWorldReady.AddUObject(this, &UAsdaLoadManagerSubsystem::OnNetWorldReady);
	}
}

void UAsdaLoadManagerSubsystem::Deinitialize()
{
	if (PostLoadMapHandle.IsValid()) { FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle); PostLoadMapHandle.Reset(); }
	if (WorldReadyHandle.IsValid())
	{
		if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { Net->OnWorldReady.Remove(WorldReadyHandle); }
		WorldReadyHandle.Reset();
	}
	// Выход из игры: без растворения — все слои снять сразу.
	ClearScreenFade();
	if (LoadingWidget) { LoadingWidget->RemoveFromParent(); }
	LoadingWidget = nullptr; LoadingImage = nullptr; LoadingFill = nullptr; LoadingText = nullptr; LoadingFrozen = nullptr;
	if (FadeTicker.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(FadeTicker); FadeTicker.Reset(); }
	FadeState = EAsdaScreenFade::None; FadeAlpha = 0.f;
	ReleaseCharSelectPreload();   // ★ charsel_preload: вернуть флаги мипов и отпустить ручку
	Super::Deinitialize();
}

void UAsdaLoadManagerSubsystem::OnPostLoadMap(UWorld* LoadedWorld)
{
	if (!LoadedWorld) return;
	if (PendingTargetMapId < 0)
	{
		// ★ Ревизия 31.10 [ИСПРАВЛЕНИЕ]: травел ВНЕ цепочки загрузки (уход на экран входа по ОК попапа обрыва, возврат к выбору
		//   персонажа, любой OpenLevel), а экран загрузки ещё на экране (он переживает травел, ключ KeepScreenOnTravel) — снимаем,
		//   иначе он висел бы над новой картой без предела (его снимает только 4003, а 4003 уже не придёт).
		if (LoadingWidget || FadingWidget || BlackWidget)
		{
			AbortLoadingScreen(*FString::Printf(TEXT("травел на '%s' вне загрузки карты"), *LoadedWorld->GetName()));
		}
		return;
	}
	const FString MapName = LoadedWorld->GetName();   // напр. "L_Loading" / "alpen" (в PIE — с префиксом UEDPIE_)
	if (MapName.Contains(GLoadingLevelName))
	{
		// Мы на выделенном уровне загрузки (login уже выгружен травелом) → показать экран + грузить целевую карту.
		StartTargetLoad();
	}
	else
	{
		// ★ ФАЗА B3 (WORLD_ENTRY_CHAIN.md): целевой уровень загружен (аналог эталонного LoadProgress==3.0)
		//   → шлём 4001 CharLogonReq (триггер DB-бурста сервера). Экран НЕ прячем — эталон держит его до
		//   конца сетевой синхронизации (у нас — до 4055 OnWorldReady, см. UAsdaWorldSubsystem).
		PendingTargetMapId = -1;
		// ★ 31.10.2026: экран загрузки должен пережить травел на целевую карту (он висит до 4003). Прежде движок снимал его
		//   при уничтожении мира L_Loading (виджет GameInstance, `bAutoRemoveOnWorldRemoved`), и до 4003 игрок видел карту
		//   без персонажа — камера смотрела в небо. Здесь — след в журнале и страховка: виджет не на экране → поставить.
		if (LoadingWidget)
		{
			const bool bOn = IsLoadScreenOnViewport();
			UE_LOG(LogAsdaLoad, Log, TEXT("[загрузка] целевая карта '%s' загружена: экран загрузки во вьюпорте — %s"),
				*MapName, bOn ? TEXT("да") : TEXT("НЕТ (снят травелом)"));
			if (!bOn && CVarAsdaLoadKeepScreenOnTravel.GetValueOnGameThread() != 0) { AddScreenWidget(LoadingWidget, 30000); }
		}
		bool bSent = false;
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
			{
				if (Net->bInGameConnected)
				{
					// ★ ПОРТАЛ-ХЕНДОФФ (4055 смена карты): портальный логон 5058 с тайлом респауна —
					//   НЕ обычный 4001 (тот спавнит по старой позиции CUser; живой тест 2026-08-04).
					bSent = Net->bPortalPending ? Net->SendCharPortalLogonReq() : Net->SendCharLogonReq();
				}
			}
		}
		if (!bSent && bDebugHoldScreenAsOnline)
		{
			// Самотест `asda.Load.FadeSelfTest`: экран держим, как при живом соединении (его снимет эмуляция 4003).
			UE_LOG(LogAsdaLoad, Log, TEXT("[загрузка] самотест: офлайн-карта держит экран, как онлайн (ждём эмуляцию 4003)"));
			return;
		}
		if (!bSent)
		{
			// Оффлайн-просмотр карты (нет game-коннекта) — экран снять сразу, как раньше.
			HideLoadingScreen();
			return;
		}
		// Страховка (эталон: таймаут ожидания сервера 120с → state16+попап; у нас пока Hide+лог).
		// ★ Ревизия 31.10: таймер — на GameInstance, не на мире: травел (уход на экран входа) убил бы его вместе с миром.
		if (UGameInstance* TGI = GetGameInstance())
		{
			TGI->GetTimerManager().SetTimer(NetSyncTimeoutTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				UE_LOG(LogAsdaLoad, Warning, TEXT("Таймаут 120с ожидания world-state (эталон state16) — снимаю экран"));
				HideLoadingScreen();
			}), 120.f, false);
		}
	}
}

const FAsdaMapRow* UAsdaLoadManagerSubsystem::GetMapRow(int32 MapId) const
{
	if (!MapTable) return nullptr;
	// RowName = mapId строкой.
	const FString Row = FString::FromInt(MapId);
	return MapTable->FindRow<FAsdaMapRow>(FName(*Row), TEXT("GetMapRow"), false);
}

FString UAsdaLoadManagerSubsystem::GetMapDirName(int32 MapId) const
{
	const FAsdaMapRow* R = GetMapRow(MapId);
	return R ? R->DirName : FString();
}

FText UAsdaLoadManagerSubsystem::GetMapDisplayName(int32 MapId) const
{
	// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ПОРТА (02.09.2026, жалоба владельца: имя локации по-английски).
	//
	// ЧТО ДЕЛАЕТ ОРИГИНАЛ: отображаемое имя карты берётся из записи карты по mapId, поле +40
	//   (cp1251), напр. эталон уровня «Кто?»: sprintf("%s (%d)", rec+40, cnt) (game.c:505229);
	//   мини-карта — std::string(rec+4)→+40 (CMap_LoadMapForSlot@0xB4CA90). В локализованном
	//   клиенте это поле содержит РУССКОЕ имя («Долина Ночи»).
	//
	// ЧТО ДЕЛАЛ ПОРТ: возвращал DT_MapTable.DisplayName напрямую, а колонка DisplayName в
	//   map_table.csv заполнена ЛАТИНСКИМ именем папки ("silalis", "alpen", …) — не русским +40.
	//   ⇒ вкладки «Кто?»/«Приятель»/«Клан» (общий вызов GetMapDisplayName) и загрузочный экран
	//   показывали английское имя. (DisplayName == +40 в порт не перенесён; русские имена
	//   курируются отдельно в RawAssets/Text/World/Map_info.csv → DT_Map_info.)
	//
	// ЧТО ДЕЛАЕМ: имя карты берём из ЕДИНОГО источника русских имён — локтаблицы Map_info по
	//   mapId (тот же вызов, что у мини-карты AsdaMinimapSubsystem и квестлога AsdaQuestLogSubsystem,
	//   закон «одна точка данных»). Фолбэк: DT_MapTable.DisplayName, затем имя папки DirName —
	//   для карт без русского перевода (сейчас только id 23 «mine»: в Map_info его нет).
	//   Ничего не выдумываем: где перевода нет, честно отдаём латинское имя записи.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UAsdaTextSubsystem* TS = GI->GetSubsystem<UAsdaTextSubsystem>())
		{
			const FString N = TS->GetText(TEXT("Map_info"), MapId, TEXT("name"));
			if (!N.IsEmpty())
			{
				return FText::FromString(N);
			}
		}
	}
	// Локтаблица имени не дала — падаем на запись карты: её DisplayName, иначе имя папки.
	const FAsdaMapRow* R = GetMapRow(MapId);
	if (!R) { return FText::GetEmpty(); }
	// ★ [ПОРТ] 09.10.2026 (мультиязычность): оверлей `MapTable`, ключ `<mapId>.DisplayName` (RowName DT_MapTable = mapId); русская сессия — как было.
	if (R->DisplayName.IsEmpty()) { return FText::FromString(R->DirName); }
	if (FAsdaLang::IsRu()) { return R->DisplayName; }
	return FText::FromString(FAsdaLang::ResolveId(TEXT("MapTable"), R->MapId, TEXT("DisplayName"), R->DisplayName.ToString()));
}

UTexture2D* UAsdaLoadManagerSubsystem::PickLoadingTexture(int32 MapId) const
{
	// ══════════════════════════════════════════════════════════════════════════════════════════
	// ★★★ РЕКЛАМЫ НА ЗАГРУЗОЧНОМ ЭКРАНЕ В ОРИГИНАЛЕ НЕТ НИКОГДА (правка 02.09.2026, К-8).
	//
	// БЫЛО В ПОРТЕ: `GAdChancePercent = 30` с подписью «дефолт 30 %» — число ВЫДУМАНО, и примерно
	//   треть загрузок показывала `ad_img*`, чего оригинал не делает.
	//
	// ЧТО ДЕЛАЕТ ОРИГИНАЛ (`CMap_LoadLoadingScreen@0x975650`, `RE/etalon/world_map.c:133-134`):
	//     roll = rand() % 100;  if (roll < a4) → ветка рекламы;  else → ветка карты.
	//   Последний аргумент `a4` во ВСЕХ 9 вызовах — глобал `g_nLoadingScreenChannel`
	//   (`world_map.c:8512,8573`; `game_main.c:5279`; `network.c:3140,42678,43081,43170,43910,44048`).
	//
	// ДОКАЗАТЕЛЬСТВО ЗНАЧЕНИЯ (перепроверено своими глазами 02.09.2026, закон
	//   `idb-build-differs-from-live` — образы названы явно):
	//     • Таблица секций PE `Donors\AsdaChroniclesInterlude\system\AsdaStory.exe` (md5 a0a00b40…, 13 344 768 б):
	//       imagebase 0x400000, `.data` VA 0x15B5000, raw 0xC98000, rawsize 0x17000 ⇒
	//       VA 0x15CB4C8 лежит в `.data` (смещение в секции 0x164C8 < 0x17000) ⇒ файловый оффсет 0xCAE4C8.
	//     • По 0xCAE4C8 лежит `00 00 00 00` — то есть g_nLoadingScreenChannel = 0.
	//     • Литерал адреса (`c8 b4 5c 01`) встречается во ВСЁМ файле ровно 9 раз, и перед каждым
	//       стоит опкод `A1` (`mov eax, ds:[imm32]`) — только ЧТЕНИЕ. Ни `A3`/`89`/`C7 05` (запись),
	//       ни `68`/`B8..BF`/`8D 05` (взятие адреса) нет ⇒ переменную не может записать ни .ini,
	//       ни пакет, ни memcpy: указателя на неё в коде не существует.
	//     • То же самое побайтово подтверждено на ВТОРОМ образе — `AsdaStory1.exe` (md5 9dbc69eb…),
	//       том самом, на котором построен IDB.
	//   ⇒ `rand() % 100 < 0` ложно ВСЕГДА: ветка рекламы в оригинале мертва.
	//
	// СТАЛО: ветки рекламы здесь НЕТ ВООБЩЕ (её нельзя «включить» — в оригинале включателя нет),
	//   а цепочка фолбэков приведена к эталону 1:1 (находка №2 того же аудита):
	//     1) картинки конкретной карты `map\<dir>\load\*` → `rand() % count`  (`world_map.c:196-235`);
	//     2) папки нет / она пуста → `map\load\start_logo.TEX` И ВЫХОД        (`world_map.c:212-218`).
	//   Прежний порт при пустой папке брал ЛЮБУЮ глобальную текстуру, включая `ad_img*` — то есть
	//   реклама вылезала даже с нулевой вероятностью. Этого фолбэка больше нет.
	//
	// ⚠ Файлы `Textures/Maps/load/ad_img000..008` НЕ мусор и НЕ удалять: это картинки эталонного
	//   клиента; загрузочный экран их не показывает, но они остаются частью исходных данных.
	//   Стартовая рекламная подсистема (`System\AD.bin`) — это ДРУГАЯ подсистема с ДРУГИМИ
	//   картинками (`start_ad_img0N`), см. `UAsdaStartupAdSubsystem`.
	// ══════════════════════════════════════════════════════════════════════════════════════════
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	auto ListTextures = [&AR](const FString& PkgPath) -> TArray<FAssetData>
	{
		TArray<FAssetData> Out;
		FARFilter F; F.PackagePaths.Add(FName(*PkgPath)); F.bRecursivePaths = true;
		F.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
		AR.GetAssets(F, Out);
		return Out;
	};
	// ★ 12.10.2026: разовое переопределение (тест производительности — pt_RU/pt_EN по языку сессии). Не загрузилась —
	//   предупреждение и штатный путь ниже.
	if (!NextLoadingTextureOverride.IsEmpty())
	{
		if (UTexture2D* T = LoadObject<UTexture2D>(nullptr, *NextLoadingTextureOverride, nullptr, LOAD_NoWarn | LOAD_Quiet)) { return T; }
		UE_LOG(LogAsdaLoad, Warning, TEXT("PickLoadingTexture: переопределение фона %s не загрузилось — штатный выбор"), *NextLoadingTextureOverride);
	}
	const FString Dir = GetMapDirName(MapId);
	FRandomStream Rng(FPlatformTime::Cycles());

	// Ветка карты (единственная, что живёт в оригинале): `map\<dir>\load\*.*`, выбор `rand() % count`.
	TArray<FAssetData> Candidates;
	if (!Dir.IsEmpty())
	{
		// ★ ИСПРАВЛЕНИЕ D1 (волна 2 п. 7, 14.09.2026): имя папки карты приводится AsdaMapRegistry::ToAssetName (пробел → «_»),
		//   как при импорте. Было «…/Maps/Mountain Flamion/load» при папке на диске Mountain_Flamion (2 картинки) → пусто →
		//   вместо картинок карты 12 показывался фолбэк start_logo. Остальные 23 карты без пробела — строка та же.
		Candidates = ListTextures(AsdaAssetPaths::MapLoadDir(Dir));
	}
	if (Candidates.Num() > 0)
	{
		const FAssetData& Pick = Candidates[Rng.RandRange(0, Candidates.Num() - 1)];
		return Cast<UTexture2D>(Pick.GetAsset());
	}

	// Папки у карты нет или она пуста → эталонный фолбэк `map\load\start_logo.TEX` и ВЫХОД.
	if (UTexture2D* Logo = LoadObject<UTexture2D>(nullptr, GTexStartLogo))
	{
		return Logo;
	}
	UE_LOG(LogAsdaLoad, Warning,
		TEXT("PickLoadingTexture: у карты dir='%s' нет картинок и не загрузился фолбэк %s"),
		*Dir, GTexStartLogo);
	return nullptr;
}

void UAsdaLoadManagerSubsystem::AddScreenWidget(UUserWidget* W, int32 ZOrder)
{
	if (!W) { return; }
	UGameViewportSubsystem* VS = UGameViewportSubsystem::Get();
	if (CVarAsdaLoadKeepScreenOnTravel.GetValueOnGameThread() != 0 && VS)
	{
		if (VS->IsWidgetAdded(W)) { return; }
		// Слот как у AddToViewport (весь экран), но травел виджет НЕ снимает — снимаем сами.
		FGameViewportWidgetSlot Slot;
		Slot.ZOrder = ZOrder;
		Slot.bAutoRemoveOnWorldRemoved = false;
		VS->AddWidget(W, Slot);
		return;
	}
	if (!W->IsInViewport()) { W->AddToViewport(ZOrder); }   // прежний путь (ключ 0)
}

bool UAsdaLoadManagerSubsystem::IsLoadScreenOnViewport() const
{
	const UUserWidget* W = LoadingWidget ? LoadingWidget.Get() : FadingWidget.Get();
	const UGameViewportSubsystem* VS = UGameViewportSubsystem::Get();
	return W && ((VS && VS->IsWidgetAdded(W)) || W->IsInViewport());
}

void UAsdaLoadManagerSubsystem::ApplyPictureBrush(UImage* Img, UTexture2D* Tex, bool bFadeCapable)
{
	if (!Img || !Tex) { return; }
	if (bFadeCapable)
	{
		// ★ 31.10.2026: кисть-текстура с кадрированием UV V 0..0.75 (то же, что делает M_LoadingBG и квад эталона).
		//   Материал M_LoadingBG непрозрачный — его не растворить альфой; кисть-текстура растворяется ColorAndOpacity.
		FSlateBrush Br;
		Br.SetResourceObject(Tex);
		Br.DrawAs = ESlateBrushDrawType::Image;
		Br.Tiling = ESlateBrushTileType::NoTile;
		Br.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
		Br.SetUVRegion(FBox2f(FVector2f(0.f, 0.f), FVector2f(1.f, 0.75f)));
		Img->SetBrush(Br);
		return;
	}
	// ★ КОРЕНЬ (реверс uiRenderLoadingBar@0x97A6B0, ui_core.c:1496-1513): загрузочные текстуры 1024×1024
	//   имеют белый паддинг снизу — контент в верхних 768px (0.75). Оригинал строит quad с UV V 0..0.75
	//   (обрезает паддинг) и тянет на весь экран. Материал M_LoadingBG делает то же (VTiling=0.75) по UV.
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, AsdaMat::kLoadingBgPath);   // ★ В1-C F16
	if (BaseMat)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, this);
		MID->SetTextureParameterValue(AsdaMat::kParamTex, Tex);   // ★ В1-C F10
		Img->SetBrushFromMaterial(MID);
	}
	else
	{
		UE_LOG(LogAsdaLoad, Warning, TEXT("M_LoadingBG не загружен — фон загрузчика не показан"));
	}
}

void UAsdaLoadManagerSubsystem::EnsureLoadingWidget()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) return;
	if (LoadingWidget)
	{
		// Виджет мог снять травел (ключ KeepScreenOnTravel 0) — вернуть на экран.
		if (!IsLoadScreenOnViewport()) { AddScreenWidget(LoadingWidget, 30000); }
		return;
	}
	{
		UClass* WC = LoadClass<UUserWidget>(nullptr, GLoadWidgetClass);
		if (!WC) { UE_LOG(LogAsdaLoad, Warning, TEXT("WBP_LoadingScreen не загружен")); return; }
		LoadingWidget = CreateWidget<UUserWidget>(GI, WC);
		if (!LoadingWidget || !LoadingWidget->WidgetTree) { LoadingWidget = nullptr; return; }
		UCanvasPanel* Root = Cast<UCanvasPanel>(LoadingWidget->WidgetTree->RootWidget);
		if (!Root) { LoadingWidget = nullptr; return; }
		UWidgetTree* T = LoadingWidget->WidgetTree;

		// ★ 31.10.2026 затемнение: нижний слой — застывший последний кадр мира (виден только пока картинка проявляется).
		LoadingFrozen = T->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("LoadFrozen"));
		LoadingFrozen->SetVisibility(ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Root->AddChild(LoadingFrozen)))
		{ S->SetAutoSize(false); S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f)); S->SetAlignment(FVector2D(0.f, 0.f)); S->SetOffsets(FMargin(0)); }

		// ★ Фон = ПОЛНОЭКРАННАЯ картинка загрузки (реверс uiRenderLoadingBar: квад 0,0→width,height, stretch).
		//   Картинка НЕСЁТ вшитый бар-фон (пустой трек). Анкор 0..1, brush тянется на весь виджет.
		LoadingImage = T->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("LoadBg"));
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Root->AddChild(LoadingImage)))
		{ S->SetAutoSize(false); S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f)); S->SetAlignment(FVector2D(0.f, 0.f)); S->SetOffsets(FMargin(0)); }

		// ★ Заливка прогресса ПОВЕРХ бара картинки (реверс uiRenderLoadingBar self+6, база 1024×768):
		//   left=126/1024, right=(1024-128)/1024=896/1024, top=687/768, bottom=(687+15)/768=702/768.
		//   Правый край заливки = lerp(left, right, progress) → анимируем anchorMax.x в SetLoadProgress.
		LoadingFill = T->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("LoadFill"));
		{
			FSlateBrush Br; Br.DrawAs = ESlateBrushDrawType::Box;
			Br.TintColor = FSlateColor(FLinearColor(1.0f, 0.85f, 0.0f, 1.0f));   // ЖЁЛТАЯ заливка (как оригинал progressBarTex)
			LoadingFill->SetBrush(Br);
		}
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Root->AddChild(LoadingFill)))
		{ S->SetAutoSize(false); S->SetAnchors(FAnchors(126.f/1024.f, 687.f/768.f, 126.f/1024.f, 702.f/768.f)); S->SetOffsets(FMargin(0)); }

		// ★ Чистый AddToViewport() БЕЗ Set*InViewport = виджет заполняет ВЕСЬ экран адаптивно (документированное
		//   поведение UUserWidget). Любой SetDesiredSizeInViewport/SetAnchorsInViewport ЛОМАЕТ это (фиксирует размер).
		//   Root CanvasPanel растянется на весь экран → LoadingImage (анкор 0..1) заполнит его = full-screen.
		// ★ Поверх всего, на весь экран. ★ 31.10.2026: через AddScreenWidget — травел на целевую карту экран не снимает.
		AddScreenWidget(LoadingWidget, 30000);
	}
}

FString UAsdaLoadManagerSubsystem::GetPictureTexPath() const
{
	// Путь выбранной картинки (PictureTex) или пусто.
	return PictureTex ? PictureTex->GetPathName() : FString();
}

void UAsdaLoadManagerSubsystem::ShowLoadingScreen(int32 MapId)
{
	CurrentMapId = MapId;
	if (!GetGameInstance()) return;
	// Растворение/чёрный от прошлого перехода ещё идут — новый переход их снимает (своё проявление — оставляет).
	if (FadeState != EAsdaScreenFade::PictureIn) { ClearScreenFade(); }
	EnsureLoadingWidget();
	if (!LoadingWidget) { return; }

	// Картинка на весь экран: DrawAs=Box + Margin=0 растягивает ВСЮ текстуру на геометрию виджета (без сохранения
	//   аспекта, в отличие от SetBrushFromTexture/DrawAs=Image, который оставлял низ пустым).
	// ★ 31.10.2026: картинка одна на весь переход — если её уже выбрало проявление (BeginMapLoad), не перевыбираем.
	if (LoadingImage && !(PictureTex && PictureMapId == MapId))
	{
		PictureTex = PickLoadingTexture(MapId);
		PictureMapId = MapId;
		if (UTexture2D* Tex = PictureTex)
		{
			// Форс резидентности мипов — иначе первые кадры текстура «мыльная» (стримится с низких мипов).
			Tex->bForceMiplevelsToBeResident = true;
			Tex->bGlobalForceMipLevelsToBeResident = true;
			ApplyPictureBrush(LoadingImage, Tex, AsdaFadeSec() > 0.f);
		}
	}
	if (FadeState != EAsdaScreenFade::PictureIn)
	{
		// Обычный показ: картинка непрозрачна, полоса прогресса видна.
		FadeState = EAsdaScreenFade::Loading;
		FadeAlpha = 1.f;
		if (LoadingImage) { LoadingImage->SetColorAndOpacity(FLinearColor::White); }
		if (LoadingFill) { LoadingFill->SetVisibility(ESlateVisibility::HitTestInvisible); }
	}
	SetLoadProgress(0.f);
	// ★ 10.11.2026 (баг «у Inferion нет экрана загрузки»): в строке — какая картинка выбрана, чтобы по логу игрока/самотеста
	//   было видно Start_logo или картинку карты (`asda.Load.PictureSelfTest`).
	UE_LOG(LogAsdaLoad, Log, TEXT("Загрузочный экран: mapId=%d dir='%s' фон=%s картинка=%s"), MapId, *GetMapDirName(MapId),
		NextLoadingTextureOverride.IsEmpty() ? TEXT("штатный") : *NextLoadingTextureOverride, *GetPictureTexPath());
	NextLoadingTextureOverride.Reset();   // разовое: следующая загрузка карты — штатный выбор
}

void UAsdaLoadManagerSubsystem::SetLoadProgress(float Frac01)
{
	// Реверс: правый край заливки = lerp(left=126/1024, right=896/1024, progress). Анимируем anchorMax.x.
	if (!LoadingFill) return;
	const float F = FMath::Clamp(Frac01, 0.f, 1.f);
	const float BarLeft = 126.f / 1024.f, BarRight = 896.f / 1024.f, BarTop = 687.f / 768.f, BarBot = 702.f / 768.f;
	const float FillRight = FMath::Lerp(BarLeft, BarRight, F);
	if (UCanvasPanelSlot* S = AsdaUi::SlotOf(LoadingFill))
	{ S->SetAnchors(FAnchors(BarLeft, BarTop, FillRight, BarBot)); S->SetOffsets(FMargin(0)); }
}

// ★★★ Бюджет прогрева. 0 = не ждать вовсе (поведение до 2026-08-25). Больше бюджет — дольше
//   висит экран, но мир открывается уже в полном качестве.
static TAutoConsoleVariable<float> CVarAsdaLoadStreamWaitSec(
	TEXT("asda.Load.StreamWaitSec"),
	6.f,
	TEXT("Сколько секунд держать загрузочный экран, доводя подкачку текстур до конца (0 = не ждать)."),
	ECVF_Default);

void UAsdaLoadManagerSubsystem::WaitForStreamedContent()
{
	const float Budget = CVarAsdaLoadStreamWaitSec.GetValueOnGameThread();
	if (Budget <= 0.f) { return; }
	const double T0 = FPlatformTime::Seconds();

	// 1. Доедаем асинхронную загрузку пакетов: без этого часть ассетов ещё даже не создана,
	//    и подкачка мипов просто не знает, что для них грузить.
	if (IsAsyncLoading()) { FlushAsyncLoading(); }

	// 2. Сообщаем потоковой подкачке, ОТКУДА смотрим. Она выбирает мипы по экранному размеру, а
	//    в момент снятия экрана мир ещё ни разу не рисовался с новой камерой — без этого шага
	//    список нужных текстур оказался бы пустым и ждать было бы нечего.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (APlayerController* PC = GI->GetFirstLocalPlayerController())
		{
			FVector  ViewLoc = FVector::ZeroVector;
			FRotator ViewRot = FRotator::ZeroRotator;
			PC->GetPlayerViewPoint(ViewLoc, ViewRot);
			FVector2D Size(1920.f, 1080.f);
			if (GEngine && GEngine->GameViewport) { GEngine->GameViewport->GetViewportSize(Size); }
			const float ScreenSize   = FMath::Max(Size.X, 1.f);
			const float FovScreenSize = ScreenSize / FMath::Tan(FMath::DegreesToRadians(45.f));
			IStreamingManager::Get().AddViewInformation(ViewLoc, ScreenSize, FovScreenSize);
		}
	}

	// 3. Блокирующая догрузка всего, что нужно этому виду. Экран в этот момент статичен, поэтому
	//    блокировка игрового потока здесь безвредна — она ровно то, ради чего экран и висит.
	IStreamingManager::Get().StreamAllResources(Budget);

	UE_LOG(LogAsdaLoad, Log,
		TEXT("[загрузка] прогрев контента перед снятием экрана: %.2f с (бюджет %.1f с, ключ asda.Load.StreamWaitSec)"),
		FPlatformTime::Seconds() - T0, Budget);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ «ПЕРВЫЙ РАЗ» БЕЗ ПОДВИСАНИЙ (задача владельца 11.09.2026, замеры и кадры — `Reports/load_hitch/`)
// ════════════════════════════════════════════════════════════════════════════════════════════════
//   Замер холодного процесса (-game, 2560×1440): первый спавн персонажа грузил 55 пакетов СИНХРОННО
//   (122..222 мс вызова), первое открытие инвентаря — 15 пакетов (278 мс, из них класс окна 265 мс);
//   второй раз — 10 мс и 1.4 мс. Качество не трогаем: те же ассеты, те же мипы, просто РАНЬШЕ и фоном.
static TAutoConsoleVariable<int32> CVarAsdaLoadFirstUsePreload(
	TEXT("asda.Load.FirstUsePreload"),
	1,
	TEXT("1 = на загрузочном экране асинхронно подгружать ассеты первого показа персонажа/инвентаря и сетку высот карты; 0 = как было."),
	ECVF_Default);

// Ожидание фоновой компиляции PSO перед снятием экрана. В упакованной игре прекэш PSO включён по умолчанию
//   (r.PSOPrecaching=1), и компонент, чьи PSO ещё компилируются, по умолчанию НЕ рисуется, пока они не готовы
//   (r.PSOPrecache.ProxyCreationStrategy=1, DelayUntilPSOPrecached) — отсюда «персонаж появляется с опозданием».
//   Персонаж спавнится в том же тике ДО снятия экрана (TrySpawnLocalPlayer → RevealWorld), поэтому его запросы
//   прекэша уже стоят в очереди и дожидаются здесь. В сборке редактора прекэш выключен движком
//   (PipelineStateCache::IsPSOPrecachingEnabled под WITH_EDITOR = false) — очередь пуста, ожидание нулевое.
static TAutoConsoleVariable<float> CVarAsdaLoadPsoWaitSec(
	TEXT("asda.Load.PsoWaitSec"),
	5.f,
	TEXT("Сколько секунд максимум держать загрузочный экран, дожидаясь фоновой компиляции PSO (0 = не ждать)."),
	ECVF_Default);

// Отсев отсутствующих ассетов по реестру — асинхронная загрузка иначе засоряет лог предупреждениями.
//   Общая точка предзагрузок «первого раза» и экрана выбора персонажа; What — подпись для журнала.
static TArray<FSoftObjectPath> AsdaFilterPathsByRegistry(const TArray<FSoftObjectPath>& Want, const TCHAR* What)
{
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FSoftObjectPath> Paths;
	for (const FSoftObjectPath& P : Want)
	{
		// У класса блюпринта в реестре лежит сам блюпринт (объект пакета без _C).
		FString Obj = P.ToString();
		Obj.RemoveFromEnd(TEXT("_C"));
		if (AR.GetAssetByObjectPath(FSoftObjectPath(Obj)).IsValid()) { Paths.Add(P); }
		else { UE_LOG(LogAsdaLoad, Warning, TEXT("[загрузка] %s: ассета нет в реестре — пропуск %s"), What, *P.ToString()); }
	}
	return Paths;
}

FString UAsdaLoadManagerSubsystem::WalkGridObjectPath(int32 MapId) const
{
	const FString Dir = GetMapDirName(MapId);
	if (Dir.IsEmpty()) { return FString(); }
	return AsdaMapRegistry::WalkGridObjectPath(Dir);   // ★ волна 2: одна точка пути (было ×2: здесь и в WorldSubsystem)
}

void UAsdaLoadManagerSubsystem::StartFirstUsePreload(int32 MapId)
{
	// Прежняя карта: её сетка высот больше не нужна — отпускаем старую ручку ДО новой.
	FirstUsePreloadHandle.Reset();
	if (CVarAsdaLoadFirstUsePreload.GetValueOnGameThread() == 0) { return; }

	TArray<FSoftObjectPath> Want;
	// 1. Сетка высот карты. FinishLoadAndOpen грузит её ДО травела, а AsdaGround держит СЛАБУЮ ссылку —
	//    сборка мусора травела её выгружала, и спавн перечитывал ассет синхронно (≈66 мс в кадре спавна).
	const FString Grid = WalkGridObjectPath(MapId);
	if (!Grid.IsEmpty()) { Want.Emplace(Grid); }
	// 2. Класс окна инвентаря (265 из 278 мс первого открытия).
	Want.Emplace(UAsdaInventorySubsystem::GetWindowClassPath());
	// 3. Персонаж — база превью ОБОИХ полов (свой пол и полы соседей заранее неизвестны) с зависимостями
	//    (скелет, физика, материалы, текстуры тела и их карты нормалей), стоковые кисти и клипы набора _00.
	for (const uint8 Sex : { (uint8)1, (uint8)2 })
	{
		const FString Base = AsdaCharPreviewBase(Sex);
		Want.Emplace(AsdaAssetPaths::PackageObjectPath(Base));   // ★ волна 2 п. 7
		// Клипы набора _00: idle01..03 / sitdown01 / walk01 / run01 / death01 / back01 = anim_12..19
		//   (таблица Set00 в AAsdaPlayerCharacter::MotionClipIdx, формат пути — там же: "%sanim_%02d").
		for (int32 Clip = 12; Clip <= 19; ++Clip)
		{
			const FString C = Base + AsdaAssetPaths::SkelAnimSuffix(Clip);   // ★ волна 2 п. 7: правила реестра путей
			Want.Emplace(AsdaAssetPaths::PackageObjectPath(C));
		}
		// Стоковые кисти (ветка «перчаток нет» UAsdaCharAssembly::BuildWornParts): имя и раскладка пути —
		//   общие точки UAsdaCharAssembly (★ 13.09: были литералами-копиями).
		const FString Hands = UAsdaCharAssembly::DefaultHandsMeshName(Sex != 2);
		Want.Emplace(AsdaAssetPaths::PackageObjectPath(UAsdaCharAssembly::EquipPartMeshPath(Hands)));   // ★ волна 2 п. 7
	}
	// 4. Эффект и звук появления: 23 пакета эффекта (BP + 11 материалов + 11 текстур) и звук.
	//    Формат пути класса — AAsdaEffectActor::SpawnByJebName/SpawnByBPName ("BP_Fx_" + '-'→'_'),
	//    звука — UAsdaSoundSubsystem::ResolveSound (папка Sound, пакет = имя).
	Want.Emplace(AsdaAssetPaths::FxClassPathFromJeb(kAsdaEntryFxJeb));                        // ★ волна 2 п. 7: правила реестра путей
	Want.Emplace(AsdaAssetPaths::SoundObject(AsdaAssetPaths::kSoundFolder, kAsdaEntrySound));

	// Отсутствующие ассеты отсеиваем по реестру (общая точка обеих предзагрузок).
	TArray<FSoftObjectPath> Paths = AsdaFilterPathsByRegistry(Want, TEXT("предзагрузка"));
	if (Paths.Num() == 0) { return; }

	const double T0 = FPlatformTime::Seconds();
	const int32 N = Paths.Num();
	FirstUsePreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		MoveTemp(Paths),
		FStreamableDelegate::CreateWeakLambda(this, [T0, N]()
		{
			UE_LOG(LogAsdaLoad, Log, TEXT("[загрузка] предзагрузка «первого раза» готова: %d ассетов за %.2f с (фоном, asda.Load.FirstUsePreload)"),
				N, FPlatformTime::Seconds() - T0);
		}),
		FStreamableManager::DefaultAsyncLoadPriority, /*bManageActiveHandle*/ false, /*bStartStalled*/ false,
		TEXT("AsdaFirstUsePreload"));
	UE_LOG(LogAsdaLoad, Log, TEXT("[загрузка] предзагрузка «первого раза» запрошена: %d ассетов (карта %d)"), N, MapId);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ ПРЕДЗАГРУЗКА ЭКРАНА ВЫБОРА ПЕРСОНАЖА (задача владельца 13.09.2026, замеры — `Reports/charsel_preload/`)
// ════════════════════════════════════════════════════════════════════════════════════════════════
//   Замер ДО (-game, ASDA_TEST_FLOW, холодный процесс): кадр появления превью стоил 223 мс — в нём СИНХРОННО
//   грузились 63 пакета (тела превью обоих полов, причёски, кисти, их материалы, текстуры и карты нормалей, клипы,
//   материал фейда, окно выбора). 13 из 17 текстур персонажей появлялись в 64×64 и дотягивались кадрами позже,
//   а 9 так и оставались ниже полного размера (128 из 256): подкачка выбирает мип по экранному размеру.
//   Здесь то же самое грузится ФОНОМ сразу по приходу данных — пока идёт перелёт и открывается шкатулка
//   (≈5.3 с) — и держится сильной ссылкой до выхода с экрана. Качество не снижается: те же ассеты, мипы полные.
static ASDA_DEV_CVAR(int32, CVarAsdaCharSelPreload,
	TEXT("asda.Login.CharSelPreload"),
	1,
	TEXT("1 = фоном предзагружать превью персонажей экрана выбора (меши, клипы, материалы, текстуры с полными мипами, прогрев PSO) и держать до выхода с экрана; 0 = как было (всё грузится в кадре появления)."),
	ECVF_Default);

bool UAsdaLoadManagerSubsystem::IsCharSelectPreloadEnabled()
{
	return CVarAsdaCharSelPreload.GetValueOnGameThread() != 0;
}

void UAsdaLoadManagerSubsystem::StartCharSelectPreload(const TArray<FString>& ObjectPaths, FSimpleDelegate OnLoaded)
{
	if (!IsCharSelectPreloadEnabled()) { ReleaseCharSelectPreload(); return; }

	TArray<FSoftObjectPath> Want;
	for (const FString& P : ObjectPaths) { if (!P.IsEmpty()) { Want.AddUnique(FSoftObjectPath(P)); } }
	TArray<FSoftObjectPath> Paths = AsdaFilterPathsByRegistry(Want, TEXT("предзагрузка выбора персонажа"));
	if (Paths.Num() == 0) { return; }

	// Тот же набор уже запрошен (данные списка приходят несколькими пакетами, событие зовётся повторно) —
	//   не перезапрашиваем: ручка либо уже держит ассеты, либо догрузит и доведёт запрос сама.
	TArray<FString> Key;
	for (const FSoftObjectPath& P : Paths) { Key.Add(P.ToString()); }
	Key.Sort();
	if (CharSelectPreloadHandle.IsValid() && Key == CharSelectPreloadKey) { return; }
	CharSelectPreloadKey = MoveTemp(Key);

	// Прежняя ручка живёт, пока новая не догрузит: общие ассеты (база превью, кисти) между ними не выгружаются.
	if (CharSelectPreloadHandle.IsValid()) { CharSelectPreloadPrev = CharSelectPreloadHandle; }
	const int32 Gen = ++CharSelectPreloadGen;
	const double T0 = FPlatformTime::Seconds();
	const int32 N = Paths.Num();
	CharSelectPreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		MoveTemp(Paths),
		FStreamableDelegate::CreateWeakLambda(this, [this, Gen, T0, N, OnLoaded]()
		{
			FinishCharSelectPreload(Gen, T0, N, OnLoaded);
		}),
		FStreamableManager::DefaultAsyncLoadPriority, /*bManageActiveHandle*/ false, /*bStartStalled*/ false,
		TEXT("AsdaCharSelectPreload"));
	CharSelectPreloadHandleGen = Gen;
	UE_LOG(LogAsdaLoad, Log, TEXT("[загрузка] предзагрузка выбора персонажа запрошена: %d ассетов (фоном, asda.Login.CharSelPreload)"), N);
	// Всё уже было в памяти: колбэк мог сработать ДО присвоения ручки (и был отброшен) — доводим здесь.
	if (CharSelectPreloadHandle.IsValid() && CharSelectPreloadHandle->HasLoadCompleted())
	{
		FinishCharSelectPreload(Gen, T0, N, OnLoaded);
	}
}

void UAsdaLoadManagerSubsystem::FinishCharSelectPreload(int32 Gen, double T0, int32 N, FSimpleDelegate OnLoaded)
{
	// Устаревший запрос (набор сменился или ручку отпустили), ручка ещё не присвоена, либо запрос уже доведён.
	if (Gen != CharSelectPreloadGen || Gen != CharSelectPreloadHandleGen || Gen == CharSelectPreloadDoneGen
		|| !CharSelectPreloadHandle.IsValid())
	{
		return;
	}
	CharSelectPreloadDoneGen = Gen;
	CharSelectPreloadPrev.Reset();

	// ПОЛНАЯ РЕЗИДЕНТНОСТЬ. Подкачка выбирает мип по экранному размеру и не грузит то, что ещё не рисовалось;
	//   постоянный bForceMiplevelsToBeResident (то же правило, что у персонажа в мире, char-stable-quality)
	//   заставляет её довести ВСЕ мипы заранее, пока превью нет на экране. Кому: текстурам материалов мешей
	//   (включая карты нормалей), явным текстурам (лица, цвета причёсок, эталонные текстуры предметов) и самим
	//   мешам (уровни детализации в упакованной игре тоже подкачиваются).
	TArray<UObject*> Assets;
	CharSelectPreloadHandle->GetLoadedAssets(Assets);
	TSet<UStreamableRenderAsset*> Render;
	auto AddMaterial = [&Render](UMaterialInterface* M)
	{
		if (!M) { return; }
		TArray<UTexture*> Used;
		M->GetUsedTextures(Used);
		for (UTexture* T : Used) { if (T) { Render.Add(T); } }
	};
	for (UObject* O : Assets)
	{
		if (UTexture* T = Cast<UTexture>(O)) { Render.Add(T); }
		else if (USkeletalMesh* Sk = Cast<USkeletalMesh>(O))
		{
			Render.Add(Sk);
			for (const FSkeletalMaterial& M : Sk->GetMaterials()) { AddMaterial(M.MaterialInterface); }
		}
		else if (UStaticMesh* Sm = Cast<UStaticMesh>(O))
		{
			Render.Add(Sm);
			for (const FStaticMaterial& M : Sm->GetStaticMaterials()) { AddMaterial(M.MaterialInterface); }
		}
		else if (UMaterialInterface* Mi = Cast<UMaterialInterface>(O)) { AddMaterial(Mi); }
	}
	// ★ Флаг — через ЕДИНЫЙ реестр владения (ревизия 13.09): доля предзагрузки не зависит от того, успело ли
	//   превью или чужой персонаж поставить флаг раньше, и снимается только когда её не держит больше никто.
	//   Половина текстур правила качества — ключами `asda.Char.StableQuality` и `asda.Char.StableQuality.Textures` ≥ 1.
	const int32 Before = CharSelectForcedAssets.Num();
	const bool bForceMips = AAsdaPlayerCharacter::IsStableQualityEnabled() && AAsdaPlayerCharacter::GetStableQualityTexturesMode() >= 1;
	if (bForceMips)
	{
		TSet<UStreamableRenderAsset*> Want;
		for (UStreamableRenderAsset* A : Render) { if (A) { Want.Add(A); } }
		// Прежний набор (другие персонажи списка) отпускается, новый берётся.
		UAsdaTextureSubsystem::SyncFullMips(CharSelectForcedAssets, Want);
	}
	else
	{
		UAsdaTextureSubsystem::ReleaseAllFullMips(CharSelectForcedAssets);
	}
	UE_LOG(LogAsdaLoad, Log, TEXT("[загрузка] предзагрузка выбора персонажа готова: %d ассетов за %.2f с; полные мипы/LOD держим у %d (было %d%s)"),
		N, FPlatformTime::Seconds() - T0, CharSelectForcedAssets.Num(), Before, bForceMips ? TEXT("") : TEXT(", половина текстур выключена ключом"));
	OnLoaded.ExecuteIfBound();
}

void UAsdaLoadManagerSubsystem::GetCharSelectPreloadedAssets(TArray<UObject*>& Out) const
{
	Out.Reset();
	if (CharSelectPreloadHandle.IsValid()) { CharSelectPreloadHandle->GetLoadedAssets(Out); }
}

void UAsdaLoadManagerSubsystem::ReleaseCharSelectPreload()
{
	++CharSelectPreloadGen;   // отложенный колбэк прежнего запроса станет устаревшим
	const bool bHad = CharSelectPreloadHandle.IsValid() || CharSelectPreloadPrev.IsValid() || CharSelectForcedAssets.Num() > 0;
	const int32 Restored = CharSelectForcedAssets.Num();
	UAsdaTextureSubsystem::ReleaseAllFullMips(CharSelectForcedAssets);   // флаг снимется, где его не держит никто другой
	CharSelectPreloadHandle.Reset();
	CharSelectPreloadPrev.Reset();
	CharSelectPreloadKey.Reset();
	if (bHad)
	{
		UE_LOG(LogAsdaLoad, Log, TEXT("[загрузка] предзагрузка выбора персонажа отпущена (доля во флаге полных мипов отпущена у %d ассетов)"), Restored);
	}
}

void UAsdaLoadManagerSubsystem::WaitForPsoPrecache(double Budget)
{
	if (Budget <= 0.0) { return; }
	// Запросы прекэша, выданные в этом тике (регистрация компонентов персонажа), могут ещё сидеть в очереди
	//   рисующего потока — отдаём её, чтобы счётчик активных запросов был честным.
	FlushRenderingCommands();
	const uint32 Start = PipelineStateCache::NumActivePrecacheRequests();
	if (Start == 0) { return; }
	const double T0 = FPlatformTime::Seconds();
	while (PipelineStateCache::NumActivePrecacheRequests() > 0 && (FPlatformTime::Seconds() - T0) < Budget)
	{
		FPlatformProcess::Sleep(0.002f);
	}
	UE_LOG(LogAsdaLoad, Log, TEXT("[загрузка] прекэш PSO перед снятием экрана: было %u запросов, осталось %u, ждали %.2f с (бюджет %.1f с, asda.Load.PsoWaitSec)"),
		Start, PipelineStateCache::NumActivePrecacheRequests(), FPlatformTime::Seconds() - T0, Budget);
}

void UAsdaLoadManagerSubsystem::HideLoadingScreen()
{
	// ★ Прогрев — только если экран реально висел (иначе это обычный вызов «на всякий случай»).
	// Экран снят — предел ожидания 4003 больше не нужен (иначе через 120 с он «снял» бы экран следующего перехода).
	if (UGameInstance* TGI = GetGameInstance()) { TGI->GetTimerManager().ClearTimer(NetSyncTimeoutTimer); }
	if (LoadingWidget) { WaitForStreamedContent(); }
	if (LoadingWidget) { WaitForPsoPrecache(CVarAsdaLoadPsoWaitSec.GetValueOnGameThread()); }
	// ★ 31.10.2026: с затемнением экран не исчезает, а растворяется над готовым миром (режим 4 эталона).
	if (LoadingWidget && AsdaFadeSec() > 0.f && LoadingImage) { BeginPictureFadeOut(); return; }
	if (LoadingWidget) { LoadingWidget->RemoveFromParent(); }
	LoadingWidget = nullptr; LoadingImage = nullptr; LoadingFill = nullptr; LoadingText = nullptr; LoadingFrozen = nullptr;
	if (FadeState == EAsdaScreenFade::PictureIn || FadeState == EAsdaScreenFade::Loading) { FadeState = EAsdaScreenFade::None; FadeAlpha = 0.f; }
	FrozenTex = nullptr; PictureTex = nullptr; PictureMapId = -1;
}

bool UAsdaLoadManagerSubsystem::CaptureFrozenFrame()
{
	// ★ Не FViewport::ReadPixels: вне отрисовки кадра у вьюпорта нет целевой текстуры (ensure InRHITexture, D3D12RenderTarget.cpp:599,
	//   самотест 31.10). Снимок окна Slate (`FSlateApplication::TakeScreenshot`, им же пользуется скриншот с UI) синхронен
	//   в любой момент игрового потока и даёт кадр вместе с интерфейсом — как его видел игрок.
	UGameViewportClient* GVC = GEngine ? GEngine->GameViewport : nullptr;
	TSharedPtr<SWindow> Win = GVC ? GVC->GetWindow() : nullptr;
	if (!Win.IsValid() || !FSlateApplication::IsInitialized()) { return false; }
	TArray<FColor> Px;
	FIntVector Size3(0, 0, 0);
	if (!FSlateApplication::Get().TakeScreenshot(Win.ToSharedRef(), Px, Size3)) { return false; }
	const FIntPoint Sz(Size3.X, Size3.Y);
	if (Sz.X <= 0 || Sz.Y <= 0 || Px.Num() != Sz.X * Sz.Y) { return false; }
	for (FColor& C : Px) { C.A = 255; }   // альфа сцены не нужна: подложка непрозрачна
	UTexture2D* T = UTexture2D::CreateTransient(Sz.X, Sz.Y, PF_B8G8R8A8, TEXT("AsdaLoadFrozenFrame"));
	if (!T || !T->GetPlatformData() || T->GetPlatformData()->Mips.Num() == 0) { return false; }
	T->SRGB = true;          // снимок окна Slate — уже гамма-кодированный кадр (как его видел игрок)
	T->NeverStream = true;
	T->Filter = TF_Bilinear;
	void* Dst = T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Dst, Px.GetData(), Px.Num() * sizeof(FColor));
	T->GetPlatformData()->Mips[0].BulkData.Unlock();
	T->UpdateResource();
	FrozenTex = T;
	return true;
}

void UAsdaLoadManagerSubsystem::BeginPictureFadeIn(int32 MapId)
{
	ClearScreenFade();
	const bool bFrozen = CaptureFrozenFrame();
	EnsureLoadingWidget();
	if (!LoadingWidget || !LoadingImage) { FrozenTex = nullptr; return; }
	PictureTex = PickLoadingTexture(MapId);
	PictureMapId = MapId;
	if (!PictureTex) { FrozenTex = nullptr; return; }
	PictureTex->bForceMiplevelsToBeResident = true;
	PictureTex->bGlobalForceMipLevelsToBeResident = true;
	ApplyPictureBrush(LoadingImage, PictureTex, /*bFadeCapable*/ true);
	if (LoadingFrozen)
	{
		if (bFrozen && FrozenTex)
		{
			FSlateBrush Br; Br.SetResourceObject(FrozenTex); Br.DrawAs = ESlateBrushDrawType::Image; Br.Tiling = ESlateBrushTileType::NoTile;
			Br.ImageSize = FVector2D(FrozenTex->GetSizeX(), FrozenTex->GetSizeY());
			LoadingFrozen->SetBrush(Br);
			LoadingFrozen->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			LoadingFrozen->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (LoadingFill) { LoadingFill->SetVisibility(ESlateVisibility::Collapsed); }   // режим 3 рисует только картинку
	LoadingImage->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.f));
	FadeState = EAsdaScreenFade::PictureIn;
	FadeAlpha = 0.f;
	FadeElapsed = 0.0;
	if (!FadeTicker.IsValid())
	{
		FadeTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAsdaLoadManagerSubsystem::TickScreenFade), 0.f);
	}
	UE_LOG(LogAsdaLoad, Log, TEXT("[затемнение] режим 3: картинка карты %d проявляется за %.2f с над %s"),
		MapId, AsdaFadeSec(), bFrozen ? TEXT("застывшим кадром мира") : TEXT("пустым экраном (кадр не снят)"));
}

void UAsdaLoadManagerSubsystem::BeginPictureFadeOut()
{
	if (FadingWidget) { FadingWidget->RemoveFromParent(); }
	FadingWidget = LoadingWidget;
	FadingImage = LoadingImage;
	if (LoadingFill) { LoadingFill->SetVisibility(ESlateVisibility::Collapsed); }       // режим 4 рисует только картинку
	if (LoadingFrozen) { LoadingFrozen->SetVisibility(ESlateVisibility::Collapsed); }   // прежний мир больше не нужен
	FadingWidget->SetVisibility(ESlateVisibility::HitTestInvisible);                    // клики идут в мир
	// Проявление не успело дойти до конца (мгновенная офлайн-загрузка) — растворяем с достигнутой непрозрачности.
	const float A0 = (FadeState == EAsdaScreenFade::PictureIn) ? FadeAlpha : 1.f;
	if (FadingImage) { FadingImage->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, A0)); }
	LoadingWidget = nullptr; LoadingImage = nullptr; LoadingFill = nullptr; LoadingText = nullptr; LoadingFrozen = nullptr;
	FrozenTex = nullptr;
	FadeState = EAsdaScreenFade::WaitReady;
	FadeAlpha = A0;
	FadeElapsed = 0.0;
	ReadyFrame0 = GFrameCounter;
	if (!FadeTicker.IsValid())
	{
		FadeTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAsdaLoadManagerSubsystem::TickScreenFade), 0.f);
	}
	UE_LOG(LogAsdaLoad, Log, TEXT("[затемнение] экран снят: картинка держится до готовности мира (%d кадров, потолок %.1f с), затем режим 4 за %.2f с"),
		CVarAsdaLoadRevealFrames.GetValueOnGameThread(), CVarAsdaLoadRevealMaxSec.GetValueOnGameThread(), AsdaFadeSec());
}

void UAsdaLoadManagerSubsystem::BeginBlackFadeOut(const TCHAR* Why)
{
	if (AsdaFadeSec() <= 0.f) { return; }
	if (LoadingWidget || FadeState == EAsdaScreenFade::PictureIn || FadeState == EAsdaScreenFade::Loading
		|| FadeState == EAsdaScreenFade::WaitReady || FadeState == EAsdaScreenFade::PictureOut)
	{
		UE_LOG(LogAsdaLoad, Log, TEXT("[затемнение] режим 0 (%s) пропущен: идёт загрузка или растворение её картинки (эталон: режим 4 конца загрузки его перекрывает)"), Why);
		return;
	}
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	if (!BlackWidget)
	{
		UClass* WC = LoadClass<UUserWidget>(nullptr, GLoadWidgetClass);
		BlackWidget = WC ? CreateWidget<UUserWidget>(GI, WC) : nullptr;
		UCanvasPanel* Root = (BlackWidget && BlackWidget->WidgetTree) ? Cast<UCanvasPanel>(BlackWidget->WidgetTree->RootWidget) : nullptr;
		if (!Root) { BlackWidget = nullptr; return; }
		BlackImage = BlackWidget->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FadeBlack"));
		FSlateBrush Br; Br.DrawAs = ESlateBrushDrawType::Box; Br.TintColor = FSlateColor(FLinearColor::White);
		BlackImage->SetBrush(Br);
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Root->AddChild(BlackImage)))
		{ S->SetAutoSize(false); S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f)); S->SetAlignment(FVector2D(0.f, 0.f)); S->SetOffsets(FMargin(0)); }
		BlackWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	AddScreenWidget(BlackWidget, 30001);
	BlackImage->SetColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 1.f));
	FadeState = EAsdaScreenFade::BlackOut;
	FadeAlpha = 1.f;
	FadeElapsed = 0.0;
	if (!FadeTicker.IsValid())
	{
		FadeTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAsdaLoadManagerSubsystem::TickScreenFade), 0.f);
	}
	UE_LOG(LogAsdaLoad, Log, TEXT("[затемнение] режим 0 (%s): чёрный рассеивается за %.2f с"), Why, AsdaFadeSec());
}

// ★ Ревизия 31.10 [ИСПРАВЛЕНИЕ]: обрыв игрового соединения во время загрузки карты (ожидание 4003, причина 255 или отказ 4101).
//   Эталон: попап обрыва при причине 255 гасится гейтом `!g_bLoadingInProgress` (AsdaStory_game.c:24034) — там он защищает от
//   ложной тревоги, когда смена карты штатно закрывает ПРЕЖНИЙ сокет; сама загрузка затем упирается в предел 120 с → state 16 и
//   попап. У порта закрытие прежнего сокета до `OnGameDisconnected` не доходит (`bInGameConnected` снят до закрытия, AsdaNetSubsystem.cpp
//   «старое game-соединение закрывается в EnterGameWorld»), значит обрыв во время загрузки — всегда обрыв ЖИВОЙ сессии: ждать 120 с
//   под застывшим экраном нечего. 1 — экран снимается сразу, попап виден (UAsdaDisconnectPopupSubsystem); 0 — как было.
static ASDA_DEV_CVAR(int32, CVarAsdaLoadDisconnectDropsScreen,
	TEXT("asda.Load.DisconnectDropsScreen"), 1,
	TEXT("1 = обрыв/отказ сервера во время загрузки карты снимает экран загрузки и показывает попап обрыва; 0 = как было (попап гасился, экран до предела 120 с)."),
	ECVF_Default);

bool UAsdaLoadManagerSubsystem::IsDisconnectDropsScreenOn()
{
	return CVarAsdaLoadDisconnectDropsScreen.GetValueOnGameThread() != 0;
}

void UAsdaLoadManagerSubsystem::AbortLoadingScreen(const TCHAR* Why)
{
	const bool bHad = LoadingWidget || FadingWidget || BlackWidget;
	// Отменить цепочку: целевой травел, добор минимума показа, опрос прогресса, предел ожидания 4003.
	PendingTargetMapId = -1;
	PendingLevelName.Reset();
	FinalShortName.Reset();
	if (UGameInstance* GI = GetGameInstance())
	{
		GI->GetTimerManager().ClearTimer(NetSyncTimeoutTimer);
		if (UWorld* W = GI->GetWorld())
		{
			W->GetTimerManager().ClearTimer(MinShowTimer);
			W->GetTimerManager().ClearTimer(LoadProgressTimer);
		}
	}
	// Слои — сразу, без растворения и без ожиданий подкачки (это ошибка, а не готовый мир).
	ClearScreenFade();
	if (LoadingWidget) { LoadingWidget->RemoveFromParent(); }
	LoadingWidget = nullptr; LoadingImage = nullptr; LoadingFill = nullptr; LoadingText = nullptr; LoadingFrozen = nullptr;
	if (FadeTicker.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(FadeTicker); FadeTicker.Reset(); }
	FadeState = EAsdaScreenFade::None; FadeAlpha = 0.f;
	FrozenTex = nullptr; PictureTex = nullptr; PictureMapId = -1;
	bDebugHoldScreenAsOnline = false;
	if (bHad) { UE_LOG(LogAsdaLoad, Warning, TEXT("[загрузка] экран загрузки снят немедленно: %s"), Why); }
}

void UAsdaLoadManagerSubsystem::OnNetWorldReady()
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && Net->LastWorldReadyOpcode == 4056) { BeginBlackFadeOut(TEXT("своё 4056")); }
}

void UAsdaLoadManagerSubsystem::ClearScreenFade()
{
	if (FadingWidget) { FadingWidget->RemoveFromParent(); }
	FadingWidget = nullptr; FadingImage = nullptr;
	if (BlackWidget) { BlackWidget->RemoveFromParent(); }
	if (FadeState == EAsdaScreenFade::WaitReady || FadeState == EAsdaScreenFade::PictureOut || FadeState == EAsdaScreenFade::BlackOut)
	{
		if (FadeState != EAsdaScreenFade::BlackOut) { PictureTex = nullptr; PictureMapId = -1; }   // картинка прошлого перехода
		FadeState = EAsdaScreenFade::None;
		FadeAlpha = 0.f;
	}
	if (FadeTicker.IsValid() && !LoadingWidget) { FTSTicker::GetCoreTicker().RemoveTicker(FadeTicker); FadeTicker.Reset(); }
}

bool UAsdaLoadManagerSubsystem::TickScreenFade(float Dt)
{
	const float Sec = FMath::Max(AsdaFadeSec(), 0.001f);
	FadeElapsed += FMath::Clamp((double)Dt, 0.0, 0.1);   // подвисание кадра не «съедает» затемнение
	const float T = (float)(FadeElapsed / Sec);
	auto Stop = [this]() { FadeTicker.Reset(); return false; };
	switch (FadeState)
	{
	case EAsdaScreenFade::PictureIn:
		FadeAlpha = FMath::Clamp(T, 0.f, 1.f);
		if (LoadingImage) { LoadingImage->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, FadeAlpha)); }
		if (FadeAlpha >= 1.f)
		{
			if (LoadingFrozen) { LoadingFrozen->SetVisibility(ESlateVisibility::Collapsed); }
			FrozenTex = nullptr;
			if (LoadingFill) { LoadingFill->SetVisibility(ESlateVisibility::HitTestInvisible); }
			FadeState = EAsdaScreenFade::Loading;
			UE_LOG(LogAsdaLoad, Log, TEXT("[затемнение] режим 3 завершён: экран загрузки непрозрачен"));
			return Stop();
		}
		return true;
	case EAsdaScreenFade::WaitReady:
	{
		bool bViewReady = true;
		if (const UGameInstance* GI = GetGameInstance())
		{
			if (const APlayerController* PC = GI->GetFirstLocalPlayerController())
			{
				const APawn* P = PC->GetPawn();
				bViewReady = !P || (PC->PlayerCameraManager && PC->PlayerCameraManager->GetViewTarget() == P);
			}
		}
		const uint64 Frames = GFrameCounter - ReadyFrame0;
		const int32 NeedFrames = FMath::Max(0, CVarAsdaLoadRevealFrames.GetValueOnGameThread());
		const bool bTimeout = FadeElapsed >= CVarAsdaLoadRevealMaxSec.GetValueOnGameThread();
		if ((bViewReady && Frames >= (uint64)NeedFrames) || bTimeout)
		{
			UE_LOG(LogAsdaLoad, Log, TEXT("[затемнение] мир готов (%llu кадров, камера у персонажа: %s%s) — режим 4: картинка растворяется"),
				(unsigned long long)Frames, bViewReady ? TEXT("да") : TEXT("нет"), bTimeout ? TEXT(", по потолку ожидания") : TEXT(""));
			// Растворение продолжает с достигнутой непрозрачности (1 — обычно).
			FadeElapsed = (1.0 - FadeAlpha) * Sec;
			FadeState = EAsdaScreenFade::PictureOut;
		}
		return true;
	}
	case EAsdaScreenFade::PictureOut:
		FadeAlpha = FMath::Clamp(1.f - T, 0.f, 1.f);
		if (FadingImage) { FadingImage->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, FadeAlpha)); }
		if (FadeAlpha <= 0.f)
		{
			if (FadingWidget) { FadingWidget->RemoveFromParent(); }
			FadingWidget = nullptr; FadingImage = nullptr;
			PictureTex = nullptr; PictureMapId = -1;
			FadeState = EAsdaScreenFade::None;
			UE_LOG(LogAsdaLoad, Log, TEXT("[затемнение] режим 4 завершён: картинка загрузки убрана"));
			return Stop();
		}
		return true;
	case EAsdaScreenFade::BlackOut:
		FadeAlpha = FMath::Clamp(1.f - T, 0.f, 1.f);
		if (BlackImage) { BlackImage->SetColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, FadeAlpha)); }
		if (FadeAlpha <= 0.f)
		{
			if (BlackWidget) { BlackWidget->RemoveFromParent(); }
			FadeState = EAsdaScreenFade::None;
			UE_LOG(LogAsdaLoad, Log, TEXT("[затемнение] режим 0 завершён"));
			return Stop();
		}
		return true;
	default:
		return Stop();
	}
}

void UAsdaLoadManagerSubsystem::BeginMapLoad(int32 MapId)
{
	const FAsdaMapRow* Row = GetMapRow(MapId);
	if (!Row) { UE_LOG(LogAsdaLoad, Warning, TEXT("BeginMapLoad: карта %d не в реестре"), MapId); return; }
	if (Row->Level.IsNull())
	{
		UE_LOG(LogAsdaLoad, Warning, TEXT("BeginMapLoad: у карты %d ('%s') нет UE-уровня"), MapId, *Row->DirName);
		return;
	}
	// Запоминаем целевую карту и путь её уровня; сам показ+загрузка — после травела на L_Loading.
	PendingTargetMapId = MapId;
	const FSoftObjectPath LevelPath = Row->Level.ToSoftObjectPath();
	PendingLevelName = FPackageName::ObjectPathToPackageName(LevelPath.ToString());   // /Game/Map/<dir>/<dir>

	// Переход на выделенный УРОВЕНЬ загрузки: штатный травел UE выгружает login/предыдущий уровень.
	//   Экран и async-load целевой карты запустятся в OnPostLoadMap→StartTargetLoad (login уже выгружен).
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) return;
	UE_LOG(LogAsdaLoad, Log, TEXT("BeginMapLoad: mapId=%d → травел на '%s' (выгрузка предыдущего), цель '%s'"), MapId, GLoadingLevelName, *PendingLevelName);
	// ★ 31.10.2026 затемнение (режим 3): картинка карты проявляется над застывшим последним кадром — травел не ждёт.
	//   Экран уже висит (повторный вход в загрузку) — его не трогаем.
	if (AsdaFadeSec() > 0.f && !LoadingWidget) { BeginPictureFadeIn(MapId); }
	UGameplayStatics::OpenLevel(W, FName(GLoadingLevelName));
}

void UAsdaLoadManagerSubsystem::StartTargetLoad()
{
	// Вызывается на уровне L_Loading (предыдущий уровень выгружен). Показываем экран и грузим целевую карту.
	CurrentMapId = PendingTargetMapId;
	ShowLoadingScreen(CurrentMapId);
	LoadShowTime = FPlatformTime::Seconds();   // экран висит МИНИМУМ kMinShowSeconds, даже если загрузка мгновенна
	// ★ Волна В6 ремесла (ревизия В5 МЕЛК-2): шаг эталона `g_LoadProgress == 0` (game_main.c:3540-3549) —
	//   `CGameUI_CloseWindow(5001/5002/0)`, три закрытия менеджера сообщений: у каждого звук 5, а с очередью
	//   окон-сообщений (26.09) при непустой очереди — ещё и возврат записи (25; окно встанет после входа в мир).
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// ★ 22.10.2026: переход по пакету (4055/5055/6013/6015/6201/свиток) закрыл их уже в момент приёма, в порядке эталона
		//   (`AsdaEntryWindows::ApplyMapLoadWindowCloses`) — второй раз (лишние звуки 5) не закрываем.
		if (!WM->ConsumeMapLoadMsgBoxesClosed()) { WM->MapLoadCloseMsgBoxes(); }
	}

	UE_LOG(LogAsdaLoad, Log, TEXT("StartTargetLoad: mapId=%d уровень '%s' → async-load"), CurrentMapId, *PendingLevelName);
	LoadPackageAsync(PendingLevelName,
		FLoadPackageAsyncDelegate::CreateUObject(this, &UAsdaLoadManagerSubsystem::OnLevelPackageLoaded));
	// ★ load_hitch: ассеты «первого раза» (персонаж, инвентарь, сетка высот) — асинхронно, ПОСЛЕ запроса
	//   уровня, чтобы уровень стоял в очереди первым. Экран висит минимум 3 с и дольше грузится сам уровень.
	StartFirstUsePreload(CurrentMapId);
	SetLoadProgress(0.02f);
	// Плавный прогресс: опрашиваем процент async-загрузки пакета 10 раз/сек → заливка бара.
	if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
		W->GetTimerManager().SetTimer(LoadProgressTimer, this, &UAsdaLoadManagerSubsystem::PollLoadProgress, 0.1f, true);
}

void UAsdaLoadManagerSubsystem::PollLoadProgress()
{
	if (PendingLevelName.IsEmpty()) return;
	const float Pct = GetAsyncLoadPercentage(FName(*PendingLevelName));   // -1 если не грузится/готов
	if (Pct >= 0.f) { SetLoadProgress(FMath::Max(0.02f, Pct / 100.f)); }
}

void UAsdaLoadManagerSubsystem::OnLevelPackageLoaded(const FName& PackageName, UPackage* Package, EAsyncLoadingResult::Type Result)
{
	// ★ Ревизия 31.10: загрузку отменил AbortLoadingScreen (обрыв/уход на экран входа) — травел на целевую карту не нужен.
	if (PendingLevelName.IsEmpty() || PackageName.ToString() != PendingLevelName)
	{
		UE_LOG(LogAsdaLoad, Log, TEXT("[загрузка] пакет '%s' догрузился, но загрузка карты отменена — травела не будет"), *PackageName.ToString());
		return;
	}
	// Остановить опрос прогресса.
	if (UWorld* Tw = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
		Tw->GetTimerManager().ClearTimer(LoadProgressTimer);
	if (Result != EAsyncLoadingResult::Succeeded || !Package)
	{
		UE_LOG(LogAsdaLoad, Warning, TEXT("Уровень '%s' не загрузился (result=%d)"), *PackageName.ToString(), (int32)Result);
		return;
	}
	SetLoadProgress(1.f);   // геометрия загружена → бар полон (стадии 4-11 сети = ФАЗА C)
	// Короткое имя уровня для OpenLevel (последний сегмент пути).
	FString ShortName; PendingLevelName.Split(TEXT("/"), nullptr, &ShortName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (ShortName.IsEmpty()) { ShortName = PackageName.ToString(); }
	FinalShortName = ShortName;
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) return;
	// Гарантия минимальной длительности загрузочного экрана: даже если геометрия загрузилась мгновенно,
	//   экран висит не меньше kMinShowSeconds (пользователь успевает увидеть картинку/бар).
	const double kMinShowSeconds = 3.0;
	const double Remain = kMinShowSeconds - (FPlatformTime::Seconds() - LoadShowTime);
	if (Remain > 0.05)
	{
		UE_LOG(LogAsdaLoad, Log, TEXT("Уровень загружен, добор экрана %.2f с → OpenLevel '%s'"), Remain, *FinalShortName);
		W->GetTimerManager().SetTimer(MinShowTimer, this, &UAsdaLoadManagerSubsystem::FinishLoadAndOpen, (float)Remain, false);
	}
	else
	{
		FinishLoadAndOpen();
	}
}

void UAsdaLoadManagerSubsystem::FinishLoadAndOpen()
{
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W || FinalShortName.IsEmpty()) return;
	UE_LOG(LogAsdaLoad, Log, TEXT("Уровень загружен → OpenLevel '%s'"), *FinalShortName);
	// ★ BGM карты БОЛЬШЕ НЕ СТАРТУЕТ ЗДЕСЬ: оригинал выбирает трек по зоне тайла, на котором стоит
	//   персонаж (game_main.c:3404-3418, бит 0x80 → «городской» слот 0, иначе «полевой» слот 1),
	//   а позиция известна только после спавна павна в новом уровне. Этим занимается
	//   UAsdaZoneSubsystem (первый расчёт зоны на карте = ветка «конец загрузки», старт без фейда).
	// ★ СЕТКА ВЫСОТ карты (.TER в DA_WalkGrid) — регистрируем ДО открытия уровня, чтобы первый же
	//   спавн (персонаж/мобы/NPC/лут) уже брал высоту из данных, а не трейсом по мешу.
	{
		// ★ load_hitch: путь — из общей точки WalkGridObjectPath. Ручка предзагрузки держит ассет сильной
		//   ссылкой, но запрос стоит в очереди после уровня: если сетка ещё не дочитана, LoadObject
		//   дочитает её здесь синхронно (корректно, просто без выигрыша).
		const FString GridPath = WalkGridObjectPath(CurrentMapId);
		const UAsdaWalkGrid* G = GridPath.IsEmpty() ? nullptr
			: LoadObject<UAsdaWalkGrid>(nullptr, *GridPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		AsdaGround::SetGrid(G);
	}
	// OpenLevel: переход в мир. GameInstance-субсистемы (сеть/этот LoadManager) переживают травел →
	//   ФАЗА C (приём world-state + спавн) продолжается на game-канале. Загрузочный экран снимет ФАЗА C по спавну.
	//   ?game= — мировой GameMode (AAsdaPlayerController: click-to-move/камера; павн спавнит WorldSubsystem).
	UGameplayStatics::OpenLevel(W, FName(*FinalShortName), true,
		TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode"));
}


// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ДИАГНОСТИКА
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★ ТЕСТОВЫЙ КРЮК, а не игровая механика. В игре `CurrentMapId` выставляет сам менеджер загрузки
//   (`ShowLoadingScreen` и завершение травела) — то же самое, что делает эталонная `g_nCurMapId`.
//   В headless-PIE уровень открывается напрямую, менеджер загрузки не участвует, и номер карты
//   остаётся −1: тогда «своя/чужая карта» не проверить ничем, и живой тест давал бы ложное «нет»
//   (закон `false-negatives-method`). Команда ставит РОВНО ТО ЖЕ поле и ничего больше не трогает.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaLoadCurMap(
	TEXT("Asda.Load.CurrentMap"),
	TEXT("Asda.Load.CurrentMap [id] — показать/задать номер текущей карты (g_nCurMapId).\n")
	TEXT("  Диагностический крюк для живых проверок: в игре это поле ведёт менеджер загрузки."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
			if (!LM) { UE_LOG(LogAsdaLoad, Warning, TEXT("[загрузка] подсистема недоступна")); return; }
			if (Args.Num() >= 1) { LM->CurrentMapId = FCString::Atoi(*Args[0]); }
			UE_LOG(LogAsdaLoad, Log, TEXT("[загрузка] текущая карта = %d"), LM->CurrentMapId);
		}));
#endif // ASDA_DEV_CONSOLE
