// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaNameplateComponent — НАД-ГОЛОВНАЯ ПЛАШКА сущности (порт JUI-окон gamemain 1:1).
//   Эталон: TARGETMARK_MON (1600) для мобов, TARGETMARK_MAN1 (1660) — постоянная плашка игрока,
//   TARGETMARK_MAN (1620) — панель выбранной цели-игрока. Виджеты сгенерированы 1:1 из JUI
//   (Tools/asda_build_nameplates.py): контролы стоят в ЛОКАЛЬНЫХ координатах вокруг ЯКОРЯ окна,
//   якорь — в центре канваса, поэтому пивот (0.5,0.5) кладёт плашку ровно в проекцию точки над головой.
//
// Editor-friendly (правило ue-editor-friendly): компонент создаётся в конструкторе актора и виден
//   в редакторе (в редакторе — World-space, в игре — Screen-space как в оригинале).
#pragma once

#include "CoreMinimal.h"
#include "UI/AsdaBarSmoother.h"   // ★ общий сглаживатель полос здоровья
#include "Components/WidgetComponent.h"
#include "World/AsdaClanFlagData.h"   // FAsdaClanFlagData — дизайн эмблемы клана (пакет 6542)   ★ R3: FAsdaClanFlagData — узкий заголовок вместо AsdaClanFlag.h
#include "World/AsdaVisState.h"   // ★ FAsdaVisState — значимость плашки по видимости (волна CPU 07.09.2026)   ★ R3: FAsdaVisState — узкий заголовок вместо AsdaVisibilitySubsystem.h
#include "World/AsdaOverheadOcclusion.h"   // ★ FAsdaOccludeState — плашка прячется за препятствиями (баг игроков 3, 29.10.2026)
#include "AsdaNameplateComponent.generated.h"

// Какой JUI-шаблон использовать (эталон различает моба и игрока разными окнами).
UENUM(BlueprintType)
enum class EAsdaNameplateKind : uint8
{
	Mob     UMETA(DisplayName = "Моб (TARGETMARK_MON)"),
	Player  UMETA(DisplayName = "Игрок (TARGETMARK_MAN1)"),
	Npc     UMETA(DisplayName = "NPC (имя без HP)"),
};

