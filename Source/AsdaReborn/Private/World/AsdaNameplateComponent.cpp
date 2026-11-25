// Copyright (c) 2026 @sitxovski (Discord)
// Реализация UAsdaNameplateComponent — см. AsdaNameplateComponent.h.
#include "World/AsdaNameplateComponent.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт
#include "Login/AsdaText.h"   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): слово «Одержим» по id
#include "World/AsdaOverheadLate.h"   // ★ 30.10.2026: проекция по камере ЭТОГО кадра (плашка не «плывёт» за камерой)
#include "Npc/AsdaNpcActor.h"   // ★ 09.11.2026: id NPC владельца (префикс [MISSING])
#include "Npc/AsdaNpcMissing.h"   // ★ 09.11.2026: красный префикс [MISSING]
#include "World/AsdaEntityText.h"   // ★ волна 3 п.14: имена сущностей из таблиц — одна точка
#include "AsdaSelf.h"   // ★ 30.10.2026: запись выбранного персонажа — общей точкой (`FindSelectedCharRecord`, одержимость)
#include "World/AsdaCoord.h"   // ★ Аудит №3 (B1/B2): единый перевод координат и тайлы
#include "UI/AsdaUiHelpers.h"   // ★ D3: единая проекция над-головного UI (дедуп 2026-08-08)

#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Resources/AsdaTextureSubsystem.h"   // ★ Аудит №3 (A8/B8): текстуры только через менеджер
#include "Engine/TextureRenderTarget2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/AsdaMarkup.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/CapsuleComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/DataTable.h"
#include "World/AsdaWorldSubsystem.h"   // FAsdaEntityNameRow
#include "World/AsdaClanFlag.h"          // композитор эмблемы клана (бейк 128×64)
#include "Items/AsdaItemDbRows.h"        // FAsdaTitleRow / FAsdaTitleRankRow (title_list, title_rank)
#include "Items/AsdaItemTableSubsystem.h" // ★ Аудит №3 (A3): title_list/title_rank — только через менеджер
#include "Items/AsdaGradeColor.h"        // ★ единая палитра грейдов (общая с AAsdaLootActor)
#include "Data/AsdaMobNameColorRow.h"    // ★ цвет имени моба по разнице уровней (эталонная лесенка)
#include "AsdaNetSubsystem.h"            // SelfLevel / CharList — уровень локального игрока
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"        // ★ CVar-ключ отката индикатора «(Одержим)»
#include "SceneView.h"                  // ★ FSceneView::ProjectWorldToScreen — быстрый путь проекции плашки
#include "World/AsdaKingCrownSubsystem.h" // ★ 08.10.2026: эталонное состояние действия (8 — сидит) для точки плашки
#include "World/AsdaViewRangeSubsystem.h"  // ★ 08.11.2026: предел плашки = дальность обзора
#include "World/AsdaPlayerCharacter.h"     // ★ 24.10.2026: подъём тела клипом прыжка (GetOverheadLiftZ)
#include "World/AsdaCombatTrace.h"        // ★ 18.10.2026: журнал боя — смена отображаемого HP на плашке, показ/скрытие полосы
#include "Npc/AsdaMobActor.h"             // ★ 18.10.2026: журнал боя — rid моба-владельца плашки

DEFINE_LOG_CATEGORY_STATIC(LogAsdaNameplate, Log, All);

#if ASDA_COMBAT_TRACE
// ★ Журнал боя (18.10.2026): чья плашка — «моб rid=N 'Имя'» / «игрок 'Имя'» / «NPC 'Имя'». Только чистые геттеры.
static FString AsdaCtNpWho(const UAsdaNameplateComponent* NP)
{
	if (!NP) { return TEXT("?"); }
	if (const AAsdaMobActor* M = Cast<AAsdaMobActor>(NP->GetOwner()))
	{
		return FString::Printf(TEXT("моб rid=%d '%s'"), M->NetRenderId, *NP->DisplayName);
	}
	const TCHAR* K = (NP->Kind == EAsdaNameplateKind::Player) ? TEXT("игрок") : (NP->Kind == EAsdaNameplateKind::Npc ? TEXT("NPC") : TEXT("моб"));
	return FString::Printf(TEXT("%s '%s'"), K, NP->DisplayName.IsEmpty() ? *GetNameSafe(NP->GetOwner()) : *NP->DisplayName);
}

// ★ Журнал боя (волна 2, 18.10.2026): «наша» ли плашка — уровень 1 только для нашего боя, чужие — уровень 2.
//   Моб — `IsEngaged(rid)` (цель цикла/задет нами); игрок — `IsUserRelevant(userIdx)` (я/цель PvP/участник боя),
//   userIdx — обратным поиском `FindUserIdxByActor` (свой павн → SelfUserNum); NPC и прочее — не наше.
//   Только чистые геттеры; зовётся лишь внутри аргумента ASDA_CTRACE_IF.
static bool AsdaCtNpOurs(const UAsdaNameplateComponent* NP)
{
	const AActor* Ow = NP ? NP->GetOwner() : nullptr;
	if (!Ow) { return false; }
	if (const AAsdaMobActor* M = Cast<AAsdaMobActor>(Ow)) { return AsdaCombatTrace::IsEngaged(M->NetRenderId); }
	if (NP->Kind != EAsdaNameplateKind::Player) { return false; }
	const UGameInstance* GI = Ow->GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const int32 Uidx = WS ? WS->FindUserIdxByActor(Ow) : -1;
	return Uidx >= 0 && AsdaCombatTrace::IsUserRelevant(Uidx);
}
#endif