UCLASS(ClassGroup = (Asda), meta = (BlueprintSpawnableComponent))
class ASDAREBORN_API UAsdaNameplateComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UAsdaNameplateComponent();

	// Тип плашки — выбирает JUI-окно (моб/игрок/NPC).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	EAsdaNameplateKind Kind = EAsdaNameplateKind::Mob;

	// Отображаемое имя (в редакторе видно сразу; в игре ставится из таблиц/сети).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	FString DisplayName;

	// Гильдия/титул (у игроков; пусто — контрол скрыт, как в эталоне Visible=0).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	FString GuildName;

	// ★ ДВА СЛОТА ТИТУЛА (эталон: titleId[0]=+208840, titleId[1]=+208842; строка на контроле 1667 =
	//   sprintf("%s %s", titleText[0], titleText[1]) — ПРЕФИКС и СУФФИКС, каждый со СВОИМ цветом по грейду).
	//   TitleName/TitleGrade ниже — слот 0 (совместимость); слот 1 — TitleName1/TitleGrade1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	FString TitleName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	FString TitleName1;

	// HP (эталон: полоса 100px = 100%; сегменты — «жизни» босса, +18700).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	int32 CurHp = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	int32 MaxHp = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	int32 Segments = 0;   // 0 = одиночная полоса (обычный моб), >0 = мульти-жизни (босс)

	// ★★★ РАСШИРЕНИЕ СВЕРХ ОРИГИНАЛА (просьба владельца 2026-08-24). «Второе» здоровье —
	//   то, до которого дотянет действующее восстановление (зелье/навык). В плашке оригинала
	//   такого слоя НЕТ: окно TARGETMARK_MAN1 несёт только заливку (1664) и фон (1665), см.
	//   `Docs/UI/World/TARGETMARK_MAN1.md`. Поэтому подложка рисуется НАШИМ виджетом,
	//   создаваемым в рантайме, и только у СВОЕЙ плашки — второе значение сервер шлёт лишь по себе.
	//   Ключ отката: `bHpSecondGhost = false` (слой не создаётся вовсе).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	int32 HpSecond = 0;
	UPROPERTY(EditAnywhere, Category = "Asda|Nameplate")
	bool bHpSecondGhost = true;
	// ★ Цвет подложки — РОВНО тот же, что у слоя буффа на полосах игрока: скин `Box_Orange`,
	//   значение `C8FFB300` (см. разбор MINISTATE_WIN). Прежде сюда шёл тёмный тинт, который
	//   умножался на КРАСНУЮ картинку заливки и давал тёмно-красный вместо коричневатого —
	//   поэтому подложка рисуется сплошным цветом, а не тонированной кистью заливки.
	//   Хранится в sRGB и переводится в линейное пространство при показе (закон ui-srgb-gamma-rule).
	UPROPERTY(EditAnywhere, Category = "Asda|Nameplate")
	FColor HpSecondGhostColor = FColor(0xFF, 0xB3, 0x00, 0xC8);

	// Показывать полосу HP (эталон: у моба она видна только когда он ВЫБРАН целью; у NPC HP нет).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	bool bShowHp = false;

	// Цвет имени. Эталон: NPC — ФИКСИРОВАННЫЙ тёплый кремово-золотой (−8818 = 0xFFFFDD8E, ставится при
	//   создании NPC-плашки entity_char.c:59507); моб — по разнице уровней/агрессии (hudUpdateTargetNameplate);
	//   игрок — char_updateNameplateColor (фракция/PK/камп). Дефолт белый = обычный моб при levelDiff −1..1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	FLinearColor NameColor = FLinearColor::White;

	// ── ★★★ ОДЕРЖИМОСТЬ: ЭТАЛОННЫЙ ЦВЕТ ИМЕНИ + НАША МЕТКА «(Одержим)» ──────────────────────────────
	//   ⚠ ОПРОВЕРГНУТО 2026-09-03 (`Reports/possess_broadcast_soulstate.md`): прежний текст здесь
	//   утверждал, будто чужой SoulState по проводу не приходит и признака одержимости в оригинале нет.
	//   Оба утверждения НЕВЕРНЫ:
	//     • SoulState ЛЮБОГО игрока едет каналами 6152 (тело @12) и 5220 (запись @32) — эталонный
	//       клиент их разбирает (`PartyMark_Helper2@0xE64750` → `CCharacter+2241`);
	//     • признак одержимости для окружающих в ОРИГИНАЛЕ ЕСТЬ — имя цели красится в −9764876 =
	//       0xFF6AFFF4 (`AsdaStory_game.c:20902` и `:21139`, `gameUpdateTargetNameplates`).
	//   Значения = серверный SoulState (`@State` из `UPLoginGame`): 0 обычный, 1 «душа», 2 ОДЕРЖИМ.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|Possess")
	int32 SoulState = 0;
	// [ОРИГИНАЛ] Цвет имени одержимого — ровно эталонный −9764876 = 0xFF6AFFF4 (мятно-бирюзовый),
	//   вместо обычного −1 (белого). Ключ отката — `asda.Possess.NameColor`.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|Possess")
	FColor PossessNameColor = FColor(0x6A, 0xFF, 0xF4, 0xFF);
	// [НАШЕ ДОБАВЛЕНИЕ] Цвет СЛОВА «Одержим» в метке под ником — фиолетовый (скобки всегда белые).
	//   sRGB, FColor(R,G,B,A); ~0xFFA020F0. Ключ отката метки — `asda.Nameplate.PossessTag`.
	//   Метка и эталонный цвет имени НЕЗАВИСИМЫ: владелец может оставить любую комбинацию.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|Possess")
	FColor PossessWordColor = FColor(0xA0, 0x20, 0xF0, 0xFF);
	// Задать состояние одержимости (0/2) и перерисовать плашку (для сети/рантайма).
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	void SetSoulState(int32 InSoulState);

	// Цвет имени NPC (эталон −8818): RGB(255,221,142).
	static const FColor NpcNameColor;

	// ── ★ ЦВЕТ ИМЕНИ МОБА ПО РАЗНИЦЕ УРОВНЕЙ (эталон game_main.c:4544/4763, ui_core.c:75770) ──
	//   Полное описание правила и палитра — в Data/AsdaMobNameColorRow.h.
	//   Владелец-моб кладёт сюда уровень типа (Mob list.BIN +0x98) и байт агрессии (+0xFC),
	//   а RefreshNameplate при bAutoLevelDiffColor пересчитывает NameColor на КАЖДОМ обновлении
	//   (плашка моба живёт только на наведении/выборе — значит пересчёт всегда свежий).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|MobColor")
	int32 MobLevel = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|MobColor")
	int32 MobAggression = 0;
	// Пересчитывать цвет имени по разнице уровней (у мобов — да; выключается для строгих тестов).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|MobColor")
	bool bAutoLevelDiffColor = false;

	// Цвет имени моба по эталонной лесенке. Diff = PlayerLevel − MobLevel; Aggression = байт +0xFC.
	//   bOut* — вернули ли правило вообще цвет (агрессия вне {1,2,3,4,5} → эталон цвет не трогает).
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	static FLinearColor ResolveMobNameColor(int32 InMobLevel, int32 InPlayerLevel, int32 InAggression,
		bool& bOutHasColor, int32& OutDiff);
	// ★ Объектный путь таблицы цветов имени моба — одно объявление (15.09.2026, dt_gc_hold): читает
	//   `ResolveMobNameColor`, удерживает на сессию `UAsdaWorldSubsystem::HoldSpawnTables(Mob)`.
	static constexpr const TCHAR* kMobNameColorsPath = TEXT("/Game/AsdaAssets/Data/Mob/DT_MobNameColors.DT_MobNameColors");

	// ★ Уровень ЛОКАЛЬНОГО игрока надёжным путём: рантайм-поле сети, а если оно ещё 0 —
	//   запись выбранного персонажа char-select (тот же фолбэк, что у HUD и меню смерти:
	//   SelfLevel сидируется 4005-self @+40, но приходит не всегда).
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	static int32 ResolveSelfLevel(const UObject* WorldCtx);

	// ★ ЦВЕТ ГИЛЬДИИ — ФИКСИРОВАННЫЙ (эталон: контрол 1662, цвет −5051137 = 0xFFB2ECFF, бледно-голубой;
	//   ветвлений по отношениям в клиенте НЕТ).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	FLinearColor GuildColor = FLinearColor(FColor(0xB2, 0xEC, 0xFF, 0xFF));

	// ★ ГРЕЙД ТИТУЛА (title_list.ColorIndex) → ЦВЕТ титула по палитре редкости (эталон
	//   NetHandler_UpdateCharGuildTitle@0xE9AD90: "<FC=%08x>имя</F>", цвет = slot_getValueByMode@0x9C4820,
	//   массив CItemManager+0x48 = копия drop-палитры). −1 = нет титула.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	int32 TitleGrade = -1;
	// Грейд ВТОРОГО слота титула (суффикс). −1 = слот пуст.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	int32 TitleGrade1 = -1;
	// Id титулов, как приходят в 6068 (−1 = слот пуст). Нужны для признака «титул есть» в раскладке.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	int32 TitleId0 = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	int32 TitleId1 = -1;

	// ── КЛАН (эталон: clanId=+2408 i16 (−1 нет), clanName=+2410 char[20], clanRankName=+2432 char[9]) ──
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|Clan")
	int32 ClanId = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|Clan")
	FString ClanRankName;
	// Флаг клана включён (+2505) — только тогда рисуется ЭМБЛЕМА на камп-марке 1669.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|Clan")
	bool bClanFlagOn = false;
	// Дизайн эмблемы (+2506, 40 байт из пакета 6542).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|Clan")
	FAsdaClanFlagData ClanFlag;

	// ── ФРАКЦИЯ / ВОЙНА ЛАГЕРЕЙ (эталон CharNamePlate_UpdateFactionUI@0x9DCED0) ──
	// campId (+209016): −1 нет, 0=синий, 1=красный, 2=зелёный лагерь (боксы 1671/1670/1672).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|Faction")
	int32 CampId = -1;
	// factionGrade (+209020) — ЗВАНИЕ ВОЙНЫ: иконка Faction_Grade.tex, ячейка (grade−1) в сетке 24 px × 8 кол.
	//   Сервер считает грейд по RoseWarPoint (GameServer sub_466170, таблица порогов dword_5F5F18).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|Faction")
	int32 FactionGrade = -1;
	// Личный магазин открыт (+225921) — сдвигает бейдж на −34 (эталон charNamePlate_LayoutControls).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|Faction")
	bool bShopOpen = false;
	// Мастер-тумблер оригинала (byte_15CB4E7, опция настроек) убран по решению юзера 2026-07-30:
	//   фракц-UI всегда включён при наличии принадлежности (лагерь/флаг клана/звание).

	// ── БЕЙДЖ ПОЗИЦИИ В ЛИДЕРАХ (эталон uiLevelBadgeUpdateTexture@0xA4B270, контрол 1668) ──
	// rankValue (+208972): место в рейтинге 1..1600 (приходит в 6068/6078). −1 = нет.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|Rank")
	int32 RankValue = -1;
	// rankCat (+208976): доп. категория (ветка при rank<1 или >1600; в title_rank.bin строк категории 1 НЕТ).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate|Rank")
	int32 RankCat = -1;

	// Палитра грейдов 0..8 (ARGB, эталон; та же для имён предметов и квестов):
	//   0 белый FFFFFFFF · 1 жёлтый FFFFFF46 · 2 розовый FFFF96DC · 3 зелёный FF96FF32 · 4 оранжевый FFFF7800
	//   5 синий FF4141FF · 6 фиолетовый FFBE50FF · 7 красный FFFA1E46 · 8 голубой FF9BEBEB
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	static FLinearColor GradeColor(int32 Grade);

	// Титул по id из DT_title_list (имя + ColorIndex=грейд). Возвращает false, если записи нет.
	// ★ Аудит №3 (A3): таблица берётся у `UAsdaItemTableSubsystem` (своей загрузки больше нет),
	//   поэтому появился контекст мира (тот же паттерн, что у `IdleBlinkToggle`).
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	static bool LookupTitle(const UObject* WorldCtx, int32 TitleId, FString& OutName, int32& OutGrade);

	// Применить титул по id (ставит TitleName+TitleGrade и обновляет плашку). Слот 0 (совместимость).
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	void SetTitleById(int32 TitleId);

	// ★ ДВА СЛОТА ТИТУЛА из пакета 6068 (эталон NetHandler_UpdateCharGuildTitle@0xE9AD90):
	//   для каждого слота rec = title_list[id] → "<FC=грейд-цвет>имя</F>", строка на плашке = "s0 s1".
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	void SetTitles(int32 InTitleId0, int32 InTitleId1);

	// Применить название клана (цвет фиксированный) + id (−1 = нет клана → контрол пуст).
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	void SetGuild(const FString& InGuildName);
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	void SetClan(int32 InClanId, const FString& InClanName, const FString& InClanRankName);
	// Флаг (эмблема) клана — пакет 6542: включён + 40-байтный дизайн.
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	void SetClanFlag(bool bOn, const FAsdaClanFlagData& InFlag);
	// Лагерь/звание войны (эталон +209016/+209020).
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	void SetCamp(int32 InCampId, int32 InFactionGrade);
	// Место в рейтинге → бейдж 1668 (6068 rankValue, 6078 обновление ранга).
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	void SetRank(int32 InRankValue, int32 InRankCat = -1);

	// Запись title_rank по месту в рейтинге (эталон findRecordByKeyInRange: Category==cat && Min<=v<=Max).
	//   Возвращает false, если записи нет (эталон в этом случае оставляет бейдж пустым 32×35).
	// ★ Аудит №3 (A3): таблица берётся у `UAsdaItemTableSubsystem` — отсюда контекст мира.
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	static bool LookupRankBadge(const UObject* WorldCtx, int32 InRankValue, int32 InRankCat, FString& OutTexName,
		int32& OutSrcX, int32& OutSrcY, int32& OutW, int32& OutH);

	// ★ Альтернатор эталона (g_bIdleBlinkToggle, game_main.c:1415): true держится 30 000 мс,
	//   false — 5 000 мс. На камп-марке: true → эмблема клана (если флаг есть), false → иконка звания.
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	static bool IdleBlinkToggle(const UObject* WorldCtx);

	// Высота плашки над origin актора в МИРОВЫХ единицах (см). Компенсируется масштабом владельца.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	float WorldHeight = 200.f;

	// ★ ЕДИНАЯ высота плашки: ВЕРХ КОЛЛИЗИИ сущности + это значение (юзер 2026-07-29).
	//   Работает одинаково для мобов/NPC/игроков любого размера. Подобрано по месту: 10.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	float NameplateTopOffset = 10.f;

	// ★ ФИКСИРОВАННАЯ высота (эталон): плашка сидит на WorldHeight над origin владельца, габариты
	//   меша/капсулы НЕ учитываются. Так плашку позиционирует оригинал у ПИТОМЦЕВ
	//   (ui_core.c:99042-99060 — к мировому Y пета прибавляется flt_1461D84/flt_1461D88 ДО проекции).
	//   Дефолт false = прежнее поведение (верх коллизии + NameplateTopOffset) для мобов/NPC/игроков.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	bool bUseFixedWorldHeight = false;

	// ── ЗОНА ВИДИМОСТИ (эталон: дистанционного порога НЕТ; сущности просто не приходят за AoI сервера) ──
	// Сервер (GetCurrentPosInSectorIndex@0x46F2E0): карта 512×512 тайлов, СЕКТОР 30×30 тайлов, клиппинг 3×3
	//   сектора вокруг игрока (sub_46DC70, for i<9) → сущность видна на 30…60 тайлов (3000…6000 мир. ед.).
	//   Запечённые NPC у нас существуют всегда (не сетевые), поэтому AoI эмулируем ПОСЕКТОРНО — 1:1 с сервером.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	bool bLimitByServerAoI = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	int32 SectorTiles = 30;              // размер сектора в тайлах (MapInfo.txt, одинаков у всех 24 карт)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	int32 AoIRadiusSectors = 1;          // клиппинг 3×3 = радиус 1 сектор

	// ★ Плашка НАД СОБОЙ не рисуется (эталон CCharacter_Init:22044 — гейт !effectSlots[112].field_40:
	//   клон окна 1660 локальному игроку НЕ создаётся + явное SetVisible(0) при e==localPlayer).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	bool bHideForLocalPlayer = true;

	// ★ Постоянное имя (не только при наведении/таргете). Эталон: у МОБА постоянного имени нет — окно 1600
	//   показывается только для ВЫБРАННОЙ цели, 1660 — hover. Оставлено включаемым: удобство > 1:1,
	//   переключается в редакторе (для строгого 1:1 у мобов — снять флаг).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	bool bAlwaysShowName = true;

	// ★ Гориз. смещение окна MON: его контролы центрированы вокруг +51 px от начала окна (JUI 1603 −24 +
	//   150/2), и эталон ставит окно в screenX − ширина/2 с ресайзом. Для MAN1 контролы симметричны (0).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Nameplate")
	float ExtraScreenOffsetX = 0.f;

	// Применить все поля к виджету (имя/гильдия/титул/HP/цвет/высота). Идемпотентно.
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	void RefreshNameplate();

	// Быстрый сеттер HP (сеть): обновляет поля и заливку.
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	void SetHp(int32 InCur, int32 InMax, int32 InSegments = 0);
	// ★ Второе («восстановительное») здоровье для подложки. См. HpSecond.
	void SetHpSecond(int32 InSecond);

	// ★ ЕДИНАЯ точка имён сущностей (DataTable-ассеты из локализованных данных игры).
	//   Нужны и акторам (OnConstruction — имя видно в редакторе и до сетевого adoption), и мир-подсистеме.
	//   NPC: ключ строки = NpcId (npc_list поле@0, 1-based). Моб: строка = MobId−1 + гард по модели.
	//   ★ Волна 3 п.14: делегаты `AsdaEntityText::NpcName` / `MobName` (C++ зовёт их напрямую).
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	static FString LookupNpcName(int32 NpcId);
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	static FString LookupMobName(int32 MobId, const FString& ModelName);

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void InitWidget() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ★ «ВЕРХ AABB» сущности в мировых единицах ОТНОСИТЕЛЬНО origin владельца — та самая величина,
	//   которую эталон получает как helper_getSumF(ent+432/+424) = centerY + halfY (см.
	//   nameplate-height-from-data). Плашка садится на неё + NameplateTopOffset, облачко чата —
	//   на неё + 30 (персонаж) / +160 (моб) / −20 (NPC). Единая точка, чтобы обе системы брали
	//   высоту из ОДНОГО источника (данные сущности), а не считали её по-разному.
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	float GetEntityTopZ() const;
	// ★★★ [ОРИГИНАЛ] Мировая точка, которую проецирует плашка (08.10.2026, баг владельца п.3, `Reports/owner_1008/ui_bugs.md`):
	//   у ИГРОКА в состоянии действия 8 («сидит», +0x2E6FC — `UAsdaKingCrownSubsystem::EtalonActionStateOf`) — центр AABB
	//   (`posY` +448, без сдвига K: entity_char.c:9198-9227 постоянная плашка MAN1, game_main.c:4472 своя), иначе — точка
	//   компонента (верх + K). Та же точка у вывески личной лавки (`Chat_RenderHeadBubble`, chat.c:6538) — продавец в лавке
	//   сидит, и без этого вывеска опускалась к центру тела, а плашка оставалась наверху (большой зазор). Ключ `asda.Plate.SitCenter`.
	FVector GetPlateWorldPoint() const;

	// ★★★ ОБЩАЯ ТОЧКА «ГДЕ У ПЛАШКИ ВЕРХ» (заведена 2026-08-22 под подъём квестовой метки).
	//   ЗАЧЕМ. Плашка — НЕ трёхмерный объект: в игре её рисует `ScreenWidget` ручной проекцией,
	//   и окно ставится так, что его якорь лежит на 50 ЭКРАННЫХ единиц ВЫШЕ проекции точки
	//   `GetComponentLocation()` (`UpdateScreenPosition`, эталон `npcNameplateUpdateLoop`
	//   `screenY − 50`). Поэтому «над плашкой» в МИРОВЫХ единицах не выражается: одна и та же
	//   мировая прибавка даёт разный зазор на разном удалении камеры.
	//   ЧТО ВОЗВРАЩАЕТ. Сколько ЭКРАННЫХ единиц (тех же, в которых работает
	//   `SetPositionInViewport`, т.е. пикселей вьюпорта, делённых на DPI) отделяют проекцию
	//   якорной точки от ВЕРХА СТРОКИ ИМЕНИ — верхней видимой части плашки (у NPC гильдия,
	//   титул, ранг и HP погашены, остаётся только имя).
	//   ВЫВОД. `TopLeft.Y = screenY/DPI − 50 − kPlateAnchor.Y`, контрол имени лежит в канвасе на
	//   `SlotY` ⇒ верх имени = `screenY/DPI − (50 + kPlateAnchor.Y − SlotY)`. `SlotY` берётся у
	//   ЖИВОГО контрола (переживает перегенерацию плашек), фолбэк — раскладка генератора.
	UFUNCTION(BlueprintCallable, Category = "Asda|Nameplate")
	float GetPlateTopScreenOffset() const;

	// ★ ДИАГНОСТИКА: абсолютная (Slate-экранная) позиция ЛЕВОГО-ВЕРХНЕГО угла строки имени и её
	//   размер. Нужна, чтобы сверять расчёт с ФАКТИЧЕСКИ нарисованным виджетом, а не с формулой
	//   (закон `false-negatives-method`: формула и картинка разошлись — верить надо замеру).
	//   Возвращает false, если виджета/геометрии ещё нет.
	bool GetNameAbsRect(FVector2D& OutPos, FVector2D& OutSize) const;
	// ★ ДИАГНОСТИКА (08.10.2026, самотест вывески лавки `asda.Trade.SelfTestStallPlate`): экранный виджет плашки — чтобы
	//   снимать ЖИВУЮ геометрию её контролов (имя, титул, полоса HP, бейдж). nullptr — виджета ещё нет.
	UUserWidget* DebugScreenWidget() const { return ScreenWidget; }
	// ★ ДИАГНОСТИКА (29.10.2026, самотест `asda.Overhead.SelfTestOcclude` и дамп `asda.Overhead.OccludeProbe`): состояние
	//   перекрытия плашки препятствиями (принятое «перекрыто», показанная непрозрачность).
	const FAsdaOccludeState& DebugOccState() const { return OccState; }

	// Применить высоту (верх капсулы-хитбокса + оффсет; масштаб владельца компенсируется). ПУБЛИЧНО:
	//   при ДИНАМИЧЕСКОМ спавне OnRegister отрабатывает ДО подгонки капсулы в OnConstruction (дефолт 45/80
	//   при scale 0.01 → верх ≈ 0.8 → плашка посредине модели) — пересчитать после подгонки/смены меша.
	void ApplyHeight();

	// ★ ДЕДУП Д13 (`Reports/quest_npc_dedup_audit.md`). «Высота ИЗ ДАННЫХ, а если данных нет —
	//   прежний расчёт по капсуле». Это ОДНО правило, но оно жило двумя копиями — у актёра NPC и у
	//   актёра моба (и просилось третьим у пета). Теперь оно здесь, у самой плашки.
	//   H > 0  → `bUseFixedWorldHeight = true`, `WorldHeight = H` (npc_list +0x68 / Mob list +0x84);
	//   H <= 0 → `bUseFixedWorldHeight = false` (верх капсулы-хитбокса + `NameplateTopOffset`).
	//   В обоих случаях сразу зовётся `ApplyHeight()`.
	void ApplyHeightFromData(float H);