// ★★★ Ключ отката НАШЕЙ метки «(Одержим)» — ДОБАВЛЕНИЕ поверх эталона (fix-original-defects).
//   1 = показывать суффикс под ником одержимого персонажа, 0 = не показывать.
//   ⚠ Ключ НЕЗАВИСИМ от эталонного цвета имени (asda.Possess.NameColor): владелец может оставить
//   любую комбинацию — только цвет, только метку, обе или ничего.
// ★★★ [ОРИГИНАЛ] 08.10.2026 (баг владельца п.3): сидящий игрок (состояние 8) — точка плашки в центре AABB (`GetPlateWorldPoint`).
//   1 — как оригинал (entity_char.c:9198-9227, game_main.c:4472); 0 — прежнее поведение порта (точка всегда верх + сдвиг).
static ASDA_DEV_CVAR(int32, CVarPlateSitCenter,
	TEXT("asda.Plate.SitCenter"), 1,
	TEXT("1 — у сидящего игрока (состояние 8) плашка над центром тела, как в оригинале (и как вывеска лавки); 0 — всегда над головой (прежнее)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarPossessTag,
	TEXT("asda.Nameplate.PossessTag"), 1,
	TEXT("Показывать НАШУ метку «(Одержим)» под ником одержимого персонажа (1) или нет (0). ")
	TEXT("В оригинале текстовой метки нет — это добавление порта (цвет имени идёт отдельным ключом)."),
	ECVF_Default);

// ★★★ [ОРИГИНАЛ] ЦВЕТ ИМЕНИ ОДЕРЖИМОГО. Эталон `gameUpdateTargetNameplates@0xB9C…`
//   (AsdaStory_game.c:20902 и :21139): `if (target->gap_8BE[3] == 2) CControl_SetField923(лейбл, −9764876);
//   else … −1;` — то есть имя красится в 0xFF6AFFF4 вместо обычного белого 0xFFFFFFFF.
//   `gap_8BE[3]` = `CCharacter+2241` = серверный `CUser+99027` = SoulState (доказательство —
//   `Reports/possess_broadcast_soulstate.md` §2.3). Это ПЕРЕНОС ЭТАЛОНА, а не наша выдумка,
//   поэтому по умолчанию 1. Ключ нужен, чтобы владелец мог оставить только текстовую метку.
static ASDA_DEV_CVAR(int32, CVarPossessNameColor,
	TEXT("asda.Possess.NameColor"), 1,
	TEXT("1 = красить имя одержимого игрока в эталонный 0xFF6AFFF4 (как оригинал); 0 = обычный цвет имени."),
	ECVF_Default);

// ★ Текст НАШЕГО индикатора одержимости. Отдельного loc-стринга под него в оригинале НЕТ (индикатора
//   тоже нет), новую loc-строку владелец не заводил — оставлено константой. Если появится строка в
//   таблице текста — заменить на подстановку (таблицу правит координатор, см. fix_possess_nameplate.md).
// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): слово — своя строка DT_GameText 990495 «Одержим» (язык сессии).
static constexpr int32 kPossessWordLoc = 990495;

// Канвас плашки и якорь окна внутри него — синхронно с Tools/asda_build_nameplates.py.
static const FVector2D kPlateCanvas(260.f, 160.f);
static const FVector2D kPlateAnchor(130.f, 80.f);   // якорь окна внутри канваса (см. генератор)

// ── Имена контролов окна TARGETMARK_MAN1 (JUI 1660), 1:1 с генератором ──────────────────────────
static const TCHAR* kCtrlName      = TEXT("TARGETMARK_MANNAME1");    // 1661 имя
// ★ НАШ контрол метки «(Одержим)» — заведён В АССЕТЕ генератором (ID в JUI у него нет: в оригинале
//   такой строки не существует). Имя одинаковое во всех плашках игроков (MAN и MAN1).
static const TCHAR* kCtrlPossess   = TEXT("TARGETMARK_POSSESS");     // метка одержимости (наша)
static const TCHAR* kCtrlNameMon   = TEXT("TARGETMARK_MONNAME");     // 1601 имя в окне цели-моба
static const TCHAR* kCtrlGuild     = TEXT("TARGETMARK_GUILDNAME1");  // 1662 название клана
static const TCHAR* kCtrlGuildIcon = TEXT("TARGETMARK_GUILDICON1");  // 1663 (в эталоне на неё НЕ рисуют)
static const TCHAR* kCtrlTitle     = TEXT("TargtMark_Man1Title");    // 1667 титул (2 слота)
static const TCHAR* kCtrlRank      = TEXT("TargetMark_Rank1");       // 1668 бейдж места в рейтинге
static const TCHAR* kCtrlMark      = TEXT("TARGETMARK_clanmark2");   // 1669 камп-марка (звание/эмблема)
static const TCHAR* kCtrlMarkR     = TEXT("TARGETMARK_clanmark2_R"); // 1670 бокс красного лагеря
static const TCHAR* kCtrlMarkB     = TEXT("TARGETMARK_clanmark2_B"); // 1671 бокс синего лагеря
static const TCHAR* kCtrlMarkG     = TEXT("TARGETMARK_clanmark2_G"); // 1672 бокс зелёного лагеря
static const TCHAR* kCtrlMarkS1    = TEXT("TARGETMARK_clanmark2_1673"); // 1673 обводка полосы лагеря 6×24
static const TCHAR* kCtrlMarkS2    = TEXT("TARGETMARK_clanmark2_1674"); // 1674 обводка иконки 24×24
// ★ Слои ЭМБЛЕМЫ клана (композиция бейка CMiniMap_BakeIconToRenderTarget БЕЗ render-target):
//   полотно(Clear-цвет) → контур-вымпел(тинт BorderColor) → знак(тинт SignColor). См. генератор.
static const TCHAR* kCtrlFlagBg     = TEXT("TARGETMARK_clanmark2_FLAGBG");
static const TCHAR* kCtrlFlagBorder = TEXT("TARGETMARK_clanmark2_FLAGBORDER");
static const TCHAR* kCtrlFlagSign   = TEXT("TARGETMARK_clanmark2_FLAGSIGN");

// Иконка ЗВАНИЯ ВОЙНЫ: Faction_Grade.tex 256×256, сетка 24 px, 8 столбцов
//   (эталон uiGrid_CellPosFromIndex@0xCA1C20: x = 24*(i%8), y = 24*(i/8)).
// ★ Аудит №3 (A8): КОРОТКОЕ имя JUI-атласа — полный путь строит `LoadUITexture`.
static const FName kFactionGradeTexName(TEXT("faction_grade"));
static constexpr float kGradeCell = 24.f;
static constexpr int32 kGradeCols = 8;
// Размеры отрисовки на камп-марке (эталон DrawTextureRect): иконка звания 24×24, эмблема клана 64×42.
static const FVector2D kMarkSize(24.f, 24.f);
// Базовый размер бейджа рейтинга при отсутствии записи (эталон: badgeWidth=32, badgeHeight=35).
static const FVector2D kBadgeDefault(32.f, 35.f);

// ★ Цвет имени NPC (эталон −8818 = 0xFFFFDD8E) — тёплый кремово-золотой, НЕ белый.
const FColor UAsdaNameplateComponent::NpcNameColor(0xFF, 0xDD, 0x8E, 0xFF);

// ── ★ ЦВЕТ ИМЕНИ МОБА ПО РАЗНИЦЕ УРОВНЕЙ ───────────────────────────────────────────────────────
// Таблица правится в редакторе: /Game/AsdaAssets/Data/Mob/DT_MobNameColors (FAsdaMobNameColorRow).
// Пока ассета нет (или строка не нашлась) — работает ЭТАЛОННЫЙ ВСТРОЕННЫЙ фолбэк, чтобы порт
// никогда не «терял» механику из-за отсутствия таблицы. Полное описание — в AsdaMobNameColorRow.h.
static const TCHAR* kMobNameColorDT = UAsdaNameplateComponent::kMobNameColorsPath;   // путь — объявление в шапке (15.09.2026, dt_gc_hold)

// Эталонный фолбэк (sRGB ARGB как в клиенте).
static bool MobNameColorFallback(int32 Diff, int32 Aggression, FColor& Out)
{
	switch (Aggression)
	{
	case 2: Out = FColor(0xE2, 0x4A, 0xCB, 0xFF); return true;   // −1946933
	case 3: Out = FColor(0xC3, 0xA8, 0x68, 0xFF); return true;   // −3954584
	case 1: case 4: case 5: break;
	default: return false;                                       // эталон цвет НЕ трогает
	}
	if (Diff >= 7)                    { Out = FColor(0x80, 0x80, 0x80, 0xFF); return true; }  // −8355712
	if (Diff >= 2  && Diff <= 6)      { Out = FColor(0x00, 0xFF, 0x00, 0xFF); return true; }  // −16711936
	if (Diff >= -1 && Diff <= 1)      { Out = FColor(0xFF, 0xFF, 0xFF, 0xFF); return true; }  // −1
	if (Diff >= -5 && Diff <= -2)     { Out = FColor(0xFF, 0xA2, 0x00, 0xFF); return true; }  // −24064
	if (Diff <= -6)                   { Out = FColor(0xFF, 0x00, 0x00, 0xFF); return true; }  // −65536
	return false;
}

FLinearColor UAsdaNameplateComponent::ResolveMobNameColor(int32 InMobLevel, int32 InPlayerLevel,
	int32 InAggression, bool& bOutHasColor, int32& OutDiff)
{
	// ★ Эталон: levelDiff = уровень ИГРОКА − уровень МОБА (плюс ⇒ моб слабее).
	OutDiff = InPlayerLevel - InMobLevel;
	bOutHasColor = false;
	FColor Srgb = FColor::White;

	// ★ Аудит №4 (B2): свой ленивый кэш заменён общей точкой `AsdaDt::Cached`.
	if (const UDataTable* DT = AsdaDt::Cached(kMobNameColorDT))
	{
		// 1) Ветка по агрессии (фиксированные цвета 2/3) — приоритет над лесенкой.
		TArray<FAsdaMobNameColorRow*> Rows;
		DT->GetAllRows<FAsdaMobNameColorRow>(TEXT("ResolveMobNameColor"), Rows);
		for (const FAsdaMobNameColorRow* R : Rows)
		{
			if (R && R->AggressionOverride == InAggression)
			{
				Srgb = R->NameColor; bOutHasColor = true;
				return FLinearColor(Srgb);   // FColor→FLinearColor = sRGB→linear (закон ui-srgb-gamma-rule)
			}
		}
		// 2) Лесенка по разнице уровней — только для «обычных» типов агрессии 1/4/5.
		if (InAggression == 1 || InAggression == 4 || InAggression == 5)
		{
			for (const FAsdaMobNameColorRow* R : Rows)
			{
				if (R && R->AggressionOverride < 0 && OutDiff >= R->MinDiff && OutDiff <= R->MaxDiff)
				{
					Srgb = R->NameColor; bOutHasColor = true;
					return FLinearColor(Srgb);
				}
			}
		}
		if (Rows.Num() > 0) { return FLinearColor::White; }   // таблица есть, но правило не подошло
	}
	// 3) Встроенный эталонный фолбэк.
	bOutHasColor = MobNameColorFallback(OutDiff, InAggression, Srgb);
	return FLinearColor(Srgb);
}

int32 UAsdaNameplateComponent::ResolveSelfLevel(const UObject* WorldCtx)
{
	const UWorld* W = GEngine ? GEngine->GetWorldFromContextObject(WorldCtx, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return 0; }
	if (Net->SelfLevel > 0) { return (int32)Net->SelfLevel; }
	// ★ Фолбэк на запись char-select: SelfLevel сидируется из 4005-self @+40, но приходит не всегда
	//   (живой тест 2026-08-03 в меню смерти — там же и найден этот путь).
	// ★ 30.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Possess.BodyIdentity`): общей точкой — при одержимости своя запись с тем же номером слота
	//   принадлежит другому персонажу (уровень тела — `SelfLevel` из блока 1017, выше).
	if (const FAsdaCharRecord* R = AsdaSelf::FindSelectedCharRecord(Net)) { return (int32)R->Level; }
	return 0;
}

UAsdaNameplateComponent::UAsdaNameplateComponent()
{
	// ★ ТИК ОБЯЗАТЕЛЕН: Screen-space UWidgetComponent проецирует и рисует плашку именно в TickComponent.
	//   С bCanEverTick=false плашка не появляется вообще (симптом: виджет есть, на экране пусто).
	PrimaryComponentTick.bCanEverTick = true;
	// Плашка не должна ловить клики и отбрасывать тень.
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCastShadow(false);
	// Экранное пространство (как в оригинале — плашка не уменьшается с расстоянием); в редакторе
	//   переключаемся на World в OnRegister, иначе её не видно в вьюпорте.
	SetWidgetSpace(EWidgetSpace::Screen);
	SetDrawAtDesiredSize(false);
	SetDrawSize(kPlateCanvas);
	// ★ ПИВОТ 1:1 С ЭТАЛОНОМ. Эталон (nameplate_positioning_re.md) ставит ЯКОРЬ окна в
	//   (screenX − ширина/2, screenY − 50): т.е. якорь на 50 ЭКРАННЫХ пикселей ВЫШЕ проекции головы.
	//   Якорь внутри нашего канваса = (130,80) (см. генератор), значит пивот компонента = (130, 80+50)/канвас.
	SetPivot(FVector2D(kPlateAnchor.X / kPlateCanvas.X, (kPlateAnchor.Y + 50.f) / kPlateCanvas.Y));
	SetTickWhenOffscreen(true);
	bReceivesDecals = false;
	// ★★★ [ОРИГИНАЛ] 30.10.2026 (asda.Overhead.LateProject, разбор — `World/AsdaOverheadLate.h`): тик ПОСЛЕ обновления камеры
	//   кадра — плашка проецируется той камерой, которой кадр рисуется (у оригинала `cameraUpdate` → `npcNameplateUpdateLoop`).
	//   Прежняя группа — TG_DuringPhysics (умолчание компонента): камера прошлого кадра. Ключ сверяется в OnRegister/TickComponent.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

// Группа тика плашки по ключу asda.Overhead.LateProject (смена ключа действует со следующего кадра).
static ETickingGroup AsdaPlateTickGroup()
{
	return AsdaOverheadLate::IsOn() ? TG_PostUpdateWork : TG_DuringPhysics;
}

const TCHAR* UAsdaNameplateComponent::WidgetPathForKind() const
{
	switch (Kind)
	{
	case EAsdaNameplateKind::Mob:
		return TEXT("/Game/AsdaAssets/UI/GameMain/WBP_NAMEPLATE_MON.WBP_NAMEPLATE_MON_C");
	case EAsdaNameplateKind::Player:
	case EAsdaNameplateKind::Npc:
	default:
		// NPC: у них нет своей плашки-окна в JUI (эталон рисует только имя контролом сущности),
		//   поэтому берём MAN1 и гасим всё, кроме имени (HP/гильдия/титул/иконки выключены).
		return TEXT("/Game/AsdaAssets/UI/GameMain/WBP_NAMEPLATE_MAN1.WBP_NAMEPLATE_MAN1_C");
	}
}

void UAsdaNameplateComponent::OnRegister()
{
	// Цвет имени NPC — фиксированный эталонный (если не переопределён в редакторе).
	if (Kind == EAsdaNameplateKind::Npc && NameColor == FLinearColor::White)
	{
		NameColor = FLinearColor(NpcNameColor);
	}
	// Гориз. поправка окна MON (контролы центрированы вокруг +51 px от якоря окна — эталон/JUI).
	if (Kind == EAsdaNameplateKind::Mob && FMath::IsNearlyZero(ExtraScreenOffsetX)) { ExtraScreenOffsetX = 51.f; }
	// В РЕДАКТОРЕ (не игровой мир) — World-space, иначе плашку не видно в вьюпорте сцены.
	const UWorld* W = GetWorld();
	const bool bGame = (W && W->IsGameWorld());
	// ★★★ ОДИН ВИДЖЕТ НА ПЛАШКУ (правка 07.09.2026, cpu_nameplate_report.md; ключ asda.Plate.SingleWidget).
	//   ДО правки на КАЖДУЮ плашку жили ДВА UUserWidget: внутренний виджет UWidgetComponent (его создаёт
	//   UWidgetComponent::InitWidget, WidgetComponent.cpp:1746) и наш экранный ScreenWidget. В ИГРЕ рисуется
	//   ТОЛЬКО второй: компонент скрыт (ниже SetVisibility(false)+SetHiddenInGame(true)), поэтому
	//   ShouldDrawWidget() (WidgetComponent.cpp:1369) у него всегда false и внутренний виджет не попадает
	//   ни в один кадр — но при этом он полноценно СОЗДАЁТСЯ: дерево контролов WBP, его Slate-виджет
	//   (TakeWidget) и память под всё это, на каждого моба/NPC в мире. В игре класс виджета не задаём —
	//   InitWidget тогда ничего не создаёт (ветка `WidgetClass && Widget == nullptr`), а UpdateWidget
	//   штатно кладёт в окно SNullWidget (WidgetComponent.cpp:1919) — путь предусмотрен движком.
	//   В РЕДАКТОРЕ класс остаётся: там компонент рисуется в 3D и превью плашки нужно.
	const bool bSingleWidget = bGame && UAsdaVisibilitySubsystem::PlateSingleWidget();
	if (bSingleWidget)
	{
		if (GetWidgetClass() != nullptr) { SetWidgetClass(nullptr); }
		if (GetUserWidgetObject() != nullptr) { SetWidget(nullptr); }   // остаток прошлой регистрации
	}
	// Класс виджета по типу — до Super, чтобы InitWidget создал нужный инстанс.
	else if (UClass* Cls = LoadClass<UUserWidget>(nullptr, WidgetPathForKind()))
	{
		if (GetWidgetClass() != Cls) { SetWidgetClass(Cls); }
	}
	// В РЕДАКТОРЕ — World-space превью (плашка видна в сцене). В ИГРЕ рендер самого компонента НЕ нужен:
	//   рисуем отдельный viewport-виджет с ручной проекцией (как эталон), см. UpdateScreenPosition.
	SetWidgetSpace(EWidgetSpace::World);
	if (!bGame)
	{
		SetDrawSize(kPlateCanvas);
		const float S = 0.5f / FMath::Max(FMath::Abs(GetComponentScale().Z), 1e-4f);
		SetRelativeScale3D(FVector(S, S, S));
	}
	Super::OnRegister();
	SetTickGroup(AsdaPlateTickGroup());   // ★ 30.10.2026: после обновления камеры (asda.Overhead.LateProject)
	if (bGame)
	{
		SetVisibility(false);          // 3D-рендер плашки в игре выключен (её рисует ScreenWidget)
		SetHiddenInGame(true);
		EnsureScreenWidget();
	}
	ApplyHeight();
	RefreshNameplate();
}

void UAsdaNameplateComponent::InitWidget()
{
	Super::InitWidget();
	RefreshNameplate();
}

#if WITH_EDITOR
void UAsdaNameplateComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Правки в редакторе применяются сразу (имя/HP/высота) — editor-friendly.
	if (UClass* Cls = LoadClass<UUserWidget>(nullptr, WidgetPathForKind()))
	{
		if (GetWidgetClass() != Cls) { SetWidgetClass(Cls); InitWidget(); }
	}
	ApplyHeight();
	RefreshNameplate();
}
#endif

// Д13 — общая точка «высота из данных / расчёт по капсуле» (обоснование в заголовке).
void UAsdaNameplateComponent::ApplyHeightFromData(float H)
{
	if (H > 0.f)
	{
		bUseFixedWorldHeight = true;   // габариты меша/капсулы не участвуют — так у эталона (пет/NPC/моб)
		WorldHeight = H;
	}
	else
	{
		// Эталонных данных нет — прежний расчёт по верху капсулы-хитбокса.
		bUseFixedWorldHeight = false;
	}
	ApplyHeight();
}

void UAsdaNameplateComponent::ApplyHeight()
{
	// ★ ВЫСОТА ПО ЭТАЛОНУ: headY = posY + H + K, где H = «высота сущности» (helper_getSumF@0x97D6D0 =
	//   centerY + halfY её AABB), K — тип-смещение (моб +50, постоянная плашка игрока +10, NPC 0).
	//   ВАЖНО: H в эталоне — КОНСТАНТА ИЗ ДАННЫХ, а не габариты модели: AABB собирается из
	//   heightParam (npc_list.BIN +0x68, −1 ⇒ 150; player — жёстко halfY = 75; пет — 88/77).
	//   Поэтому у NPC/петов владелец включает bUseFixedWorldHeight + WorldHeight (AAsdaNpcActor::
	//   ApplyNameplateHeight), и ветка «верх капсулы» ниже для них НЕ работает.
	//   Ветка по капсуле остаётся для сущностей, у которых эталонной константы нет (лут, мобы — их высоту
	//   юзер уже принял; тип-функции 1/3/5 — только при ключе asda.Npc.StaticPlateOnModel 0, иначе верх модели, 29.10.2026).
	float ScaleZ = 1.f;
	if (const USceneComponent* P = GetAttachParent()) { ScaleZ = FMath::Abs(P->GetComponentScale().Z); }
	else if (const AActor* A = GetOwner()) { ScaleZ = FMath::Abs(A->GetActorScale3D().Z); }
	ScaleZ = FMath::Max(ScaleZ, 1e-4f);

	// ★ ЕДИНАЯ ЛОГИКА ВЫСОТЫ ПЛАШКИ (юзер 2026-07-29): ВЕРХ КОЛЛИЗИИ (капсула-хитбокс пикинга) + 100.
	//   Одинаково для мобов/NPC/игроков и не зависит от размера модели — капсула уже подогнана под меш.
	//   Фолбэк (капсулы нет): верх меш-габаритов + 100. Мировые единицы; масштаб владельца компенсируется.
	// ПОВЕДЕНИЕ НЕ МЕНЯЛОСЬ: при bUseFixedWorldHeight (высота ИЗ ДАННЫХ) прибавка NameplateTopOffset
	//   не делается; расчёт по капсуле/мешу вынесен в ComputeHitboxTopZ() — им же пользуется облачко.
	const float TopWorldZ = bUseFixedWorldHeight ? 0.f : ComputeHitboxTopZ();
	const float WorldZ = (TopWorldZ > 1.f) ? (TopWorldZ + NameplateTopOffset) : WorldHeight;
	SetRelativeLocation(FVector(0.f, 0.f, WorldZ / ScaleZ));
}

// Верх хитбокса (капсула пикинга; фолбэк — габариты меша) относительно origin владельца. 0 = нет данных.
float UAsdaNameplateComponent::ComputeHitboxTopZ() const
{
	const AActor* A = GetOwner();
	if (!A) { return 0.f; }
	const float BaseZ = A->GetActorLocation().Z;
	float TopWorldZ = 0.f;
	// 1) Капсула пикинга — основной источник (её верх = «макушка» сущности для UI).
	TArray<UCapsuleComponent*> Caps;
	A->GetComponents<UCapsuleComponent>(Caps);
	for (const UCapsuleComponent* Cap : Caps)
	{
		if (!Cap || !Cap->IsRegistered()) { continue; }
		const float Top = (Cap->GetComponentLocation().Z + Cap->GetScaledCapsuleHalfHeight()) - BaseZ;
		TopWorldZ = FMath::Max(TopWorldZ, Top);
	}
	// 2) Фолбэк — меш-габариты (у сущностей без хитбокса).
	if (TopWorldZ <= 1.f)
	{
		TArray<UMeshComponent*> Meshes;
		A->GetComponents<UMeshComponent>(Meshes);
		for (const UMeshComponent* M : Meshes)
		{
			if (!M || !M->IsRegistered()) { continue; }
			const FBoxSphereBounds B = M->Bounds;
			TopWorldZ = FMath::Max(TopWorldZ, (B.Origin.Z + B.BoxExtent.Z) - BaseZ);
		}
	}
	return TopWorldZ;
}

// ★ Верх AABB сущности (мировые единицы, ОТНОСИТЕЛЬНО origin владельца). Ровно та база, на которую
//   эталон сажает и плашку имени, и облачко чата (helper_getSumF@0x97D6D0 = centerY + halfY).
//   bUseFixedWorldHeight ⇒ величина взята ИЗ ДАННЫХ (npc_list +0x68, Mob list +0x84, игрок 150) и
//   лежит в WorldHeight; иначе — верх капсулы пикинга, фолбэк — верх габаритов меша.
FVector UAsdaNameplateComponent::GetPlateWorldPoint() const
{
	// ★ 24.10.2026 [ПОРТ]: в прыжке (донор AsdaFableClassic) тело поднимает кость корня клипа, актор остаётся на земле —
	//   вся плашка (HP, ник, звания, клан, значки гильдии/звания/рейтинга — один виджет) поднимается на ту же величину.
	//   Ноль вне прыжка и при ключе `asda.Jump.OverheadFollow 0` (`AAsdaPlayerCharacter::GetOverheadLiftZ`).
	const AAsdaPlayerCharacter* JumpCh = Cast<AAsdaPlayerCharacter>(GetOwner());
	// ★★★ [ОРИГИНАЛ] 06.11.2026 (asda.Overhead.MountLift): плюс высота транспорта всадника — и в ветке сидящего (эталон прибавляет её
	//   после выбора базы). От этой же точки считаются проекция и перекрытие (`EvaluateText`).
	const FVector Lift(0.f, 0.f, (JumpCh ? JumpCh->GetOverheadLiftZ() : 0.f) + AsdaOverheadOcclusion::MountLiftZ(GetOwner()));
	if (Kind == EAsdaNameplateKind::Player && CVarPlateSitCenter.GetValueOnGameThread() != 0)
	{
		const AActor* Owner = GetOwner();
		if (Owner && UAsdaKingCrownSubsystem::EtalonActionStateOf(Owner) == 8)
		{
			// Центр AABB: origin актора = земля, AABB стоит на земле ⇒ центр = земля + высота/2 (как у вывески лавки).
			FVector P = Owner->GetActorLocation();
			P.Z += GetEntityTopZ() * 0.5f;
			return P + Lift;
		}
	}
	return GetComponentLocation() + Lift;
}

float UAsdaNameplateComponent::GetEntityTopZ() const
{
	if (bUseFixedWorldHeight) { return WorldHeight; }
	const float Top = ComputeHitboxTopZ();
	return (Top > 1.f) ? Top : WorldHeight;
}

// ★★★ ВЕРХ ВИДИМОЙ ЧАСТИ ПЛАШКИ В ЭКРАННЫХ ЕДИНИЦАХ (обоснование — в шапке объявления).
//   Экранный сдвиг окна: `TopLeft.Y = screenY/DPI − 50 − kPlateAnchor.Y` (см. UpdateScreenPosition),
//   контрол имени лежит в канвасе на `SlotY` ⇒ верх строки имени выше проекции якоря на
//   `50 + kPlateAnchor.Y − SlotY`. Для MAN1 генератор кладёт имя на `ANCHOR_Y + 16` = 96 ⇒ 34;
//   для MON — на `ANCHOR_Y + 20` = 100 ⇒ 30. Числа не зашиты: читаются у ЖИВОГО контрола.
float UAsdaNameplateComponent::GetPlateTopScreenOffset() const
{
	const bool bMob = (Kind == EAsdaNameplateKind::Mob);
	// Фолбэк — раскладка генератора Tools/asda_build_nameplates.py (имя: MAN1 +16, MON +20).
	float SlotY = kPlateAnchor.Y + (bMob ? 20.f : 16.f);
	const UUserWidget* W = ScreenWidget ? ScreenWidget.Get() : GetUserWidgetObject();
	if (W)
	{
		if (const UWidget* C = W->GetWidgetFromName(FName(bMob ? kCtrlNameMon : kCtrlName)))
		{
			if (const UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(C->Slot))
			{
				SlotY = S->GetPosition().Y;
			}
		}
	}
	return 50.f + kPlateAnchor.Y - SlotY;
}

// ★ ДИАГНОСТИКА: фактический прямоугольник строки имени (абсолютные Slate-координаты).
bool UAsdaNameplateComponent::GetNameAbsRect(FVector2D& OutPos, FVector2D& OutSize) const
{
	const UUserWidget* W = ScreenWidget ? ScreenWidget.Get() : GetUserWidgetObject();
	if (!W) { return false; }
	const bool bMob = (Kind == EAsdaNameplateKind::Mob);
	const UWidget* C = W->GetWidgetFromName(FName(bMob ? kCtrlNameMon : kCtrlName));
	if (!C) { return false; }
	const FGeometry& G = C->GetCachedGeometry();
	OutPos = G.GetAbsolutePosition();
	OutSize = G.GetAbsoluteSize();
	return true;
}

void UAsdaNameplateComponent::SetControlText(const TCHAR* CtrlName, const FString& Text, bool bWantVisible)
{
	// В игре плашка = ScreenWidget (ручная проекция), в редакторе — виджет самого компонента.
	UUserWidget* W = ScreenWidget ? ScreenWidget.Get() : GetUserWidgetObject();
	if (!W) { return; }
	UWidget* C = W->GetWidgetFromName(FName(CtrlName));
	if (!C)
	{
		UE_LOG(LogAsdaNameplate, Warning, TEXT("[плашка] контрол '%s' не найден в виджете %s"),
			CtrlName, *W->GetClass()->GetName());
		return;
	}
	// Текст ставим НАПРЯМУЮ в UTextBlock (виджеты плашек генерируются как TextBlock);
	//   RichText-путь оставлен фолбэком для контролов с разметкой.
	if (UTextBlock* T = Cast<UTextBlock>(C))
	{
		T->SetText(FText::FromString(Text));
		T->SetColorAndOpacity(FSlateColor(NameColor));
	}
	else
	{
		UAsdaRichText::ApplyFormatted(C, Text);
	}
	C->SetVisibility(bWantVisible && !Text.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

// Текст с ЯВНЫМ цветом (гильдия/титул имеют свои цвета, отличные от имени).
void UAsdaNameplateComponent::SetControlTextColored(const TCHAR* CtrlName, const FString& Text, bool bWantVisible, const FLinearColor& Color)
{
	UUserWidget* W = ScreenWidget ? ScreenWidget.Get() : GetUserWidgetObject();
	if (!W) { return; }
	UWidget* C = W->GetWidgetFromName(FName(CtrlName));
	if (!C) { return; }
	if (UTextBlock* T = Cast<UTextBlock>(C))
	{
		T->SetText(FText::FromString(Text));
		T->SetColorAndOpacity(FSlateColor(Color));
	}
	else { UAsdaRichText::ApplyFormatted(C, Text); }
	C->SetVisibility(bWantVisible && !Text.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UAsdaNameplateComponent::SetControlVisible(const TCHAR* CtrlName, bool bWantVisible)
{
	UUserWidget* W = ScreenWidget ? ScreenWidget.Get() : GetUserWidgetObject();
	if (!W) { return; }
	if (UWidget* C = W->GetWidgetFromName(FName(CtrlName)))
	{
		C->SetVisibility(bWantVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UAsdaNameplateComponent::ApplyHpBars()
{
	UUserWidget* W = ScreenWidget ? ScreenWidget.Get() : GetUserWidgetObject();
	if (!W) { return; }
	const bool bMob = (Kind == EAsdaNameplateKind::Mob);
	const TCHAR* BackName = bMob ? TEXT("TARGETMARK_MONGAUGEBACK") : TEXT("TARGETMARK_HPBACK1");
	const TCHAR* Bars[4] = {
		bMob ? TEXT("TARGETMARK_MONGAUGE")  : TEXT("TARGETMARK_HP1"),
		bMob ? TEXT("TARGETMARK_MONGAUGE1") : nullptr,
		bMob ? TEXT("TARGETMARK_MONGAUGE2") : nullptr,
		bMob ? TEXT("TARGETMARK_MONGAUGE3") : nullptr,
	};
	// NPC — HP нет вовсе.
	const bool bWantHp = bShowHp && (Kind != EAsdaNameplateKind::Npc) && MaxHp > 0 && CurHp >= 0;
#if ASDA_COMBAT_TRACE
	{
		// Журнал боя: показ/скрытие HP-полосы — по смене видимости фона полосы (читаем ДО записи), не каждый вызов.
		const UWidget* CtBack = W->GetWidgetFromName(FName(BackName));
		const bool bCtWasShown = CtBack && CtBack->GetVisibility() != ESlateVisibility::Collapsed && CtBack->GetVisibility() != ESlateVisibility::Hidden;
		// Скрытие пишем только у полосы, которую мы уже рисовали (`HpSmooth.Shown >= 0`): у свежего виджета фон виден по
		//   умолчанию разметки, и без этого каждый спавн моба давал бы «СКРЫТА».
		if (CtBack && ((bWantHp && !bCtWasShown) || (!bWantHp && bCtWasShown && HpSmooth.Shown >= 0.f)))
		{
			ASDA_CTRACE_IF(AsdaCtNpOurs(this), EAsdaCT::HpPlate, TEXT("плашка %s: HP-полоса %s — HP %d/%d (%.1f%%)%s"),
				*AsdaCtNpWho(this), bWantHp ? TEXT("ПОКАЗАНА") : TEXT("СКРЫТА"), CurHp, MaxHp,
				MaxHp > 0 ? 100.0 * (double)CurHp / (double)MaxHp : 0.0,
				bWantHp ? TEXT("") : (!bShowHp ? TEXT(" (не цель / труп)") : (MaxHp <= 0 ? TEXT(" (max HP нет)") : TEXT(""))));
		}
	}
#endif
	SetControlVisible(BackName, bWantHp);
	if (!bWantHp)
	{
		for (const TCHAR* B : Bars) { if (B) { SetControlVisible(B, false); } }
		ApplyHpSecondGhost(W, Bars[0], 0.f, /*bWantHp=*/false);
		HpSmooth.Reset();   // ★ полоса скрыта — следующий показ должен быть мгновенным
		return;
	}
	// Эталон uiUpdateSegmentedExpBar: при segments==0 — одиночный режим (только первый бар, 0..maxHp).
	//   При segments>0 — «жизни»: полные сегменты показаны целиком, текущий — частично.
	// ★ Рисуем СГЛАЖЕННУЮ долю, а не мгновенную: цель задаётся здесь, догоняет её тик компонента.
	//   Ключ отката к оригиналу (мгновенный скачок): `asda.UI.BarSmoothSec 0`.
	HpSmooth.SetTarget((float)CurHp / (float)FMath::Max(1, MaxHp));
	const float Ratio = FMath::Clamp(HpSmooth.Shown, 0.f, 1.f);
	for (int32 i = 0; i < 4; ++i)
	{
		if (!Bars[i]) { continue; }
		UWidget* C = W->GetWidgetFromName(FName(Bars[i]));
		if (!C) { continue; }
		float Frac = 0.f;
		if (Segments <= 0) { Frac = (i == 0) ? Ratio : 0.f; }
		else
		{
			// ★ Мульти-жизни (эталон uiUpdateSegmentedExpBar): бары НАЛОЖЕНЫ друг на друга, поэтому
			//   одновременно видно максимум ДВА — текущая «жизнь» (частично) поверх предыдущей (полностью).
			//   Показывать все подряд (как было) — неверно: получалась сплошная полоса.
			const int32 Segs = FMath::Max(1, Segments);
			const float SegSize = 1.f / (float)Segs;
			const int32 CurSeg = FMath::Clamp((int32)FMath::FloorToInt(Ratio / SegSize), 0, Segs - 1);
			if (i == CurSeg)      { Frac = FMath::Clamp((Ratio - CurSeg * SegSize) / SegSize, 0.f, 1.f); }
			else if (i == CurSeg - 1) { Frac = 1.f; }   // предыдущая «жизнь» — фон под текущей
			else                  { Frac = 0.f; }
		}
		C->SetVisibility(Frac > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		// Ширина полосы: 100px базы = 100% (JUI Size 100×4).
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(C))
		{
			FVector2D Sz = S->GetSize();
			Sz.X = 100.f * Frac;
			S->SetSize(Sz);
		}
	}
	ApplyHpSecondGhost(W, Bars[0], Ratio, /*bWantHp=*/true);
}

// ★★★ РАСШИРЕНИЕ СВЕРХ ОРИГИНАЛА (просьба владельца 2026-08-24): подложка восстановления НА
//   ПЛАШКЕ НАД ГОЛОВОЙ. В оригинале её нет — окно `TARGETMARK_MAN1` несёт только заливку (1664)
//   и фон (1665), никакого третьего слоя (`Docs/UI/World/TARGETMARK_MAN1.md`). Поэтому виджет
//   создаём сами, кисть берём у заливки, цвет — тёмно-коричневый.
//   ⚠ Рисуем НЕ от нуля (как слой буффа на полосах игрока), а «хвостом» от текущего здоровья до
//   второго значения: на плашке слой лёг бы поверх заливки, а порядок рисования здесь задаётся
//   порядком в канвасе, и увести его под заливку нечем. Видимый результат тот же — выступающая
//   вправо часть, ровно она и видна у полос игрока.
//   Мульти-жизни (боссы) исключены: там полоса означает не долю здоровья, а сегмент.
void UAsdaNameplateComponent::ApplyHpSecondGhost(UUserWidget* W, const TCHAR* FillName, float Ratio, bool bWantHp)
{
	if (!bHpSecondGhost || !W || !FillName) { return; }
	// Подложка живёт в том же виджете, что и заливка; сменился виджет — создаём заново.
	if (HpGhostOwner.Get() != W) { HpGhostImage.Reset(); HpGhostOwner = W; }

	const float GhostRatio = (MaxHp > 0) ? FMath::Clamp((float)HpSecond / (float)MaxHp, 0.f, 1.f) : 0.f;
	const bool  bWant = bWantHp && Segments <= 0 && HpSecond > 0 && GhostRatio > Ratio + KINDA_SMALL_NUMBER;
	if (!bWant)
	{
		if (UImage* G = HpGhostImage.Get()) { G->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}
	UImage* Fill = Cast<UImage>(W->GetWidgetFromName(FName(FillName)));
	UCanvasPanelSlot* FS = Fill ? Cast<UCanvasPanelSlot>(AsdaUi::SlotOf(Fill)) : nullptr;
	if (!FS) { return; }
	UImage* G = HpGhostImage.Get();
	if (!G)
	{
		UPanelWidget* Parent = Fill->GetParent();
		if (!Parent) { return; }
		G = NewObject<UImage>(W, UImage::StaticClass(), TEXT("AsdaHpSecondGhost"));
		// ★ Кисть НЕ берём у заливки: она красная, и тинт поверх неё давал тёмно-красный.
		//   Пустая кисть = сплошной прямоугольник, который целиком красится нашим цветом —
		//   тем же `Box_Orange C8FFB300`, каким слой буффа нарисован на полосах игрока.
		FSlateBrush Plain;
		Plain.DrawAs = ESlateBrushDrawType::Image;
		Plain.TintColor = FSlateColor(FLinearColor::White);
		G->SetBrush(Plain);
		G->SetColorAndOpacity(FLinearColor::FromSRGBColor(HpSecondGhostColor));
		UPanelSlot* PS = Parent->AddChild(G);
		if (!Cast<UCanvasPanelSlot>(PS)) { return; }
		HpGhostImage = G;
	}
	if (UCanvasPanelSlot* GS = Cast<UCanvasPanelSlot>(G->Slot))
	{
		GS->SetAnchors(FS->GetAnchors());
		GS->SetAlignment(FS->GetAlignment());
		GS->SetZOrder(FS->GetZOrder());
		FVector2D P = FS->GetPosition();
		P.X += 100.f * Ratio;                       // начинается ровно там, где кончилась заливка
		GS->SetPosition(P);
		FVector2D Sz = FS->GetSize();
		Sz.X = 100.f * (GhostRatio - Ratio);
		GS->SetSize(Sz);
	}
	G->SetColorAndOpacity(FLinearColor::FromSRGBColor(HpSecondGhostColor));
	G->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UAsdaNameplateComponent::SetHpSecond(int32 InSecond)
{
	if (HpSecond == InSecond) { return; }
	ASDA_CTRACE_IF(AsdaCtNpOurs(this), EAsdaCT::HpPlate, TEXT("плашка %s: подложка восстановления %d → %d (HP %d/%d)"),
		*AsdaCtNpWho(this), HpSecond, InSecond, CurHp, MaxHp);
	HpSecond = InSecond;
	ApplyHpBars();
}

// ── Имена сущностей из DataTable (единая точка; ассеты импортированы из локализованных данных игры) ──
// ★★★ Аудит №4 (B2). Свой `LoadNameTable` + два `static TWeakObjectPtr` заменены общей точкой
//   `AsdaDt::Cached`. ⚠ САМИ ЭТИ ФУНКЦИИ ОСТАЮТСЯ СТАТИЧЕСКИМИ И НЕ ИДУТ В ПОДСИСТЕМУ: их зовёт
//   `OnConstruction` ЗАПЕЧЁННЫХ акторов, то есть редактор вне PIE, где `UGameInstance` не
//   существует — попытка достать оттуда `UAsdaItemTableSubsystem` вернула бы nullptr и стёрла бы
//   имена у всех NPC/мобов на карте (проверено аудитом отдельно).
// ★ Волна 3 п.14 (15.09.2026): тела переехали в `AsdaEntityText` (построчно); здесь — делегаты для Blueprint.

FString UAsdaNameplateComponent::LookupNpcName(int32 NpcId)
{
	return AsdaEntityText::NpcName(NpcId);
}

FString UAsdaNameplateComponent::LookupMobName(int32 MobId, const FString& ModelName)
{
	return AsdaEntityText::MobName(MobId, ModelName);
}

// ★ Палитра грейдов (эталон: drop-палитра CItemManager_GetDropColor@0x9C45C0; общая для титулов/
//   предметов/квестов). Сама таблица — в Items/AsdaGradeColor.h, ОДНА на весь порт.
//   Было: локальные 9 записей + Clamp(0,8) → грейд 9 красился голубым (грейд 8) вместо светло-серого.
FLinearColor UAsdaNameplateComponent::GradeColor(int32 Grade)
{
	return FAsdaGradeColor::Get(Grade);
}

// ★ Аудит №3 (A3): `title_list` грузит МЕНЕДЖЕР (таблица в его манифесте), и у неё есть
//   КЛЮЧЕВОЙ ИНДЕКС по полю `Id` — типизированный геттер `GetTitle` даёт тот же результат,
//   что прежние ДВА пути (FindRow по имени строки + линейный перебор по полю Id), но без
//   второй копии строк в памяти и без линейного обхода на каждый промах.
bool UAsdaNameplateComponent::LookupTitle(const UObject* WorldCtx, int32 TitleId, FString& OutName, int32& OutGrade)
{
	OutName.Reset(); OutGrade = -1;
	if (TitleId < 0) { return false; }
	const UGameInstance* GI = WorldCtx ? UGameplayStatics::GetGameInstance(WorldCtx) : nullptr;
	const UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Items) { return false; }
	FAsdaTitleRow Row;
	if (!Items->GetTitle(TitleId, Row)) { return false; }
	OutName = Row.Name; OutGrade = Row.ColorIndex;
	return true;
}

void UAsdaNameplateComponent::SetTitleById(int32 TitleId)
{
	FString Nm; int32 Gr = -1;
	if (LookupTitle(this, TitleId, Nm, Gr)) { TitleName = Nm; TitleGrade = Gr; }
	else                              { TitleName.Reset(); TitleGrade = -1; }
	RefreshNameplate();
}

// ★ ДВА СЛОТА ТИТУЛА (эталон NetHandler_UpdateCharGuildTitle@0xE9AD90): для каждого слота отдельно
//   ищем запись title_list, цвет — по её ColorIndex (грейд); пустой слот = пустая строка.
void UAsdaNameplateComponent::SetTitles(int32 InTitleId0, int32 InTitleId1)
{
	TitleId0 = InTitleId0; TitleId1 = InTitleId1;
	FString N; int32 G = -1;
	if (LookupTitle(this, TitleId0, N, G)) { TitleName  = N; TitleGrade  = G; } else { TitleName.Reset();  TitleGrade  = -1; }
	if (LookupTitle(this, TitleId1, N, G)) { TitleName1 = N; TitleGrade1 = G; } else { TitleName1.Reset(); TitleGrade1 = -1; }
	RefreshNameplate();
}

void UAsdaNameplateComponent::SetGuild(const FString& InGuildName)
{
	GuildName = InGuildName;
	RefreshNameplate();
}

// Клан целиком (эталон: пакет 4109 для ЧУЖОГО игрока, 4105 для своего, 4133 = сброс).
void UAsdaNameplateComponent::SetClan(int32 InClanId, const FString& InClanName, const FString& InClanRankName)
{
	ClanId = InClanId;
	// clanId == −1 ⇒ клана нет: эталон обнуляет строку (memset +2410) → контрол пустой и скрыт.
	GuildName     = (ClanId >= 0) ? InClanName : FString();
	ClanRankName  = (ClanId >= 0) ? InClanRankName : FString();
	if (ClanId < 0) { bClanFlagOn = false; ClanFlag = FAsdaClanFlagData(); }
	RefreshNameplate();
}

// Флаг (эмблема) клана — пакет 6542 (partyMemberUpdateHpState@0xBCDC00): flagOn + 40 байт дизайна.
void UAsdaNameplateComponent::SetClanFlag(bool bOn, const FAsdaClanFlagData& InFlag)
{
	bClanFlagOn = bOn;
	ClanFlag    = InFlag;
	RefreshNameplate();
}

void UAsdaNameplateComponent::SetCamp(int32 InCampId, int32 InFactionGrade)
{
	CampId = InCampId; FactionGrade = InFactionGrade;
	RefreshNameplate();
}

void UAsdaNameplateComponent::SetRank(int32 InRankValue, int32 InRankCat)
{
	RankValue = InRankValue; RankCat = InRankCat;
	RefreshNameplate();
}

// ★ Альтернатор эталона (game_main.c:1415): toggle=1 держится 30 000 мс, toggle=0 — 5 000 мс.
bool UAsdaNameplateComponent::IdleBlinkToggle(const UObject* WorldCtx)
{
	const UWorld* W = GEngine ? GEngine->GetWorldFromContextObject(WorldCtx, EGetWorldErrorMode::ReturnNull) : nullptr;
	const double T = W ? W->GetTimeSeconds() : 0.0;
	const double Phase = FMath::Fmod(T, 35.0);   // цикл 30 с (toggle=1) + 5 с (toggle=0)
	return Phase < 30.0;
}

// ── Титул: строка с ЦВЕТОМ КАЖДОГО СЛОТА (эталон: "<FC=aarrggbb>имя</F>" в самой строке) ─────────
static FString AsdaFcTag(const FLinearColor& C)
{
	const FColor S = C.ToFColor(/*bSRGB=*/true);
	return FString::Printf(TEXT("<FC=%02X%02X%02X%02X>"), S.A, S.R, S.G, S.B);
}

FString UAsdaNameplateComponent::BuildTitleMarkup() const
{
	// Цвет по грейду; пустой слот — пустая строка. Склейка через пробел, как sprintf("%s %s", …).
	auto Part = [](const FString& Nm, int32 Grade) -> FString
	{
		if (Nm.IsEmpty()) { return FString(); }
		const FLinearColor C = (Grade >= 0) ? GradeColor(Grade) : FLinearColor(FColor(0xB0, 0xFF, 0xB0, 0xFF));
		return AsdaFcTag(C) + Nm + TEXT("</F>");
	};
	const FString S0 = Part(TitleName, TitleGrade);
	const FString S1 = Part(TitleName1, TitleGrade1);
	if (S0.IsEmpty() && S1.IsEmpty()) { return FString(); }
	if (S0.IsEmpty()) { return S1; }
	if (S1.IsEmpty()) { return S0; }
	return S0 + TEXT(" ") + S1;
}

void UAsdaNameplateComponent::ApplyTitleMarkup()
{
	UUserWidget* W = ScreenWidget ? ScreenWidget.Get() : GetUserWidgetObject();
	if (!W) { return; }
	UWidget* C = W->GetWidgetFromName(FName(kCtrlTitle));
	if (!C) { return; }
	const FString Markup = BuildTitleMarkup();
	if (Markup.IsEmpty())
	{
		// Пусто → контрол скрыт (эталон: Visible=0 в JUI, текст не ставится).
		C->SetVisibility(ESlateVisibility::Collapsed);
		if (UTextBlock* T = Cast<UTextBlock>(C)) { T->SetText(FText::GetEmpty()); }
		else if (URichTextBlock* R = Cast<URichTextBlock>(C)) { R->SetText(FText::GetEmpty()); }
		return;
	}
	// Разметка «два цвета в одной строке» — только через rich-путь (единая точка формата порта).
	UWidget* Applied = UAsdaRichText::ApplyFormatted(C, Markup);
	if (URichTextBlock* R = Cast<URichTextBlock>(Applied ? Applied : C))
	{
		// ★ Плашка однострочная: autowrap в CanvasPanel «раздувает» слот (правило ui-autowrap-canvas-gotcha).
		R->SetAutoWrapText(false);
		R->SetJustification(ETextJustify::Center);   // JUI TextAlign=5 (центр)
		// ★ Шрифт и ОБВОДКА — как у остальных подписей плашки (генератор ставит их на TextBlock'ах:
		//   шрифт UI порта — arial/arialbd, см. AsdaUmgBuilder — + чёрный контур 1 px). Берём
		//   FSlateFontInfo прямо с контрола имени.
		if (const UTextBlock* NameT = Cast<UTextBlock>(W->GetWidgetFromName(FName(kCtrlName))))
		{
			R->SetDefaultFont(NameT->GetFont());
		}
		R->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else if (Applied)
	{
		Applied->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UAsdaNameplateComponent::SetSoulState(int32 InSoulState)
{
	if (SoulState == InSoulState) { return; }
	SoulState = InSoulState;
	RefreshNameplate();
}

// ★★★ МЕТКА «(Одержим)» ПОД НИКОМ — НАШЕ ДОБАВЛЕНИЕ ПОВЕРХ ЭТАЛОНА (закон fix-original-defects).
//   В оригинале одержимость показана ЦВЕТОМ ИМЕНИ (0xFF6AFFF4, см. RefreshNameplate), текстовой метки
//   там нет — это требование владельца 2026-09-02. Оба признака НЕЗАВИСИМЫ и сосуществуют:
//   `asda.Possess.NameColor` (эталонный цвет) и `asda.Nameplate.PossessTag` (эта метка).
//   Скобки БЕЛЫЕ, слово ФИОЛЕТОВОЕ — в ОДНОЙ строке через ту же rich-разметку <FC=AARRGGBB>, что и
//   титул (единый механизм цветного текста порта, ничего не изобретаем). Кегль = кегль имени − 1.
//   ★★★ ЗАКОН no-runtime-widget-fixes: контрол ЖИВЁТ В АССЕТЕ плашки (`TARGETMARK_POSSESS`,
//   генератор Tools/asda_build_nameplates.py). Раскладка: имя y16 h16, HPBACK y33 h6, метка y40 h16 —
//   ничего не перекрывается, геометрия ОРИГИНАЛЬНЫХ контролов не сдвинута. Здесь — только текст и
//   видимость, ровно как у имени/гильдии/титула. Гейт: только ИГРОК, SoulState==2, ключ включён.
void UAsdaNameplateComponent::ApplyPossessTag(UUserWidget* W)
{
	if (!W) { return; }
	UWidget* C = W->GetWidgetFromName(FName(kCtrlPossess));
	if (!C)
	{
		// Плашка не перегенерирована (старый ассет без контрола) — говорим вслух, а не молчим
		//   (закон false-negatives-method: «ничего не видно» не должно быть неотличимо от «выключено»).
		if (Kind == EAsdaNameplateKind::Player && SoulState == 2)
		{
			UE_LOG(LogAsdaNameplate, Warning,
				TEXT("[плашка] контрол метки '%s' не найден в %s — перегенерируйте плашки (asda_build_nameplates.py)"),
				kCtrlPossess, *W->GetClass()->GetName());
		}
		return;
	}

	const bool bWant = (Kind == EAsdaNameplateKind::Player)
		&& SoulState == 2
		&& CVarPossessTag.GetValueOnGameThread() != 0
		&& !DisplayName.IsEmpty();
	if (!bWant)
	{
		C->SetVisibility(ESlateVisibility::Collapsed);
		if (UTextBlock* T = Cast<UTextBlock>(C)) { T->SetText(FText::GetEmpty()); }
		else if (URichTextBlock* R = Cast<URichTextBlock>(C)) { R->SetText(FText::GetEmpty()); }
		return;
	}

	// Разметка: «(» и «)» белые, слово — фиолетовое (PossessWordColor). Тег FC = AARRGGBB (AsdaFcTag).
	const FString Markup =
		AsdaFcTag(FLinearColor::White) + TEXT("(") + TEXT("</F>")
		+ AsdaFcTag(FLinearColor::FromSRGBColor(PossessWordColor)) + FAsdaText::Loc(kPossessWordLoc) + TEXT("</F>")
		+ AsdaFcTag(FLinearColor::White) + TEXT(")") + TEXT("</F>");

	// Разметка «два цвета в одной строке» — только через rich-путь (та же единая точка, что у титула).
	//   Контрол создан ГЕНЕРАТОРОМ внутри WidgetTree, поэтому ApplyFormatted находит дерево и
	//   возвращает живой URichTextBlock (рантайм-NewObject этого гарантировать не мог).
	UWidget* Applied = UAsdaRichText::ApplyFormatted(C, Markup);
	URichTextBlock* Tag = Cast<URichTextBlock>(Applied ? Applied : C);
	if (!Tag)
	{
		if (Applied) { Applied->SetVisibility(ESlateVisibility::HitTestInvisible); }
		return;
	}
	// Шрифт и чёрная обводка — как у имени, кегль на 1 меньше. ★ В UE 5.8 FSlateFontInfo::Size — float,
	//   поэтому ОБА аргумента FMath::Max тоже float (шаблон однотипный, иначе C2782).
	if (const UTextBlock* NameT = Cast<UTextBlock>(W->GetWidgetFromName(FName(kCtrlName))))
	{
		FSlateFontInfo Font = NameT->GetFont();
		Font.Size = FMath::Max(1.f, Font.Size - 1.f);
		Tag->SetDefaultFont(Font);
	}
	Tag->SetAutoWrapText(false);                            // однострочный (правило ui-autowrap-canvas-gotcha)
	Tag->SetJustification(ETextJustify::Center);            // по центру, как имя (JUI TextAlign=5)
	Tag->SetVisibility(ESlateVisibility::HitTestInvisible); // клики по персонажу не съедаем
}

// ── Общие хелперы контролов ──────────────────────────────────────────────────────────────────────
FVector2D UAsdaNameplateComponent::GetBasePos(const TCHAR* CtrlName)
{
	const FName Key(CtrlName);
	if (const FVector2D* Found = BasePos.Find(Key)) { return *Found; }
	UUserWidget* W = ScreenWidget ? ScreenWidget.Get() : GetUserWidgetObject();
	if (!W) { return FVector2D::ZeroVector; }
	if (UWidget* C = W->GetWidgetFromName(Key))
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(C))
		{
			const FVector2D P = S->GetPosition();
			BasePos.Add(Key, P);
			return P;
		}
	}
	return FVector2D::ZeroVector;
}

void UAsdaNameplateComponent::SetControlPos(const TCHAR* CtrlName, const FVector2D& Pos)
{
	UUserWidget* W = ScreenWidget ? ScreenWidget.Get() : GetUserWidgetObject();
	if (!W) { return; }
	if (UWidget* C = W->GetWidgetFromName(FName(CtrlName)))
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(C)) { S->SetPosition(Pos); }
	}
}

void UAsdaNameplateComponent::SetControlTexture(const TCHAR* CtrlName, UTexture* Tex, const FBox2D& UV, const FVector2D& SizePx,
	const FLinearColor& Tint)
{
	UUserWidget* W = ScreenWidget ? ScreenWidget.Get() : GetUserWidgetObject();
	if (!W) { return; }
	UImage* Img = Cast<UImage>(W->GetWidgetFromName(FName(CtrlName)));
	if (!Img) { return; }
	if (!Tex)
	{
		Img->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	FSlateBrush B = Img->GetBrush();
	B.SetResourceObject(Tex);
	B.DrawAs = ESlateBrushDrawType::Image;
	B.SetImageSize(SizePx);
	B.SetUVRegion(FBox2f(FVector2f((float)UV.Min.X, (float)UV.Min.Y), FVector2f((float)UV.Max.X, (float)UV.Max.Y)));
	B.TintColor = FSlateColor(Tint);   // MODULATE текстура×цвет (эталон: TEXTUREFACTOR)
	Img->SetBrush(B);
	// Эталон перед DrawTextureRect зовёт vtable SetSize(W,H) — размер слота = размер рисунка.
	if (UCanvasPanelSlot* S = AsdaUi::SlotOf(Img)) { S->SetSize(SizePx); }
	Img->SetVisibility(ESlateVisibility::HitTestInvisible);
}

float UAsdaNameplateComponent::MeasureTextWidth(const TCHAR* CtrlName, const FString& Text) const
{
	if (Text.IsEmpty()) { return 0.f; }
	UUserWidget* W = ScreenWidget ? ScreenWidget.Get() : GetUserWidgetObject();
	if (!W || !FSlateApplication::IsInitialized()) { return 0.f; }
	UWidget* C = W->GetWidgetFromName(FName(CtrlName));
	FSlateFontInfo Font;
	if (const UTextBlock* T = Cast<UTextBlock>(C))              { Font = T->GetFont(); }
	else if (const URichTextBlock* R = Cast<URichTextBlock>(C)) { Font = R->GetDefaultTextStyle().Font; }
	if (!Font.HasValidFont()) { return 0.f; }
	// ★ СВЕДЕНО (`fix_helpers_dedup.md`): общий замер `AsdaUi::MeasureTextWidth` (Slate поднят и шрифт валиден — гварды выше).
	return AsdaUi::MeasureTextWidth(Text, Font, 0.f);
}

// ── §6 Раскладка детей: charNamePlate_LayoutControls@0xA4B4E0 ────────────────────────────────────
void UAsdaNameplateComponent::LayoutControls()
{
	if (Kind != EAsdaNameplateKind::Player) { return; }
	const FVector2D GuildBase = GetBasePos(kCtrlGuild);
	const FVector2D BadgeBase = GetBasePos(kCtrlRank);
	if (GuildBase.IsZero() && BadgeBase.IsZero()) { return; }   // виджет ещё не создан

	FVector2D Guild = GuildBase, Badge = BadgeBase;
	const bool bHasTitle = (TitleId0 >= 0 || TitleId1 >= 0);
	if (!bHasTitle)          { Guild.Y += 16.f; Badge.Y += 16.f; }
	if (ClanId == -1)        { Badge.Y += 16.f; }
	if (bShopOpen)           { Badge.Y -= 34.f; }
	if (!bHasTitle && ClanId == -1 && bShopOpen) { Badge.Y -= 5.f; }

	UpdateFactionUI();                       // эталон вызывает её ИЗ раскладки (до SetField74_75)
	SetControlPos(kCtrlGuild, Guild);
	SetControlPos(kCtrlRank,  Badge);
	// Эталон сохраняет позицию бейджа в gap_3E4[32/36] — её читает uiLevelBadgeUpdateTexture (центровка).
	BasePos.Add(FName(TEXT("__badgeLaidOut")), Badge);
	UpdateRankBadge();
}

// ── §4 Камп-марка / фракция: CharNamePlate_UpdateFactionUI@0x9DCED0 ─────────────────────────────
void UAsdaNameplateComponent::UpdateFactionUI()
{
	if (Kind != EAsdaNameplateKind::Player) { return; }
	auto HideAll = [this]()
	{
		for (const TCHAR* N : { kCtrlMark, kCtrlMarkR, kCtrlMarkB, kCtrlMarkG, kCtrlMarkS1, kCtrlMarkS2,
								kCtrlFlagBg, kCtrlFlagBorder, kCtrlFlagSign })
		{
			SetControlVisible(N, false);
		}
	};
	// Условие показа: лагерь ИЛИ флаг клана ИЛИ звание > 0. Мастер-тумблер оригинала (byte_15CB4E7,
	//   опция настроек) УБРАН по решению юзера 2026-07-30: всегда включено при наличии принадлежности.
	if (!(CampId != -1 || bClanFlagOn || FactionGrade > 0))
	{
		HideAll();
		return;
	}

	// Центровка марки: x = базаX − maxШирина/2 (эталон: +996 − maxW*0.5), y = базаY.
	//   maxW = max(ширина имени, ширина клана, ширина «титул0 титул1» БЕЗ разметки).
	float MaxW = MeasureTextWidth(kCtrlName, DisplayName);
	MaxW = FMath::Max(MaxW, MeasureTextWidth(kCtrlGuild, GuildName));
	if (TitleId0 >= 0 || TitleId1 >= 0)
	{
		FString Plain = TitleName;
		if (!TitleName1.IsEmpty()) { Plain = Plain.IsEmpty() ? TitleName1 : (Plain + TEXT(" ") + TitleName1); }
		MaxW = FMath::Max(MaxW, MeasureTextWidth(kCtrlName, Plain));
	}
	const FVector2D MarkBase = GetBasePos(kCtrlMark);
	SetControlPos(kCtrlMark, FVector2D(MarkBase.X - MaxW * 0.5f, MarkBase.Y));
	// В JUI 1670..1674 — ДЕТИ контейнера 1669 (едут с ним автоматически); в плоском UMG-канвасе
	//   сдвигаем каждого сателлита марки на ту же дельту от его авторской позиции.
	for (const TCHAR* N : { kCtrlMarkR, kCtrlMarkB, kCtrlMarkG, kCtrlMarkS1, kCtrlMarkS2 })
	{
		const FVector2D Base = GetBasePos(N);
		if (!Base.IsZero()) { SetControlPos(N, FVector2D(Base.X - MaxW * 0.5f, Base.Y)); }
	}

	// Что рисуем на марке (эталон, альтернатор g_bIdleBlinkToggle):
	//   toggle=1: есть флаг клана → ЭМБЛЕМА; иначе (лагерь есть и звание ≥ −1) → ИКОНКА ЗВАНИЯ.
	//   toggle=0: лагерь есть и звание ≥ −1 → ИКОНКА ЗВАНИЯ; иначе есть флаг → ЭМБЛЕМА.
	const bool bBlink     = IdleBlinkToggle(this);
	const bool bGradeOk   = (FactionGrade >= -1 && CampId != -1);
	const bool bWantGrade = bBlink ? (!bClanFlagOn && bGradeOk) : bGradeOk;
	const bool bWantFlag  = bBlink ? bClanFlagOn : (!bGradeOk && bClanFlagOn);

	// Слои эмблемы двигаются вместе с маркой (центровка выше двигает только 1669; слои — отдельные
	//   контролы канваса с той же авторской привязкой (-29,5) → сдвигаем на ту же дельту).
	const FVector2D MarkDelta(-MaxW * 0.5f, 0.f);
	auto MoveLayer = [this, &MarkDelta](const TCHAR* N)
	{
		const FVector2D Base = GetBasePos(N);
		if (!Base.IsZero()) { SetControlPos(N, Base + MarkDelta); }
	};
	auto HideFlagLayers = [this]()
	{
		SetControlVisible(kCtrlFlagBg, false);
		SetControlVisible(kCtrlFlagBorder, false);
		SetControlVisible(kCtrlFlagSign, false);
	};

	if (bWantGrade)
	{
		HideFlagLayers();
		int32 G = FactionGrade;
		if (G == 0 || G == -1) { G = 1; }   // эталон: 0/−1 → 1
		const int32 Cell = G - 1;
		// ★ Аудит №3 (A8/B8): JUI-атлас — только через менеджер (кэш по имени + refCount).
		UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
		if (UTexture2D* Tex = TexMgr ? TexMgr->LoadUITexture(kFactionGradeTexName) : nullptr)
		{
			// ★ ЛОГИЧЕСКИЙ размер атласа (координаты в данных — в пикселях исходного).
			const FIntPoint LogSize = AsdaUi::LogicalAtlasSize(Tex);
			const float Tw = (float)LogSize.X;
			const float Th = (float)LogSize.Y;
			const FVector2D UV0((Cell % kGradeCols) * kGradeCell / Tw, (Cell / kGradeCols) * kGradeCell / Th);
			SetControlTexture(kCtrlMark, Tex, FBox2D(UV0, UV0 + FVector2D(kGradeCell / Tw, kGradeCell / Th)), kMarkSize);
		}
	}
	else if (bWantFlag)
	{
		// ★ ЭМБЛЕМА КЛАНА СЛОЯМИ (композиция бейка CMiniMap_BakeIconToRenderTarget, БЕЗ render-target):
		//   RT-модель: Clear(BgColor) → контур-вымпел из ячейки 144×96 на весь RT 128×64 (×BorderColor)
		//   → знак из ячейки 64×64 в (27,14)+57×38 (×SignColor); на плашку идёт вырезка (23,12)+64×42.
		SetControlVisible(kCtrlMark, false);   // сам 1669 в режиме эмблемы не рисует (он для звания)
		// ★ ГЕЙТ БЕЙКА 1:1 (эталон world_map.c:10233-10236, CMiniMap_BakeIconToRenderTarget): бейк
		//   выполняется ТОЛЬКО когда ОБА индекса в границах таблиц (`a2 <= dword_2BE52CC && a2 >= 0` для
		//   знака И `a3 <= dword_2BE52D4 && a3 >= 0` для рамки) — Clear(BgColor) стоит ВНУТРИ гейта.
		//   Невалидный индекс ⇒ бейка нет вовсе ⇒ на плашке НЕ появляется цветное поле. Раньше порт
		//   рисовал красный FlagBg безусловно — при плохих данных получался «красный квад-плейсхолдер».
		FString BorderTexName, SignTexName;
		const bool bBorderIdxOk = UAsdaClanFlag::LookupBorderTex(ClanFlag.BorderIdx, BorderTexName);
		const bool bSignIdxOk   = UAsdaClanFlag::LookupSignTex(ClanFlag.SignIdx, SignTexName);
		if (!bBorderIdxOk || !bSignIdxOk)
		{
			HideFlagLayers();
			UE_LOG(LogAsdaNameplate, Warning,
				TEXT("[плашка] '%s': индексы флага вне таблиц (border=%d ok=%d, sign=%d ok=%d) — бейк пропущен (эталонный гейт)"),
				*DisplayName, ClanFlag.BorderIdx, bBorderIdxOk ? 1 : 0, ClanFlag.SignIdx, bSignIdxOk ? 1 : 0);
			goto FactionLabel37;   // LABEL_37 эталона: рамки/боксы обрабатываются в любом случае
		}
		// Полотно: сплошной цвет BgColor на весь контрол (вырезка целиком залита Clear-цветом).
		{
		UUserWidget* WW = ScreenWidget ? ScreenWidget.Get() : GetUserWidgetObject();
		if (UImage* Bg = WW ? Cast<UImage>(WW->GetWidgetFromName(FName(kCtrlFlagBg))) : nullptr)
		{
			FSlateBrush B = Bg->GetBrush();
			B.SetResourceObject(nullptr);
			B.DrawAs = ESlateBrushDrawType::Image;
			B.TintColor = FSlateColor(UAsdaClanFlag::ArgbToLinear(ClanFlag.BgColor));
			Bg->SetBrush(B);
			Bg->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		// Контур-вымпел: видимая часть ячейки бордера = суб-регион (вырезка (23,12)+64×42 от RT 128×64).
		UTexture2D* BorderTex = UAsdaClanFlag::LoadJuiTexture(BorderTexName);
		if (BorderTex)
		{
			const int32 Cell = ClanFlag.BorderIdx % 15;
			const float BaseX = (Cell % 3) * 144.f, BaseY = (Cell / 3) * 96.f;
			// Доли вырезки внутри ячейки: X 23/128..87/128 → 25.875..97.875 px ячейки; Y 12/64..54/64 → 18..81.
			const FBox2D UV(FVector2D((BaseX + 144.f * 23.f / 128.f) / 512.f, (BaseY + 96.f * 12.f / 64.f) / 512.f),
			                FVector2D((BaseX + 144.f * 87.f / 128.f) / 512.f, (BaseY + 96.f * 54.f / 64.f) / 512.f));
			SetControlTexture(kCtrlFlagBorder, BorderTex, UV, kMarkSize, UAsdaClanFlag::ArgbToLinear(ClanFlag.BorderColor));
		}
		else { SetControlVisible(kCtrlFlagBorder, false); }
		// Знак: ячейка 64×64 атласа инсигний целиком (гео-сжатие в 57×38 даёт слот генератора).
		UTexture2D* SignTex = UAsdaClanFlag::LoadJuiTexture(SignTexName);
		if (SignTex)
		{
			const FVector2D UV0((ClanFlag.SignIdx % 8) * 64.f / 512.f, (ClanFlag.SignIdx / 8) * 64.f / 512.f);
			const FBox2D UV(UV0, UV0 + FVector2D(64.f / 512.f, 64.f / 512.f));
			SetControlTexture(kCtrlFlagSign, SignTex, UV, FVector2D(21.4f, 21.7f), UAsdaClanFlag::ArgbToLinear(ClanFlag.SignColor));
		}
		else { SetControlVisible(kCtrlFlagSign, false); }
		MoveLayer(kCtrlFlagBg); MoveLayer(kCtrlFlagBorder); MoveLayer(kCtrlFlagSign);
		}
	}
	else
	{
		// Ни звания, ни эмблемы — марка видима (эталон SetVisible(+972,1)), но без картинки.
		SetControlVisible(kCtrlMark, false);
		HideFlagLayers();
	}

	// Обводки и цветные полосы лагеря (эталон LABEL_37).
FactionLabel37:
	SetControlVisible(kCtrlMarkS2, true);                 // 1674: обводка иконки — всегда при показе марки
	const bool bCamp = (CampId != -1);
	SetControlVisible(kCtrlMarkS1, bCamp);               // 1673: обводка полосы — только когда лагерь есть
	SetControlVisible(kCtrlMarkB, bCamp && CampId == 0); // 1671 синий
	SetControlVisible(kCtrlMarkR, bCamp && CampId == 1); // 1670 красный
	SetControlVisible(kCtrlMarkG, bCamp && CampId == 2); // 1672 зелёный
}

// ── §3 Бейдж места в рейтинге: uiLevelBadgeUpdateTexture@0xA4B270 ───────────────────────────────
// ★ Аудит №3 (A3): `title_rank` тоже держит менеджер. Типизированного геттера у неё НЕТ и быть
//   не может: поиск идёт НЕ по ключу, а по ДИАПАЗОНУ (эталон `findRecordByKeyInRange`:
//   `Cat == c && Min <= v <= Max`) — берём саму таблицу через `GetDataTable` и обходим как раньше.
bool UAsdaNameplateComponent::LookupRankBadge(const UObject* WorldCtx, int32 InRankValue, int32 InRankCat, FString& OutTexName,
	int32& OutSrcX, int32& OutSrcY, int32& OutW, int32& OutH)
{
	OutTexName.Reset(); OutSrcX = 0; OutSrcY = 0; OutW = 32; OutH = 35;
	const UGameInstance* GI = WorldCtx ? UGameplayStatics::GetGameInstance(WorldCtx) : nullptr;
	const UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UDataTable* T = Items ? Items->GetDataTable(TEXT("title_rank")) : nullptr;
	if (!T) { return false; }
	// Эталон: rank 1..1600 → категория 0 по rank; иначе (rank<101 || cat>=5) → категория 1 по cat,
	//   иначе категория 0 по значению 101. Ищем запись findRecordByKeyInRange: Cat==c && Min<=v<=Max.
	int32 Cat = 0, Val = InRankValue;
	if (InRankValue < 1 || InRankValue > 1600)
	{
		if (InRankValue < 101 || InRankCat >= 5) { Cat = 1; Val = InRankCat; }
		else                                     { Cat = 0; Val = 101; }
	}
	for (const TPair<FName, uint8*>& It : T->GetRowMap())
	{
		const FAsdaTitleRankRow* R = reinterpret_cast<const FAsdaTitleRankRow*>(It.Value);
		if (!R || R->CategoryKey != Cat) { continue; }
		if (R->ThresholdMin <= Val && Val <= R->ThresholdMax)
		{
			OutTexName = R->IconTexName;
			OutSrcX = R->IconSrcX; OutSrcY = R->IconSrcY;
			OutW = FMath::Max(1, R->IconWidth); OutH = FMath::Max(1, R->IconHeight);
			return true;
		}
	}
	return false;
}

void UAsdaNameplateComponent::UpdateRankBadge()
{
	if (Kind != EAsdaNameplateKind::Player) { return; }
	FString TexName; int32 SrcX = 0, SrcY = 0, Bw = 32, Bh = 35;
	if (!LookupRankBadge(this, RankValue, RankCat, TexName, SrcX, SrcY, Bw, Bh) || TexName.IsEmpty())
	{
		// Эталон делает контрол видимым с ПУСТЫМ именем текстуры (рисуется ничего) → у нас просто скрыт.
		SetControlVisible(kCtrlRank, false);
		return;
	}
	UTexture2D* Tex = UAsdaClanFlag::LoadJuiTexture(TexName);
	if (!Tex)
	{
		SetControlVisible(kCtrlRank, false);
		return;
	}
	// Центровка иконок больше 32×35 (эталон: offX=(W−32)/2, offY=(H−35)/2; при >0 сдвиг позиции).
	const int32 OffX = (Bw - (int32)kBadgeDefault.X) / 2;
	const int32 OffY = (Bh - (int32)kBadgeDefault.Y) / 2;
	if (OffX > 0 || OffY > 0)
	{
		const FVector2D* LaidOut = BasePos.Find(FName(TEXT("__badgeLaidOut")));
		const FVector2D Base = LaidOut ? *LaidOut : GetBasePos(kCtrlRank);
		SetControlPos(kCtrlRank, FVector2D(Base.X - OffX, Base.Y - (OffY + 14)));
	}
	// ★ ЛОГИЧЕСКИЙ размер атласа (координаты в данных — в пикселях исходного).
	const FIntPoint LogSize = AsdaUi::LogicalAtlasSize(Tex);
	const float Tw = (float)LogSize.X;
	const float Th = (float)LogSize.Y;
	const FVector2D UV0(SrcX / Tw, SrcY / Th);
	SetControlTexture(kCtrlRank, Tex, FBox2D(UV0, UV0 + FVector2D(Bw / Tw, Bh / Th)), FVector2D(Bw, Bh));
}

void UAsdaNameplateComponent::OnUnregister()
{
	DestroyScreenWidget();
	Super::OnUnregister();
}

void UAsdaNameplateComponent::EnsureScreenWidget()
{
	if (ScreenWidget) { return; }
	UWorld* W = GetWorld();
	if (!W || !W->IsGameWorld()) { return; }
	UClass* Cls = LoadClass<UUserWidget>(nullptr, WidgetPathForKind());
	APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
	if (!Cls || !PC) { return; }
	ScreenWidget = CreateWidget<UUserWidget>(PC, Cls);
	if (!ScreenWidget) { return; }
	ScreenWidget->AddToViewport(-1);                       // под игровым UI
	ScreenWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	// Слот в вьюпорте у НОВОГО виджета чистый — сбрасываем кэш дедупа (см. заголовок, LastTopLeft).
	LastScreenVis = (uint8)ESlateVisibility::HitTestInvisible;
	LastOccOpacity = 1.f;      // новый виджет непрозрачен (умолчание UWidget)
	OccState.Reset();          // перекрытие мерится заново на первом же кадре показа
	bTopLeftValid = false;
	bViewportSlotSized = false;
	RefreshNameplate();
	InitialRefreshTicks = 2;   // повторный прогон после первого Slate-prepass (иначе бейдж/марка не рисуются до клика)
}

void UAsdaNameplateComponent::DestroyScreenWidget()
{
	if (ScreenWidget)
	{
		ScreenWidget->RemoveFromParent();
		ScreenWidget = nullptr;
		// Кэш слота относится к УНИЧТОЖЕННОМУ виджету — обнуляем, иначе следующий не получит ни размера, ни позиции.
		LastScreenVis = 255;
		LastOccOpacity = -1.f;
		OccState.Reset();
		bTopLeftValid = false;
		bViewportSlotSized = false;
	}
}

// ★ Позиционирование 1:1 с эталоном (nameplate_positioning_re.md):
//   мировая точка над головой → экран; якорь окна ставится в (screenX − ширина/2, screenY − 50).
//   Наш якорь внутри канваса = kPlateAnchor, поэтому левый-верхний угол = (sx − anchorX, sy − 50 − anchorY).
// ★ Видимость экранного виджета — ТОЛЬКО при реальном изменении (правка 07.09.2026).
//   UWidget::SetVisibility доходит до Slate и трогает атрибут видимости; плашек сотни, а значение
//   у подавляющего большинства из кадра в кадр одно и то же. Кэш значения лежит в самом компоненте.
// ★ [ИСПРАВЛЕНИЕ] 08.11.2026: 1 — плашка видна в пределах дальности обзора из настроек; 0 — прежний предел 3×3 сектора сервера.
static ASDA_DEV_CVAR(int32, CVarOverheadViewRangeNames,
	TEXT("asda.Overhead.ViewRangeNames"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 08.11.2026] 1 = имя и полоска над головой показываются до дальности прорисовки из настроек; 0 = до 3×3 секторов сервера (прежнее)."),
	ECVF_Default);

static void AsdaSetPlateVis(UUserWidget* W, uint8& Cached, ESlateVisibility V)
{
	if (!W || Cached == (uint8)V) { return; }
	Cached = (uint8)V;
	W->SetVisibility(V);
}

void UAsdaNameplateComponent::UpdateScreenPosition()
{
	if (!ScreenWidget) { return; }
	UWorld* W = GetWorld();
	if (!W) { return; }
	// ★★★ БЫСТРЫЙ ПУТЬ (правка 07.09.2026, ключ отката asda.Plate.FastPath 0).
	//   Всё, что не зависит от конкретной плашки — контроллер, матрица вид×проекция, размер вьюпорта,
	//   коэффициент DPI, — считает подсистема ОДИН раз за кадр (FAsdaFrameProjection). Раньше каждая
	//   из ~430 плашек добывала это сама: UGameplayStatics::GetPlayerController (перебор игроков),
	//   ProjectWorldLocationToScreen → ULocalPlayer::GetProjectionData (полная сборка данных вида) и
	//   UWidgetLayoutLibrary::GetViewportScale (в редакторной сборке БЕЗ кадрового кэша).
	const bool bFast = UAsdaVisibilitySubsystem::PlateFastPath();
	UAsdaVisibilitySubsystem* Vis = bFast ? UAsdaVisibilitySubsystem::Get(this) : nullptr;
	APlayerController* PC = Vis ? Vis->GetLocalPC() : nullptr;
	// Подсистемы нет (редактор), ключ выключен или кэш ещё пуст (первый кадр мира) — прежний поиск.
	if (!PC) { PC = UGameplayStatics::GetPlayerController(W, 0); }
	if (!PC) { return; }
	// ★ ГЕЙТЫ ВИДИМОСТИ (эталон): (а) над СОБОЙ плашка не рисуется; (б) скрытый владелец (запечённый
	//   шаблон NPC/моба, труп до деспавна) — плашки быть не должно (иначе «призрачные» имена шаблонов).
	{
		const AActor* Owner = GetOwner();
		const bool bOwnerHidden = !Owner || Owner->IsHidden();
		const bool bIsLocalPawn = (bHideForLocalPlayer && Owner && Owner == PC->GetPawn());
		if (bOwnerHidden || bIsLocalPawn || !bAlwaysShowName)
		{
			AsdaSetPlateVis(ScreenWidget, LastScreenVis, ESlateVisibility::Collapsed);
			return;
		}
	}

	// ★ ЗОНА ВИДИМОСТИ 1:1 С СЕРВЕРОМ: клиппинг 3×3 сектора по 30 тайлов (AoI). В оригинале плашки не
	//   имеют дистанц-порога — сущности просто не существуют за AoI. Наши запечённые NPC существуют
	//   всегда, поэтому режем плашки по той же секторной сетке (иначе видны имена через всю карту).
	// ★★★ [ИСПРАВЛЕНИЕ] 08.11.2026 (баг «имя и полоска монстра пропадают вдали», ключ asda.Overhead.ViewRangeNames): сектор 3×3 ниже описывает
	//   обзор сервера оригинала, а порт с 06.11 показывает сущности до дальности из настроек (`UAsdaViewRangeSubsystem`, ползунок
	//   `asda.Cull.Scale`). Моб стоял на виду, а плашка гасла за 3000…6000 ед. Предел плашки — та же дальность: мобов и NPC дальше неё
	//   скрывает сама подсистема (гейт скрытого владельца выше), перекрытие и фрустум ниже не меняются. Сеть не затронута.
	const float ViewRange = (bLimitByServerAoI && CVarOverheadViewRangeNames.GetValueOnGameThread() != 0) ? UAsdaViewRangeSubsystem::RangeUnits(W) : 0.f;
	if (ViewRange > 0.f)
	{
		const APawn* Self = PC->GetPawn();
		if (Self && FVector::DistSquared2D(Self->GetActorLocation(), GetComponentLocation()) > FMath::Square((double)ViewRange))
		{
			AsdaSetPlateVis(ScreenWidget, LastScreenVis, ESlateVisibility::Collapsed);
			return;
		}
	}
	else if (bLimitByServerAoI)
	{
		const APawn* Self = PC->GetPawn();
		if (Self)
		{
			// UE→game: gameX = UE.Y, gameZ = −UE.X; тайл = game/100; сектор = тайл/SectorTiles.
			auto SectorOf = [this](const FVector& L) -> FIntPoint
			{
				// ★ Аудит №3 (B1/B2): UE→game и game→тайл — через общую точку; деление на `S` ниже —
				//   это уже ТАЙЛЫ → СЕКТОРЫ (второй уровень сетки, не тайловая формула).
				const FIntPoint Tile = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(L));
				const int32 S = FMath::Max(1, SectorTiles);
				return FIntPoint(FMath::FloorToInt((float)Tile.X / S), FMath::FloorToInt((float)Tile.Y / S));
			};
			const FIntPoint A = SectorOf(Self->GetActorLocation());
			const FIntPoint B = SectorOf(GetComponentLocation());
			const int32 R = FMath::Max(0, AoIRadiusSectors);
			if (FMath::Abs(A.X - B.X) > R || FMath::Abs(A.Y - B.Y) > R)
			{
				AsdaSetPlateVis(ScreenWidget, LastScreenVis, ESlateVisibility::Collapsed);
				return;
			}
		}
	}
	// Пустое имя — плашки нет (эталон: без имени плашка не рисуется).
	if (DisplayName.IsEmpty())
	{
		AsdaSetPlateVis(ScreenWidget, LastScreenVis, ESlateVisibility::Collapsed);
		return;
	}
	// ★ D3 (дедуп UI 2026-08-08): проекция мир→экран + фрустум-отсечка (эталон planeAccumulateDotTest +
	//   g_FrustumCullMatrix). Медленный путь — общая точка AsdaUi::WorldToViewportFollow (та же, что у
	//   облачка чата). Быстрый путь делает РОВНО ТО ЖЕ, но на готовых матрицах кадра: движок внутри
	//   ProjectWorldLocationToScreen (PlayerController.cpp:2203) после GetProjectionData вызывает именно
	//   FSceneView::ProjectWorldToScreen и вычитает ViewRect.Min при bPlayerViewportRelative=true.
	//   Запас Margin = ширина плашки (клампинг проекции за краем экрана).
	FVector2D Screen = FVector2D::ZeroVector;
	bool bOnScreen = false;
	const FAsdaFrameProjection* Proj = Vis ? &Vis->GetFrameProjection() : nullptr;
	// ★★★ [ОРИГИНАЛ] 30.10.2026 (asda.Overhead.LateProject): кадровая проекция подсистемы видимости собрана в её Tick — ДО
	//   обновления камеры и к тому же в ПРОШЛОМ кадре (тикаемый объект мира идёт после компонентов) ⇒ плашка отставала от камеры
	//   на два кадра. Здесь (тик в TG_PostUpdateWork) берём проекцию по камере ЭТОГО кадра; нет её — прежний путь.
	if (Vis && AsdaOverheadLate::IsOn())
	{
		if (const FAsdaFrameProjection* Fresh = AsdaOverheadLate::FreshProjection(W)) { Proj = Fresh; }
	}
	const FVector PlatePt = GetPlateWorldPoint();   // ★ 08.10.2026: у сидящего игрока — центр AABB (как оригинал)
	if (Proj && Proj->bValid)
	{
		if (FSceneView::ProjectWorldToScreen(PlatePt, Proj->ViewRect, Proj->ViewProj, Screen))
		{
			Screen -= FVector2D(Proj->ViewRect.Min);   // координаты вьюпорта игрока (сплит/окно)
			const float Margin = kPlateCanvas.X;
			const FVector2D Vp = Proj->ViewportSize;
			bOnScreen = !(Screen.X < -Margin || Screen.Y < -Margin
			           || Screen.X > Vp.X + Margin || Screen.Y > Vp.Y + Margin);
		}
	}
	else
	{
		bOnScreen = AsdaUi::WorldToViewportFollow(PC, W, PlatePt, kPlateCanvas.X, Screen);
	}
	if (!bOnScreen)
	{
		AsdaSetPlateVis(ScreenWidget, LastScreenVis, ESlateVisibility::Collapsed);   // за камерой или вне кадра
		return;
	}
	// ★★★ [ОРИГИНАЛ] ПЕРЕКРЫТИЕ ПРЕПЯТСТВИЯМИ (29.10.2026, баг игроков 3, ключ asda.Overhead.Occlude): у оригинала план плашки
	//   несёт глубину ЭТОЙ ЖЕ точки (`matrixProjectZ` → поле 527 → z вершин) и рисуется с z-тестом — стена/крыша между камерой
	//   и точкой его закрывает. Порт: луч камера → точка, плавный ход непрозрачности (разбор — `World/AsdaOverheadOcclusion.h`).
	// ★★★ [ОРИГИНАЛ] 05.11.2026 (баг игроков «имена NPC поверх деревьев», asda.Overhead.OccludeAtText): z-тест оригинала — в пикселях
	//   КВАДА плашки, а он поднят над проекцией точки (якорь окна на y − 50): крона дерева закрывает текст, но не точку над головой.
	//   Решаем по строке имени: её центр и ширина — из живой геометрии контрола имени (кадр назад; до первого prepass — строка на
	//   высоте якоря окна), пересчёт в пиксели вьюпорта: локальная точка канваса L ↦ экран + (L − (якорь.X + сдвиг, якорь.Y + 50))·DPI.
	const float DPIo = (Proj && Proj->bValid) ? Proj->DpiScale : UWidgetLayoutLibrary::GetViewportScale(W);
	FVector2D TextCtrLocal(kPlateAnchor.X + ExtraScreenOffsetX, kPlateAnchor.Y);
	float TextHalfLocal = 40.f;
	{
		FVector2D NPos, NSize;
		const FGeometry& WG = ScreenWidget->GetCachedGeometry();
		const float WScale = WG.Scale;
		if (WScale > 0.f && WG.GetLocalSize().X > 0.f && GetNameAbsRect(NPos, NSize) && NSize.X > 0.f)
		{
			TextCtrLocal = WG.AbsoluteToLocal(NPos + NSize * 0.5f);
			TextHalfLocal = NSize.X * 0.5f / WScale;
		}
	}
	const FVector2D TextCtrPx = (TextCtrLocal - FVector2D(kPlateAnchor.X + ExtraScreenOffsetX, kPlateAnchor.Y + 50.f)) * DPIo;
	const float OccA = AsdaOverheadOcclusion::EvaluateText(OccState, W, PlatePt, TextCtrPx, TextHalfLocal * DPIo, GetOwner());
	if (OccA <= 0.f)
	{
		AsdaSetPlateVis(ScreenWidget, LastScreenVis, ESlateVisibility::Collapsed);   // перекрыта целиком — Slate её не рисует
		return;
	}
	AsdaSetPlateVis(ScreenWidget, LastScreenVis, ESlateVisibility::HitTestInvisible);
	if (OccA != LastOccOpacity)
	{
		LastOccOpacity = OccA;
		ScreenWidget->SetRenderOpacity(OccA);
	}
	const float DPI = (Proj && Proj->bValid) ? Proj->DpiScale : UWidgetLayoutLibrary::GetViewportScale(W);
	const FVector2D TopLeft(Screen.X / FMath::Max(DPI, 0.01f) - kPlateAnchor.X - ExtraScreenOffsetX,
	                        Screen.Y / FMath::Max(DPI, 0.01f) - 50.f - kPlateAnchor.Y);
	if (!bFast)
	{
		// Прежний путь целиком (для честного ABBA и как откат): обе записи слота каждый кадр.
		ScreenWidget->SetPositionInViewport(TopLeft, /*bRemoveDPIScale=*/false);
		ScreenWidget->SetDesiredSizeInViewport(kPlateCanvas);
		return;
	}
	// ★★★ ДЕДУП ЗАПИСЕЙ В СЛОТ ВЬЮПОРТА (обоснование — у LastTopLeft в заголовке).
	//   Размер слота у плашки КОНСТАНТА (kPlateCanvas) — ставим его один раз на виджет, а не каждый кадр.
	if (!bViewportSlotSized)
	{
		ScreenWidget->SetDesiredSizeInViewport(kPlateCanvas);
		bViewportSlotSized = true;
	}
	//   Позиция — только при сдвиге от полупикселя и больше. Порог НИЖЕ пикселя, поэтому дрожания
	//   («ступеньки» плашки при медленном ходе) он дать не может: движение меньше полупикселя на экране
	//   не отображается вовсе — слот всё равно округляется рендером Slate до пикселя.
	if (!bTopLeftValid || !TopLeft.Equals(LastTopLeft, 0.5))
	{
		LastTopLeft = TopLeft;
		bTopLeftValid = true;
		ScreenWidget->SetPositionInViewport(TopLeft, /*bRemoveDPIScale=*/false);
	}
}

// ★★★ ГЕЙТ ЗНАЧИМОСТИ ПЛАШКИ — ОТДЕЛЬНОЙ ФУНКЦИЕЙ (доводка 07.09.2026, В3 ревизии review_cpu_insights.md).
//   Возвращает true = «этой плашке в этом кадре делать нечего» (вызывающий немедленно выходит из тика).
//   Вынесен из тела тика, потому что вызывается ИЗ ДВУХ мест: до Super::TickComponent (быстрый порядок,
//   asda.Vis.GateBeforeSuper 1) и после него (прежний порядок, ключ 0) — так «до/после» меряется
//   чередующимися срезами в ОДНОМ прогоне, без двух сборок.
//
//   ЧТО ИМЕННО ЭКОНОМИТ ПЕРЕНОС ВЫШЕ Super. `Super` здесь — UWidgetComponent::TickComponent
//   (WidgetComponent.cpp:1241): он зовёт UpdateWidget() (для World-пространства — TakeWidget()
//   виджета, SNew/Resize окна SVirtualWindow, UpdateWidgetOnScreen) и, при ShouldDrawWidget(),
//   рисует виджет в render target. Всё это делается КАЖДЫЙ кадр у КАЖДОЙ плашки, в том числе
//   у той, что вне кадра камеры. В игре компонент переведён в World-пространство и скрыт
//   (OnRegister: SetVisibility(false)+SetHiddenInGame(true)) — плашку рисует отдельный
//   viewport-виджет ScreenWidget, — так что для НАС тело Super полезной работы не несёт.
//   Базовые классы между нами и UWidgetComponent (UMeshComponent/UPrimitiveComponent/USceneComponent)
//   TickComponent НЕ переопределяют, а UActorComponent::TickComponent (ActorComponent.cpp:1882)
//   для НЕ-блюпринтового класса не делает ничего (ветка ReceiveTick только для CLASS_CompiledFromBlueprint).
//   Значит пропуск Super на невидимой плашке ничего обязательного не теряет: в следующем же кадре,
//   когда плашка вернётся в зону, Super отработает как раньше.
bool UAsdaNameplateComponent::ShouldSkipTickByVisibility()
{
	if (!UAsdaVisibilitySubsystem::NameplatesEnabled()) { return false; }
	UAsdaVisibilitySubsystem* Vis = UAsdaVisibilitySubsystem::Get(this);
	if (!Vis) { return false; }   // редактор/не игровой мир — подсистемы нет, работаем как раньше
	// Своя плашка не гейтится никогда (закон self-nameplate-same-as-others: у локального павна показываем всё).
	//   ★ Доводка: локального павна берём из КАДРОВОГО кэша подсистемы, а не ищем PlayerController
	//   на каждую из ~430 плашек (UGameplayStatics::GetPlayerController в теле тика — тоже часть цены).
	if (GetOwner() && GetOwner() == Vis->GetLocalPawn()) { return false; }
	// Радиус = высота плашки над головой (nameplate-height-from-data: npc_list +0x68, у игрока 150) —
	//   так объект «зацепляет» конус краем ровно тогда, когда его плашка вот-вот войдёт в кадр.
	const int32 Level = Vis->Evaluate(VisState, GetComponentLocation(), 200.f);
	if (Level == (int32)EAsdaVisLevel::Off)
	{
		// ★ ВИДЖЕТ НЕ УНИЧТОЖАЕМ, а СХЛОПЫВАЕМ. Разбор (07.09.2026):
		//   * главная цена — сам тик (Super + EnsureScreenWidget + UpdateScreenPosition + сглаживание
		//     полосы у ~430 компонентов), и она уходит этим `return` полностью;
		//   * у объекта, который НИ РАЗУ не был в зоне (запечённые офлайн-мобы), виджет вообще
		//     не создаётся — до EnsureScreenWidget мы не доходим, а это вторая половина цены;
		//   * Collapsed-виджет в дереве Slate не участвует ни в prepass, ни в отрисовке;
		//   * а вот уничтожение с последующим CreateWidget+AddToViewport на РАЗВОРОТЕ дало бы
		//     пачку пересозданий в одном кадре (сотни плашек) — рывок начал бы «дёргать» кадр.
		//     Ровно того артефакта, ради которого волна и делается, допускать нельзя.
		// ★ 29.10.2026 [ИСПРАВЛЕНИЕ порта]: схлопывание — ЧЕРЕЗ кэш видимости (AsdaSetPlateVis). Прежде здесь стоял прямой
		//   SetVisibility(Collapsed) мимо `LastScreenVis`: если плашка уходила в «Off», будучи в кадре (рывок/телепорт сущности,
		//   резкая смена места), кэш оставался «видна», и на возврате `AsdaSetPlateVis(HitTestInvisible)` молча ничего не делал —
		//   плашка оставалась невидимой до следующей смены видимости по другой причине (поймано самотестом перекрытия).
		AsdaSetPlateVis(ScreenWidget, LastScreenVis, ESlateVisibility::Collapsed);
		return true;   // ни проекции, ни сглаживания полосы, ни разбора альтернатора
	}
	// Возврат в зону (уровень только что стал >= 1): полоса здоровья должна показать ТЕКУЩЕЕ значение,
	//   а не доезжать из старого — пока плашка молчала, HP могло измениться пакетами, и «наливание»
	//   на появлении запрещено (см. FAsdaBarSmoother::SetTarget: первый показ всегда мгновенный).
	if (VisState.bChanged) { HpSmooth.Shown = HpSmooth.Target; }
	return false;
}

void UAsdaNameplateComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// ★ ВОЛНА CPU 07.09.2026 (cpu_visibility_report.md + доводка cpu_fix2_report.md): гейт значимости.
	//   На alpen ~430 плашек тикают всегда, в т.ч. у скрытых офлайн-мобов, и каждая создаёт UUserWidget
	//   в вьюпорте. Вне расширенного конуса камеры (запас asda.Vis.FovMargin + предсказание по угловой
	//   скорости) и дальше asda.Vis.NearRadius плашка не нужна: тик не делает НИЧЕГО, а виджет схлопывается.
	//   Гейты, которые НЕ трогаем (они дешевле и остаются как были): скрытый владелец, пустое имя,
	//   секторная AoI — всё внутри UpdateScreenPosition.
	//   ПОРЯДОК (доводка): по умолчанию гейт стоит ДО Super — тогда невидимая плашка не платит и за тело
	//   UWidgetComponent::TickComponent. Откат порядка: asda.Vis.GateBeforeSuper 0; откат всего гейта:
	//   asda.Vis.Nameplates 0 (или общий asda.Vis.Enable 0).
	// ★ 30.10.2026: группа тика следует ключу asda.Overhead.LateProject (переключение на лету — со следующего кадра).
	if (PrimaryComponentTick.TickGroup != AsdaPlateTickGroup()) { SetTickGroup(AsdaPlateTickGroup()); }
	const bool bGateFirst = UAsdaVisibilitySubsystem::GateBeforeSuper();
	if (bGateFirst && ShouldSkipTickByVisibility()) { return; }

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bGateFirst && ShouldSkipTickByVisibility()) { return; }

	if (!ScreenWidget) { EnsureScreenWidget(); }   // PlayerController мог появиться позже спавна
	UpdateScreenPosition();
	// ★ Плавный ход полосы здоровья: пока показанная доля не догнала целевую — перерисовываем.
	if (HpSmooth.Advance(DeltaTime, FAsdaBarSmoother::SmoothSeconds()))
	{
		ASDA_CTRACE_V(EAsdaCT::HpPlate, TEXT("плашка %s: плавная полоса %.3f → цель %.3f (HP %d/%d)"),
			*AsdaCtNpWho(this), HpSmooth.Shown, HpSmooth.Target, CurHp, MaxHp);
		ApplyHpBars();
	}
	// ★ Догоняющий Refresh: измерения текста/позиции слотов валидны только после Slate-prepass.
	if (InitialRefreshTicks > 0 && ScreenWidget)
	{
		if (--InitialRefreshTicks == 0) { RefreshNameplate(); }
	}
	// ★ Камп-марка меняет картинку по альтернатору (эталон: charNamePlate_UpdateAllFactions вызывается
	//   ИМЕННО в момент переключения g_bIdleBlinkToggle, а не каждый кадр).
	if (Kind == EAsdaNameplateKind::Player)
	{
		const int32 Blink = IdleBlinkToggle(this) ? 1 : 0;
		if (Blink != LastBlink)
		{
			LastBlink = Blink;
			UpdateFactionUI();
		}
		// ★ Ключи одержимости должны гасить/зажигать НЕМЕДЛЕННО. Отдельного колбэка на CVar не вешаем:
		//   плашек в мире много и живут они на акторах — сравнение значения в тике проще и не требует
		//   реестра компонентов. Перерисовываем ТОЛЬКО когда значение реально изменилось.
		const int32 TagCVar   = CVarPossessTag.GetValueOnGameThread();
		const int32 ColorCVar = CVarPossessNameColor.GetValueOnGameThread();
		if (TagCVar != LastPossessTagCVar || ColorCVar != LastPossessColorCVar)
		{
			const bool bFirst = (LastPossessTagCVar < 0);
			LastPossessTagCVar   = TagCVar;
			LastPossessColorCVar = ColorCVar;
			if (!bFirst) { RefreshNameplate(); }   // первый тик — просто запоминаем, плашка уже свежая
		}
	}
}

void UAsdaNameplateComponent::RefreshNameplate()
{
	UUserWidget* W = ScreenWidget ? ScreenWidget.Get() : GetUserWidgetObject();
	if (!W)
	{
		// Виджет ещё не создан (InitWidget не отработал / класс не загрузился) — данные применятся позже.
		if (!DisplayName.IsEmpty())
		{
			UE_LOG(LogAsdaNameplate, Verbose, TEXT("[плашка] '%s': виджет не создан (класс=%s)"),
				*DisplayName, *GetPathNameSafe(GetWidgetClass()));
		}
		return;
	}
	const bool bMob = (Kind == EAsdaNameplateKind::Mob);
	// ★ ЦВЕТ ИМЕНИ МОБА ПО РАЗНИЦЕ УРОВНЕЙ (эталон game_main.c:4544 / 4763). Считаем ПЕРЕД
	//   постановкой текста: SetControlText применяет NameColor к контролу.
	//   Красится ТОЛЬКО имя — полоса HP и фон плашки в эталоне не трогаются ни в одной ветке.
	if (bMob && bAutoLevelDiffColor)
	{
		const int32 PlayerLevel = ResolveSelfLevel(this);
		bool bHas = false; int32 Diff = 0;
		const FLinearColor C = ResolveMobNameColor(MobLevel, PlayerLevel, MobAggression, bHas, Diff);
		if (bHas)
		{
			NameColor = C;
			const FColor S = C.ToFColor(true);
			UE_LOG(LogAsdaNameplate, Verbose,
				TEXT("[плашка] моб '%s' | уровень моба=%d, игрока=%d, дельта=%d, агрессия=%d → цвет #%02X%02X%02X"),
				*DisplayName, MobLevel, PlayerLevel, Diff, MobAggression, S.R, S.G, S.B);
		}
	}
	// Имя (контрол зависит от окна: MON → MONNAME, MAN1 → MANNAME1).
	// ★★★ [ОРИГИНАЛ] ОДЕРЖИМЫЙ ИГРОК — ИМЯ ЭТАЛОННЫМ ЦВЕТОМ 0xFF6AFFF4 (эталон −9764876,
	//   AsdaStory_game.c:20902/:21139; обычное значение там −1 = белый). Красится ТОЛЬКО имя:
	//   ни полоса HP, ни фон плашки в эталоне при этом не трогаются. Поле NameColor НЕ перезаписываем —
	//   иначе цвет «залип» бы после снятия одержимости (его считает своя ветка char_updateNameplateColor).
	const bool bPossessedName = (Kind == EAsdaNameplateKind::Player) && (SoulState == 2)
		&& (CVarPossessNameColor.GetValueOnGameThread() != 0);
	if (bPossessedName)
	{
		SetControlTextColored(kCtrlName, DisplayName, true, FLinearColor::FromSRGBColor(PossessNameColor));
	}
	else if (Kind == EAsdaNameplateKind::Npc && GetOwner() && Cast<AAsdaNpcActor>(GetOwner())
		&& AsdaNpcMissing::HasPrefix(this, Cast<AAsdaNpcActor>(GetOwner())->NpcId))
	{
		// ★ 09.11.2026 [ПОРТ, решение владельца] «пропавший» NPC (список AsdaNpcLists::kMissingPrefix, ключ asda.Npc.MissingPrefix; разбор —
		//   Public/Npc/AsdaNpcMissing.h): красный «[MISSING]» перед именем, имя своим цветом. Два цвета в одном контроле — только
		//   rich-путь (та же единая точка разметки, что у титула); шрифт, выравнивание и обводка — с контрола имени.
		UUserWidget* NW = ScreenWidget ? ScreenWidget.Get() : GetUserWidgetObject();
		if (UWidget* C = NW ? NW->GetWidgetFromName(FName(kCtrlName)) : nullptr)
		{
			FSlateFontInfo Font;
			bool bFont = false;
			if (const UTextBlock* T = Cast<UTextBlock>(C)) { Font = T->GetFont(); bFont = true; }
			UWidget* Applied = UAsdaRichText::ApplyFormatted(C, AsdaNpcMissing::PrefixMarkup(DisplayName, NameColor));
			if (URichTextBlock* RT = Cast<URichTextBlock>(Applied ? Applied : C))
			{
				RT->SetAutoWrapText(false);
				RT->SetJustification(ETextJustify::Center);
				if (bFont) { RT->SetDefaultFont(Font); }
				RT->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			else if (Applied) { Applied->SetVisibility(ESlateVisibility::HitTestInvisible); }
		}
	}
	else
	{
		SetControlText(bMob ? kCtrlNameMon : kCtrlName, DisplayName, true);
	}
	if (!bMob)
	{
		// Гильдия/титул — только у игроков и только при наличии данных (эталон: Visible=0 по умолчанию).
		const bool bPlayer = (Kind == EAsdaNameplateKind::Player);
		// ★ ЦВЕТ ГИЛЬДИИ — фиксированный бледно-голубой (эталон −5051137, ветвлений нет).
		SetControlTextColored(kCtrlGuild, bPlayer ? GuildName : FString(), bPlayer, GuildColor);
		if (bPlayer)
		{
			// ★ ТИТУЛ = ДВА СЛОТА в одном контроле, у каждого свой цвет по грейду (разметка <FC=..>).
			ApplyTitleMarkup();
			// Иконка гильдии 1663: в ЭТАЛОНЕ на этот контрол ничего не рисуется (только инстанс+Visible);
			//   реальная эмблема клана идёт на камп-марку 1669 — поэтому 1663 остаётся пустым.
			SetControlVisible(kCtrlGuildIcon, false);
			// Раскладка (сдвиги гильдии/бейджа) + фракция + бейдж рейтинга — как в эталоне из одной точки.
			LayoutControls();
		}
		else
		{
			SetControlTextColored(kCtrlTitle, FString(), false, GuildColor);
			for (const TCHAR* N : { kCtrlGuildIcon, kCtrlRank, kCtrlMark, kCtrlMarkR, kCtrlMarkB, kCtrlMarkG,
									kCtrlMarkS1, kCtrlMarkS2, kCtrlFlagBg, kCtrlFlagBorder, kCtrlFlagSign })
			{
				SetControlVisible(N, false);
			}
		}
	}
	// ★★★ НАШ суффикс «(Одержим)» под именем (у игроков, при SoulState==2). Гейты — внутри метода.
	ApplyPossessTag(W);
	ApplyHpBars();
}

void UAsdaNameplateComponent::SetHp(int32 InCur, int32 InMax, int32 InSegments)
{
	// ★ Смена владельца плашки (другой моб на том же акторе) или первый показ — без анимации:
	//   полоса не должна «наливаться» при появлении цели.
	if (MaxHp != InMax) { HpSmooth.Reset(); }
#if ASDA_COMBAT_TRACE
	if (CurHp != InCur || MaxHp != InMax || Segments != InSegments)   // журнал боя: только смена значения, не каждый повторный вызов
	{
		// Первое заполнение плашки (max был 0 — спавн сущности, сотни на загрузке карты) — только частый уровень.
		if (MaxHp <= 0 && !bShowHp)
		{
			ASDA_CTRACE_V(EAsdaCT::HpPlate, TEXT("плашка %s: первое заполнение HP %d / max %d"), *AsdaCtNpWho(this), InCur, InMax);
		}
		else
		{
			ASDA_CTRACE_IF(AsdaCtNpOurs(this), EAsdaCT::HpPlate, TEXT("плашка %s: HP %d → %d / max %d (%.1f%%)%s%s%s"),
				*AsdaCtNpWho(this), CurHp, InCur, InMax, InMax > 0 ? 100.0 * (double)InCur / (double)InMax : 0.0,
				InSegments > 0 ? *FString::Printf(TEXT(", жизней %d"), InSegments) : TEXT(""),
				MaxHp != InMax ? TEXT(", max сменился — полоса без анимации") : TEXT(""),
				bShowHp ? TEXT("") : TEXT(", полоса скрыта (не цель)"));
		}
	}
#endif
	CurHp = InCur; MaxHp = InMax; Segments = InSegments;
	ApplyHpBars();
}