private:
	// Верх хитбокса (капсула пикинга; фолбэк — габариты меша) относительно origin. 0 = данных нет.
	float ComputeHitboxTopZ() const;
	// Путь к JUI-виджету плашки по типу.
	const TCHAR* WidgetPathForKind() const;
	// Заливка полосы HP (эталон uiUpdateSegmentedExpBar: 100px = полная; сегменты = «жизни»).
	void ApplyHpBars();
	// ★ Подложка восстановления: «хвост» от текущего здоровья до второго значения. Виджета для
	//   неё в JUI нет, поэтому создаём свой UImage рядом с заливкой (кисть берём у заливки).
	void ApplyHpSecondGhost(class UUserWidget* W, const TCHAR* FillName, float Ratio, bool bWantHp);
	TWeakObjectPtr<class UImage>      HpGhostImage;   // созданная подложка
	TWeakObjectPtr<class UUserWidget> HpGhostOwner;   // виджет, в котором она живёт (пересоздание при смене)
	// ★ Плавный ход полосы здоровья (см. FAsdaBarSmoother). Само поле `CurHp` остаётся точным —
	//   сглаживается ТОЛЬКО отрисовка, чтобы логика (смерть, порог агрессии) не поехала.
	FAsdaBarSmoother HpSmooth;
	// Текст в контрол по имени (через единую точку UAsdaRichText).
	void SetControlText(const TCHAR* CtrlName, const FString& Text, bool bWantVisible);
	// Текст с явным цветом (гильдия/титул).
	void SetControlTextColored(const TCHAR* CtrlName, const FString& Text, bool bWantVisible, const FLinearColor& Color);

	// ★ НАШ суффикс «(Одержим)» под именем. Контрол `TARGETMARK_POSSESS` живёт В АССЕТЕ плашки
	//   (генератор `Tools/asda_build_nameplates.py`, закон no-runtime-widget-fixes) — здесь только
	//   текст и видимость, ровно как у имени/гильдии/титула. Показ гейтится SoulState==2, видом
	//   плашки (только игрок) и ключом `asda.Nameplate.PossessTag`.
	void ApplyPossessTag(class UUserWidget* W);
	// Последнее увиденное значение ключей одержимости — чтобы `asda.Nameplate.PossessTag 0` и
	//   `asda.Possess.NameColor 0` гасили НЕМЕДЛЕННО, а не ждали следующего события плашки.
	int32 LastPossessTagCVar   = -1;
	int32 LastPossessColorCVar = -1;
	// Показать/скрыть контрол по имени.
	void SetControlVisible(const TCHAR* CtrlName, bool bWantVisible);

	// ── Порт §3/§4/§6 отчёта guild_title_nameplate_re.md ─────────────────────────────────────────
	// Раскладка детей (эталон charNamePlate_LayoutControls@0xA4B4E0): сдвиги гильдии/бейджа
	//   +16 (нет титула), +16 (нет клана), −34 (открыт магазин), −5 (всё три сразу).
	void LayoutControls();
	// Камп-марка/фракция (эталон CharNamePlate_UpdateFactionUI@0x9DCED0): центровка по max-ширине текста,
	//   иконка звания ИЛИ эмблема клана по альтернатору, ровно один цветной бокс по CampId.
	void UpdateFactionUI();
	// Бейдж места в рейтинге (эталон uiLevelBadgeUpdateTexture@0xA4B270).
	void UpdateRankBadge();
	// Строка титула с разметкой цвета по грейду для КАЖДОГО слота ("<FC=..>t0</F> <FC=..>t1</F>").
	FString BuildTitleMarkup() const;
	// Текст титула через единую точку разметки (URichTextBlock) — два цвета в одном контроле, как эталон.
	void ApplyTitleMarkup();

	// Картинка в контрол: текстура + UV-регион (доли) + размер слота (эталон CControl_DrawTextureRect
	//   + vtable SetSize) + тинт (MODULATE текстура×цвет, как TEXTUREFACTOR). Пустая текстура → скрыт.
	void SetControlTexture(const TCHAR* CtrlName, class UTexture* Tex, const FBox2D& UV, const FVector2D& SizePx,
		const FLinearColor& Tint = FLinearColor::White);
	// Позиция контрола в канвасе плашки (левый-верхний угол, база JUI).
	void SetControlPos(const TCHAR* CtrlName, const FVector2D& Pos);
	// Базовая (авторская JUI) позиция контрола — снимается один раз, как эталон снимает gap_3E4 при инстансе.
	FVector2D GetBasePos(const TCHAR* CtrlName);
	// Ширина строки в шрифте контрола (аналог textFontMeasureMarkupString для центровки камп-марки).
	float MeasureTextWidth(const TCHAR* CtrlName, const FString& Text) const;

	// Кэш авторских позиций контролов (заполняется при первом Refresh на созданном виджете).
	TMap<FName, FVector2D> BasePos;
	// Последнее состояние альтернатора (перерисовываем камп-марку только на переключении, как эталон).
	int32 LastBlink = -1;
	// ★ Повторный Refresh после создания виджета: на кадре создания Slate ещё не прогнал prepass —
	//   LayoutControls (бейдж рейтинга/камп-марка) бьёт в нулевые измерения и не рисует; до фикса
	//   бейджи «появлялись только после клика по игроку» (клик звал Refresh повторно) — юзер 2026-07-30.
	int32 InitialRefreshTicks = 0;

	// ★ Волна CPU 07.09.2026: значимость плашки (UAsdaVisibilitySubsystem). 0 — владелец вне расширенного
	//   конуса камеры и дальше ближней зоны: тик плашки не делает ничего, экранный виджет освобождён.
	//   Своя плашка (владелец = павн игрока) НИКОГДА не гейтится (закон self-nameplate-same-as-others).
	UPROPERTY() FAsdaVisState VisState;

	// ★ В ИГРЕ плашка рисуется СОБСТВЕННЫМ viewport-виджетом с РУЧНОЙ проекцией мира в экран
	//   (эталон: SetPos(root, screenX − ширина/2, screenY − 50)). Screen-space рендер самого
	//   UWidgetComponent не используем — он давал «нет плашек»/уплывание. В РЕДАКТОРЕ остаётся
	//   World-space превью самого компонента (видно в сцене — правило ue-editor-friendly).
	UPROPERTY(Transient)
	TObjectPtr<class UUserWidget> ScreenWidget = nullptr;
	// ★★★ ДЕДУП СЛОТА ВЬЮПОРТА (правка 07.09.2026, cpu_nameplate_report.md).
	//   `UUserWidget::SetPositionInViewport`/`SetDesiredSizeInViewport` (UserWidget.cpp:1527/1545) — НЕ
	//   присвоение поля: каждая ищет виджет в карте UGameViewportSubsystem, переписывает пять свойств
	//   слота SConstraintCanvas и дёргает `WidgetHost->Invalidate(EInvalidateWidgetReason::Layout)`
	//   (GameViewportSubsystem.cpp:249) — то есть помечает ВЕСЬ полноэкранный канвас на пересчёт раскладки.
	//   До правки обе звались у КАЖДОЙ плашки КАЖДЫЙ кадр, причём размер задавался ОДИН И ТОТ ЖЕ
	//   (kPlateCanvas) — чистый холостой ход. Теперь: размер ставится один раз на виджет, позиция —
	//   только при реальном сдвиге (порог ниже полупикселя: меньше на экране не видно).
	FVector2D LastTopLeft = FVector2D::ZeroVector;   // последняя ПОСТАВЛЕННАЯ позиция слота (пиксели UMG)
	bool bTopLeftValid = false;                      // позиция хоть раз ставилась (иначе порог не применяем)
	bool bViewportSlotSized = false;                 // размер слота уже задан (SetDesiredSizeInViewport)
	// Последняя ПОСТАВЛЕННАЯ видимость экранного виджета (255 = ещё не ставилась). Хранится, чтобы не
	//   звать SetVisibility у сотен плашек каждый кадр ради того же самого значения.
	uint8 LastScreenVis = 255;
	// ★★★ [ОРИГИНАЛ] Перекрытие плашки геометрией мира (29.10.2026, баг игроков 3; разбор — `World/AsdaOverheadOcclusion.h`):
	//   у оригинала плашка проходит z-тест на глубине своей точки над головой. Состояние луча и плавного хода — здесь.
	FAsdaOccludeState OccState;
	// Последняя ПОСТАВЛЕННАЯ непрозрачность экранного виджета от перекрытия (−1 — ещё не ставилась): SetRenderOpacity — только при смене.
	float LastOccOpacity = -1.f;

	// Создать/убрать viewport-виджет (только игровой мир).
	void EnsureScreenWidget();
	void DestroyScreenWidget();
	// Спроецировать позицию компонента и поставить плашку (или скрыть, если за камерой/вне фрустума).
	void UpdateScreenPosition();
	// ★ Гейт значимости (волна CPU 07.09.2026 + доводка): true = плашке в этом кадре делать нечего.
	//   Вызывается ДО Super::TickComponent (asda.Vis.GateBeforeSuper 1, по умолчанию) или ПОСЛЕ (0) —
	//   обоснование порядка и цены Super см. в шапке реализации.
	bool ShouldSkipTickByVisibility();
};
