// Copyright (c) 2026 @sitxovski (Discord)
// AAsdaPlayerController — ПОЛНАЯ система управления игроком (порт keybindLoadDefaults@0xE3A9C0 +
//   hotkeyDispatchAction@0xE3CD90 + input_SetMoveFlagFromKey@0xE3CCE0). Все 85 команд из оригинала:
//   - Движение (cmd 51-54 = WASD, реверс Player_KeyboardMoveTick math_geom.c:380): W вперёд по камере,
//     S ПЯТИТСЯ к камере (лицом вперёд, клип back01); конфликт W+S — ПОСЛЕДНЯЯ нажатая побеждает (латч).
//     A/D БЕЗ ПКМ = ПЛАВНЫЙ РАЗВОРОТ камеры+персонажа 0.05 рад/31.25мс = 91.67°/с (знак инверт. при S,
//     как руль задним ходом); A/D С ПКМ (эталон GetAsyncKeyState(2)) = СТРЕЙФ. Пока движемся — 4006 к
//     точке dir×800 впереди (троттл эталона: свежее нажатие сразу, смена направления 250мс, стабильно
//     1000мс), стоп 4008 по отпуску. Автобег: опция + удержание W 3с (S/ЛКМ отменяет).
//   - Камера: ТОЛЬКО мышь (в оригинале клавиш камеры нет). ПКМ-драг орбит 0.01 рад/px, колесо зум [170..1270].
//     ЛКМ по земле = click-to-move (4006).
//   - Прочие команды (хотбар/окна/скиллы/соц/эмоции/чат/скриншот) срабатывают на ОТПУСКАНИЕ клавиши
//     (эталон: WM_KEYUP → hotkeyDispatchAction). Цели-подсистемы (инвентарь/скиллы/HUD) появляются
//     позже — команда транслируется делегатом OnGameCommand(cmd) + логом с именем действия; движение/сесть/
//     скриншот исполняются сразу. Ремап/сохранение — UAsdaKeybindConfig.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "AsdaPlayerController.generated.h"

class UAsdaKeybindConfig;
class UAsdaKeybindProfile;

// Делегат исполнения команды (cmd 1..85), на которую в порту ещё нет подсистемы-цели (окна/хотбар/чат).
//   Будущие системы подписываются и реагируют — управление уже полностью распознаётся.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAsdaGameCommand, int32, Cmd);

// ── ★ ТОЧКИ ВХОДА ПАНЕЛИ БЫСТРОГО ДОСТУПА (CHotbar) ─────────────────────────────────────────────
//   Клавиши хотбара разобраны и подключены здесь; сама панель (окно QUICK_NEW_WIN 73000, модель
//   78×10 байт, сеть 5059/5067, применение слота) делается отдельно. Чтобы стыковка не зависела от
//   разбора номеров команд, наружу торчат ДВА именованных делегата — подсистема хотбара просто
//   подписывается на них.
//
//   Соответствие эталону (`hotkeyDispatchAction@0xE3CD90`, два последовательных switch по cmd):
//     cmd 1..5   → `Hotbar_SetPage(g_pQuickSlotBar, 0..4)`            → OnHotbarPage(PageIndex 0..4)
//     cmd 6..17  → `Hotbar_ActivateSlotByIndex(…, 0..11)`             → OnHotbarSlot(Column 0..11)
//     cmd 18..29 → `Hotbar_ActivateSlotByIndex(…, 12..23)`            → OnHotbarSlot(Column 12..23)
//   Колонка → индекс слота внутри CHotbar (`Hotbar_ActivateSlotByIndex@0xDEE650`):
//     Column 0..11  → slot = Column + 13*ТекущаяСтраница   (основной ряд, страницы 0..4)
//     Column 12..23 → slot = Column + 53                   (второй ряд «NEW BAR», 65..76)
//   13-я колонка страницы (индексы 12/25/38/51/64) клавиши НЕ имеет — «BAR SLOT» ровно 12 штук.
//   Событие приходит на ОТПУСКАНИЕ клавиши (эталон исполняет действия в WM_KEYUP-диспетче).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAsdaHotbarPage, int32, PageIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAsdaHotbarSlot, int32, Column);

UCLASS()
class ASDAREBORN_API AAsdaPlayerController : public APlayerController
{
	GENERATED_BODY()
	// ★ 27.09.2026: самотест своего 4007 (`AsdaMove4007SelfTest.cpp`) задаёт маршрут из нескольких узлов напрямую.
	friend struct FAsdaMove4007SelfTest;   // он же ведёт самотест хода клавишами `asda.Move.SelfTestKeyboard` (27.09.2026)
	friend struct FAsdaPkt4024SelfTest;    // ★ 27.09.2026: самотест «сесть/встать» `asda.Net.SelfTestSitStand` — клавиша 55 и клик по миру
	friend struct FAsdaTradeGatesSelfTest; // ★ 29.09.2026: самотест гейтов торговли `asda.Trade.SelfTestGates` — `KbCanMoveCheck` (ПД-2)
	friend struct FAsdaSelfStoreL5SelfTest; // ★ 30.09.2026: самотест гейтов лавки `asda.Trade.SelfTestStoreL5` — клик по земле, верхний гейт клавиш
	friend struct FAsdaFishAnimSelfTest;   // ★ 09.10.2026 (В5fix рыбалки): `asda.Fish.AnimSelfTest` — `KbEtalonActionState` при действии 60…67
	friend struct FAsdaFishZoneSelfTest;   // ★ 08.10.2026 (волна В4 рыбалки): `asda.Fish.ZoneSelfTest` — двойной клик по зоне, гейт рыбалки
	friend struct FAsdaActionStateSelfTest; // ★ 12.10.2026 (дедуп №2, В3 S7): `asda.Action.SelfTest` — `KbEtalonActionState` из единого состояния
	friend class UAsdaInventorySubsystem;  // ★ 09.10.2026 (six_items СРЕДН-2): гейт копания `ApplyShovelingTool` — `KbEtalonActionState` (+47551)
	friend struct FAsdaEscChainAccess;     // ★ 25.10.2026: ветки состояния цепочки ESC (`AsdaEscChain::RunStateBranches`) — `KbEtalonActionState`
	friend struct FAsdaClosedTilesClickAccess; // ★ 04.11.2026: `asda.SelfTest.ClosedTiles` — мировой клик по кольцу/свечению портала (`OnLeftClick`)
	friend struct FAsdaSkillCastLockAccess;    // ★ 05.11.2026: «свой навык занят» (`World/AsdaSkillCastLock`) — повтор клика/клавиши после навыка

public:
	AAsdaPlayerController();

	// Конфиг клавиш (дефолты → профиль → system/Shortcutkey.ini). Доступен окну опций-переназначения.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Input")
	TObjectPtr<UAsdaKeybindConfig> Keys;

	// ★ Editor-Friendly раскладка: DataAsset с редакторным слоем биндов + путь к ini
	//   (правило [[ue-editor-friendly]]). Не задан — работают дефолты + автопоиск ini.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Input")
	TObjectPtr<UAsdaKeybindProfile> KeybindProfile;

	// Команда без ещё-портированной цели → сюда (окна/скиллы/соц/эмоции). cmd из keybind_actions.md.
	UPROPERTY(BlueprintAssignable, Category="Asda|Input")
	FOnAsdaGameCommand OnGameCommand;

	// ★ Панель быстрого доступа: страница (cmd 1..5) и слот (cmd 6..29). См. шапку делегатов выше.
	UPROPERTY(BlueprintAssignable, Category="Asda|Hotbar")
	FOnAsdaHotbarPage OnHotbarPage;
	UPROPERTY(BlueprintAssignable, Category="Asda|Hotbar")
	FOnAsdaHotbarSlot OnHotbarSlot;

protected:
	virtual void BeginPlay() override;
	// ★ 10.10.2026 (modal_lock2): снять препроцессор мыши под модалкой (`ModalMouseRelay`).
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

public:
	// ★★★ ВОЛНА В2 РЫБАЛКИ (08.10.2026): ФИЛЬТР КЛАВИШ ДО ВСЕГО ОСТАЛЬНОГО — порт `MainWndProc@0x9841A0` WM_KEYDOWN:
	//   1) `Input_FishingKeyFilter@0xFC6F10` (гайд мини-игры в state 4: ЛЮБОЕ нажатие/автоповтор = ответ, нумпад 8/4/2/6 =
	//      стрелки; Alt-сочетания — WM_SYSKEYDOWN — не засчитываются);
	//   2) пока идёт рыбалка (fishState ≠ 0) нажатие не уходит ни в хоткеи, ни в движение (`game.c:24144`); проходят
	//      только Esc и Enter (их KEYUP эталон пропускает, `game.c:24218`; PrintScreen у UE — платформенный опрос).
	//   true — событие съедено и в `UPlayerInput` не попадает (клавиша не «зажата» для `PollKeys`).
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	// ★ Волна В2: сам фильтр рыбалки (см. `InputKey`). true — событие съедено. Публичный — самотест проверяет пропуск
	//   Esc/Enter, не отдавая их в `UPlayerInput` (иначе открылось бы ESC-меню / чат).
	bool FishFilterKey(const FInputKeyEventArgs& Params);
	// ★ Ревизия В2 ВАЖН-1 (волна В4): сброс нажатых клавиш (потеря фокуса) гасит и признак Alt фильтра мини-игры.
	virtual void FlushPressedKeys() override;

public:
	// ── ★★★ ЕДИНАЯ ТОЧКА: УГОЛ ОРБИТ-КАМЕРЫ (эталонный `g_fCameraYaw`) ───────────────────────────
	//   Орбит-угол копится ЗДЕСЬ (ПКМ-драг 0.01 рад/px + плавный разворот A/D) и отсюда же едет в
	//   `SpringArm`. Никакого `ControlRotation` порт не трогает — у click-to-move камеры его никто
	//   не пишет, он вечно равен значению на спавне. Поэтому ВСЕ потребители «куда смотрит камера»
	//   (конус обзора мини-карты и карты мира) обязаны спрашивать угол ТОЛЬКО через этот геттер.
	//   Возвращается yaw в ГРАДУСАХ UE (эталон копит радианы; знак зеркален из-за свопа осей
	//   игра→UE, см. `AsdaCoord`).
	UFUNCTION(BlueprintPure, Category="Asda|Camera")
	float GetCameraYawUE() const { return CamYawDeg; }
	// ★ 12.10.2026 (автоподбор графики, UAsdaBenchSubsystem): угол наклона орбит-камеры (градусы UE) и постановка обоих
	//   углов снаружи — камера k1_a1 кампании производительности и плавный поворот по ходу прохода замера. Та же
	//   единственная точка (`CamYawDeg`/`CamPitchDeg` → `SpringArm`), что у ПКМ-драга; к павну применяется сразу.
	float GetCameraPitchUE() const { return CamPitchDeg; }
	void SetOrbitAnglesForBench(float YawDeg, float PitchDeg);

	// ── ★ СИСТЕМА ТАРГЕТА (порт targeting_system_re.md) ──────────────────────────────────────────
	// Что под курсором. Эталон различает 4 вида сущностей + геометрию; порядок приоритета РАЗНЫЙ
	//   для наведения и для клика (см. ниже).
	enum class EAsdaPickKind : uint8 { None, Ground, Mob, Player, Npc, Loot };

	// Результат пикинга: вид, актор, точка попадания.
	struct FAsdaPick
	{
		EAsdaPickKind Kind = EAsdaPickKind::None;
		TWeakObjectPtr<AActor> Actor;
		FVector Point = FVector::ZeroVector;
		// ★ Ревизия В4 ВАЖН-2 (рыбалка, 09.10.2026): компонент, в который попал луч. Объекты карты в порту — экземпляры ISM на
		//   простом акторе `AsdaMapObjects_<карта>` (`Tools/asda_build_map_level.py`), поэтому тип 4 эталона
		//   (`collide_rayVsMapObjects`) распознаётся по КОМПОНЕНТУ, а не по классу актора (`UAsdaFishZoneSubsystem::IsMapObjectHit`).
		TWeakObjectPtr<UPrimitiveComponent> Component;
	};

	// НАВЕДЕНИЕ (каждый кадр, эталон Input_PickEntityHover@0x993DD0): среди сущностей побеждает
	//   БЛИЖАЙШАЯ (порядок списков игроки→мобы→лут→NPC), но если геометрия карты ближе — наведение
	//   отменяется (окклюзия collide_rayVsMapObjects).
	FAsdaPick PickHover() const;
	// КЛИК (эталон Picking_TestRayPickWorldTarget@0x992D60): приоритет моб→игрок→лут→NPC→геометрия.
	FAsdaPick PickForClick() const;
	// ★ Луч мира под экранной точкой, СКВОЗЬ неотрисованных (скрытых) персонажей — общая точка
	//   наведения/клика/aim-assist (эталон берёт в пикинг только отрисовываемых, см. .cpp). Нужен и
	//   самотесту `asda.Target.SelfTestPickHidden`, где мыши нет. true = есть попадание в `OutHit`.
	bool TraceScreenSkippingUnrenderedChars(const FVector2D& ScreenPos, FHitResult& OutHit) const;
	// ★ Волна В4 рыбалки (08.10.2026): экранная точка мирового клика — позиция мыши (эталон `Point`, запомненная
	//   `MainWndProc` при WM_LBUTTONDOWN/DBLCLK); самотест подменяет её `DebugClickScreen` (мыши в `-RenderOffscreen` нет).
	bool GetClickScreenPos(float& OutX, float& OutY) const;
	// ★ Волна В4 рыбалки: ЗОНА РЫБАЛКИ под точкой ДВОЙНОГО клика — тип попадания 5 `Picking_TestRayPickWorldTarget@0x992D60`
	//   (game.c:30370-30383, только при `a6 = 1`, т.е. из `world_OnLeftClickPick@0x966980` = WM_LBUTTONDBLCLK; окно класса
	//   зарегистрировано со стилем 43 = CS_DBLCLKS, game.c:23641). Зона проверяется ПОСЛЕДНЕЙ и побеждает только СТРОГО
	//   ближе прочих попаданий (`bestDist > hitDist`, равенство — за ранее найденным типом). Соперники — сущности (моб/игрок/
	//   лут/NPC, типы 0…3) и геометрия объектов карты (тип 4 `collide_rayVsMapObjects`); террейн и вода в соревновании
	//   эталона не участвуют. `Best` — итог `PickForClick` для той же точки. Возврат — индекс спота или −1.
	//   `bQuiet` — без строк журнала об отказе (проба одиночного нажатия перед локальным стартом, 23.10.2026).
	int32 PickFishZoneForDoubleClick(const FAsdaPick& Best, float* OutZoneDist = nullptr, bool bQuiet = false) const;

	// Текущее наведение (для плашки имени: у мобов имя видно ТОЛЬКО при наведении — эталон).
	UPROPERTY(VisibleInstanceOnly, Category="Asda|Target")
	TWeakObjectPtr<AActor> HoverActor;
	// ВЫБРАННАЯ цель (эталон g_nTargetUserIdx; клик по пустому месту цель НЕ снимает —
	//   снимают ESC, деспавн/смерть цели, выход из мира).
	UPROPERTY(VisibleInstanceOnly, Category="Asda|Target")
	TWeakObjectPtr<AActor> TargetActor;

	// Выбрать/снять цель (обновляет плашки: у цели-моба показывается HP-панель).
	void SetTarget(AActor* NewTarget);
	UFUNCTION(BlueprintCallable, Category="Asda|Target")
	void ClearTarget();          // ESC / смерть / деспавн
	// Обновление наведения каждый кадр (плашка имени + курсор).
	void UpdateHover();

	// ── ★ ПОДСВЕТКА ВЫБРАННОЙ ЦЕЛИ (порт Reports/target_selection.md) ──
	//   Эталон: g_pTargetHighlightPosA/B (game_main.c:1449) → узлы меша цели рисуются с
	//   NoSkinTexScroll.z=3.0 вместо 1.0 → в .fx «Change Brightness»: RGB += z*0.1 (дельта +0.2).
	//   Мёртвая цель (actionState==10) и локальный игрок подсветку НЕ получают.
	//   Настройки — DataAsset ниже (если не задан, берутся дефолты компонента).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Target")
	TObjectPtr<class UAsdaTargetHighlightSettings> HighlightSettings = nullptr;
	// Навесить/снять подсветку на актора (сама заводит компонент, если его ещё нет).
	void ApplyTargetHighlight(AActor* Actor, bool bOn);
	// Каждый кадр: гасим подсветку у умершей цели (эталон Entity_GetActionState != 10).
	void TargetHighlightTick();
	// ★★★ Кольцо под целью сейчас показано? Кэш для `TargetHighlightTick` — компонента подсветки
	//   при дефолтном режиме `None` может не быть вовсе, а спрашивать эффект-актор по тегу каждый
	//   кадр дороже, чем держать один bool. Ставится в `ApplyTargetHighlight` (единая точка смены
	//   выделения) и в тике при смерти цели. Отступление от эталона — см. `CVarAsdaTargetRing`.
	bool bTargetRingShown = false;

	// ★ Клик по МИНИ-КАРТЕ / БОЛЬШОЙ КАРТЕ → идти к мировой точке.
	//   Эталон (MiniMap_RenderBigMap@0xB4FCC0, world_map.c:2596-2612): тайл клика → world = 100*t+50 →
	//   GroundTarget_PickOnMesh (точка валидна?) → mapClick_BeginPathMove. Публичная обёртка над
	//   RequestMoveTo, чтобы UAsdaMinimapSubsystem не лез в приватную часть контроллера.
	//   ★ 30.09.2026 (волна Л5 торговли): тело — в .cpp (гейт «личная лавка открыта» `mapClick_BeginPathMove`, молча).
	UFUNCTION(BlueprintCallable, Category="Asda|Move")
	void MoveToGamePoint(const FVector2D& DstGame);

	// ★ КУРСОРЫ (порт cursor_states.md, селектор sub_923BA4 + Cursor_SetByIndex@0xEC8DA0).
	//   Оригинал грузит .cur/.ani из JUIData/cursor и переключает по СОСТОЯНИЮ наведения:
	//     nomal(BO)=дефолт · battle(AO)=враг/атака · talk(TK)=NPC · hand(ID)=предмет с земли ·
	//     move(Move)=точка движения · nomove(noMove)=непроходимо · hammer(HM)/bottle(HO)/N0..N2 — спец-режимы.
	//   Файлы уже импортированы в Content/Slate/Cursors (те же, что в оригинале).
	//   ★ Hammer/Bottle добавлены системой самоцветов: эталон переключает их через
	//     `registry_FindEntryByName("bottle"/"hammer")` при взводе «святого» предмета и при удачном
	//     применении (`net_sendSecuredRequest_5031` / `sowelWndOnSocketResultPacket`).
	//     Файлы HO.ani (bottle) и HM.ani (hammer) уже лежат в Content/Slate/Cursors.
	//   ★ Pick (31.10.2026) — индекс 26 массива курсоров = P0.ani: эталон ставит его взводом камня переноса
	//     улучшения (`colorMix_BeginPick@0xB65530` → `Cursor_SetByIndex(…, 26, 1)`), файл — Content/Slate/Cursors/P0.
	enum class EAsdaCursor : uint8 { Normal, Battle, Talk, Hand, Move, NoMove, Hammer, Bottle, Pick };
	void SetGameCursor(EAsdaCursor C);
	EAsdaCursor CurrentCursor = EAsdaCursor::Normal;
	// ★ Курсор занят интерфейсом: пока true, наведение на мир курсор НЕ трогает. Ставит UI, который
	//   сам задаёт курсор поверх мира — сейчас это поле большой карты (эталон CBigMap_RenderAndHandleClick
	//   выставляет курсор 18 «идти» / 20 «нельзя», пока мышь над картой). Без этого флага мировой
	//   UpdateHover и карта переставляли бы курсор каждый кадр по очереди (мерцание).
	bool bUiCursorOverride = false;

	// ★ МЕТКА КЛИКА ПО ЗЕМЛЕ (порт click_marker_effect_re.md): эффект eff_0_Char_99_mtget01-a_00.jeb
	//   («move target»). Спавнится В ТОЧКЕ клика, посаженной на террейн, +20 по вертикали; живёт ~1.1 с
	//   и удаляется САМ; при новом клике старая метка НЕ гасится. Только для клика по ЗЕМЛЕ (по сущности
	//   метки нет), на непроходимой точке — метки нет (там сообщение+звук ошибки).
	//   Эталон ещё ПОВТОРЯЕТ метку ~раз в секунду у точки назначения во время автобега.
	void SpawnClickMarker(const FVector& WorldPoint);
	UPROPERTY(EditAnywhere, Category="Asda|Target")
	TSubclassOf<AActor> ClickMarkerClass;          // BP_ClickMarker (заготовка из JEB)
	FVector  LastMoveDestUe = FVector::ZeroVector; // точка назначения (для повторной метки в пути)
	double   LastMarkerTime = 0.0;                 // время последнего спавна метки (повтор ~1 с)
	bool     bMovingToPoint = false;               // идём к точке (для повтора метки)

	// ── ★ БОЕВОЙ ЦИКЛ (порт FSM=5 + ResolveBasicAttack): второй клик по мобу взводит bAutoAttack;
	//   AttackTick: вне дальности — подход 4006 с tgtRenderId (Combat_SendNormalAttack), в дальности —
	//   удары 4026. Гасится сменой цели / кликом по земле / смертью цели. ──
	UPROPERTY(EditAnywhere, Category="Asda|Combat")
	float AttackRange = 150.f;    // боевой радиус эталона (22500=150²); пер-оружейные радиусы — фаза боя
	// ★ Гистерезис погони: войдя в бой, держим стойку пока цель ближе Range×этого (иначе бродящий моб
	//   вызывает дребезг «шаг-удар-шаг-удар», сервер видит движение и перестаёт отвечать на замахи).
	UPROPERTY(EditAnywhere, Category="Asda|Combat")
	float ChaseHysteresis = 1.6f;
	bool  bInCombatStance = false;   // стоим в боевой стойке боя (стоп уже отправлен)
	// ★ БОЕВАЯ СТОЙКА ПОСЛЕ БОЯ (эталон + уточнение юзера): после смерти цели персонаж стоит в боевой
	//   стойке (клип idle01), не атакуя, пока сервер не пришлёт сброс (4205) или не истечёт кулдаун.
	UPROPERTY(EditAnywhere, Category="Asda|Combat")
	// ★ 10 с — ТОЧНОЕ значение эталона: тик покоя CGameUI_UpdateFadeAnim@0xA17B00 (game_main.c:7867/8118)
	//   при субрежиме 20 проверяет `timeGetTime() − fAutoActionStartTime > 10000.0` и уводит покой в
	//   субрежим 15 (фейд → мирный покой). Взводится/перевзводится КАЖДЫМ боевым действием.
	float CombatStanceHold = 10.f;   // кулдаун стойки, если сервер не прислал 4205
	double CombatStanceUntil = 0.0;
	void ExitCombatStance();         // сброс стойки (по 4205 или таймеру)
	UPROPERTY(EditAnywhere, Category="Asda|Combat")
	float AttackPeriod = 1.6f;    // период ударов (врем., до реверса скорости клипа 6 оружия)
	bool  bAutoAttack = false;
	bool  bCombatStarted = false;   // первый пакет боя = 4026, замахи дальше = 4028 (эталон)
	// ★ 17.10.2026 (ревизия правки боя, п.5): 4026 ушла — уточнить окно «наш замах в полёте» после старта замаха (тот
	//   может стартовать в следующем тике, если этот ушёл в ранний выход ожидания серверных часов).
	bool  bArmImpactOnSwingStart = false;
	// ★ Ждём ответ сервера на замах (4029/4027 с очередью ударов) — следующий 4028 только после него
	//   (эталон: вход в атак-состояние по ответу). Таймаут — страховка от потери пакета.
	bool  bWaitingCombatAck = false;
	// Эталон: без ответа сервера персонаж падает в idle через 2500 мс (поле field8C = 2500.0).
	UPROPERTY(EditAnywhere, Category="Asda|Combat")
	float CombatAckTimeout = 2.5f;
	double NextAttackTickTime = 0.0;
	double NextApproachTime = 0.0;   // троттл пакетов подхода (отдельно от боевого гейта)
	// Ответ пришёл: планируем следующий замах через длительность серии ударов.
	void OnCombatAckReceived(float SeriesLen);
	// ★★★ РИТМ ОТ АНИМАЦИИ (правка 2026-08-25). Зовётся персонажем в момент, когда проход клипа
	//   атаки завершился и начинается следующий. Именно так устроен оригинал: следующий удар
	//   отправляется ИЗНУТРИ тика замаха (`skill_buff.c:762-793`), а не по таймеру ответа сервера.
	//   До правки анимация и пакеты жили по двум независимым таймерам (0.85 с против 1.31 с) —
	//   примерно каждый третий взмах уходил впустую, а снаряд вылетал в произвольной фазе клипа.
	void OnSwingCycleCompleted();
	// ★★★ 15.10.2026 (ритм атаки): точка отправки пришла РАНЬШЕ серверной длительности (кадр, отложенный
	//   ответ часов) — 4028 откладывается на остаток ожидания этим таймером; там же повторяются все проверки.
	FTimerHandle DeferredAttackSendTimer;
	// Отложенный 4028 ждёт часов (`UAsdaAttackRhythmSubsystem`). Темп следующего отрезка замаха учитывает его.
	bool IsAttackSendDeferred() const;
	// ★ Ревизия СРЕДН-2: дальше этого (с) дедлайн не ставится на поток точной отправки сразу — повтор таймером за кадр до него.
	static constexpr double kTimedSendAheadS = 0.25;
	// ★ Ревизия НИЗК-2: сторож «навык занят» в боевом тике — с какого момента (FPlatformTime, 0 — не занят) и предел.
	double CastBusySinceS = 0.0;
	bool bCastBusyWatchdogLogged = false;
	static constexpr double kCastBusyWatchdogS = 6.0;
	// ★ 15.10.2026: «второй клик» по уже выбранному мобу = боевой режим (прежнее тело ветки Mob в `OnLeftClick`).
	//   Повторный клик по текущей цели посреди идущей серии НОВОЙ 4026 не даёт (эталон case 5, та же цель).
	void EngageMobFromClick(AActor* Mob);
	// ═══ ★★★ 17.10.2026 (синхронизация боя, Ф4) PvP: ОБЫЧНАЯ АТАКА ИГРОКА ПО ИГРОКУ — сторона атакующего (разбор в .cpp) ═══
	bool  bPvpAttack = false;          // идёт PvP-атака: цель — персонаж, пакеты 4201/4203 (а не 4026/4028)
	bool  bPvpAccepted = false;        // 4202 код 1 получен — подход/замах разрешены
	int32 PvpTargetUserIdx = -1;       // userIdx цели (i16 в теле 4201/4203; НЕ charId)
	double PvpSentAtS = 0.0;           // момент отправки 4201 (FPlatformTime) — страж ответа 4202
	// Второй клик по выбранному персонажу при включённом PvP-режиме: гейты эталона (input.c:1547–1680) → 4201.
	void EngagePlayerFromClick(AActor* Pl);
	// Боевой тик PvP: ожидание 4202, подход, «взвод» замаха в дистанции.
	void PvpAttackTick();
	// Точка отправки замаха PvP: дистанция (ближний бой), серверные часы → 4203.
	void PvpOnSendPoint();
	// Свой 4202 код 1 (зовёт мир): атака принята.
	void OnPvpAccepted(int32 TargetUserIdx);
	// Конец PvP-атаки: боевая стойка, своя PvP-очередь снята (эталонный покой подтипа 20).
	void StopPvpAttack(const TCHAR* Why);
	// Ключ отката: 0 = прежний ритм по ответу сервера.
	static bool RhythmFromAnimEnabled();
	// ★ 15.10.2026 (волна mobpos_swing): X/Z для 4026/4028 — позиция своего персонажа (эталон; ключ `asda.Combat.AttackPosSelf`).
	static FVector2D AttackPacketPos(const FVector2D& SelfGame, const FVector2D& TargetGame);
	// ★ 15.10.2026 (волна mobpos_swing): удержание боя не шире допуска NpcServer к 4028 (ключ `asda.Combat.RangeOutServerCap`).
	static bool RangeOutServerCapEnabled();
	// Последняя посчитанная в боевом тике дальность удержания цели. Ритм от анимации берёт её
	//   готовой, а не пересчитывает: расчёт длинный (дальность оружия + радиус цели + стоп-точка),
	//   и вторая его копия неминуемо разошлась бы с первой.
	float  LastCombatRangeOut = 0.f;
	int32  LastCombatRangeRid = -1;   // для какой цели она посчитана

	// ── ★ ПОДБОР ЛУТА (порт netHandleTargetObjectClick@0x9968F0, loot_system_re.md §5): клик по луту —
	//   одна ячейка (тайл 100) с предметом → 5012 сразу; иначе 4006-подход к тайлу + ОТЛОЖЕННЫЙ подбор
	//   по прибытии (эталон state 103, player[+208472]=dropId); повторный клик по тому же дропу в пути —
	//   игнор. 5013 (любой result) и 5015 по этому тайлу сбрасывают состояние. ──
	void StartLootPickup(const FIntPoint& LootTile);
	void ClearPendingLoot() { bLootPending = false; }
	void OnLootGone(const FIntPoint& Tile) { if (bLootPending && PendingLootTile == Tile) { bLootPending = false; } }
	// ★ 09.10.2026 (автолопата): ОБЩАЯ ТОЧКА «поднять предмет рядом» — порт `itemPickupGroundItem@0xC54ED0`
	//   (item_inventory.c:22223). Зовут её эталонные места: цикл автолопаты (game_main.c:2279) и окно действий,
	//   «функции» → 1 (`UI_DispatchTargetContextMenu` 2/1, ui_core.c:32165). Без хода к предмету: первый лут в 300 юнитах
	//   (`sub_F2F110`) → 5012 {свой тайл, тайл предмета}; молча при сидении/вставании/смерти (`isMovementActionState` — миснейм:
	//   7/8/9/6) и при бите 1 слова `g_InventoryBag+0x4EF0` (в порту такого бита нет — у эталона писателей бита тоже нет, только
	//   обнуления в `uiItemSlotReset`/`cChar_initFields`). true — 5012 ушёл. `Why` — для журнала.
	bool PickupGroundItemEtalon(const TCHAR* Why);

	// ── ★ РАЗГОВОР С NPC (порт `Reports/npc_interaction_re.md` §2, этап Э1) ───────────────────
	// ЭТАЛОН. Ветка `g_nInputControlState == 3` в `Input_WorldClickHandler@0x968890` (одиночный ЛКМ)
	//   и в `world_OnLeftClickPick@0x966980` (двойной) ПОБАЙТОВО одинакова, поэтому разбор один:
	//     localPlayer[1].id5C = npc[115];                        // ОТЛОЖЕННАЯ ЦЕЛЬ = userIdx NPC
	//     if (движемся) Net_SendActionPacket(...);               // 4008
	//     if (bboxContainsPoint(npc + 183, x, y, z)) { …открыть окно… }
	//     else CGame_HandleGroundClickAttack(bag, npc[84], npc[85], npc[86], 1);  // обычный 4006
	//   Срабатывание по прибытии — в тике локального игрока (`Player_ExecuteSkillOnTarget@0x9F16D0`,
	//   entity_char.c:18298/18639/18903): та же проверка AABB, затем 4008 и открытие окна.
	//
	// ★★★ ОТСТУПЛЕНИЕ ПОРТА (решение владельца 2026-08-21): у эталона окно открывается ЛЮБЫМ кликом,
	//   у нас — только ДВОЙНЫМ; одиночный лишь выделяет цель. Разбор клика при этом по-прежнему ОДИН
	//   (`OnLeftClick(bool bDouble)`), различается только признак. Правило — в
	//   `ShouldOpenNpcTalkOnClick`, откат — `asda.Npc.OpenOnSingleClick 1`.
	//   Разбор: `Reports/npc_target_ring.md` §3.
	//
	// ★ УСЛОВИЕ «ДОШЁЛ» — НЕ РАДИУС, А AABB СУЩНОСТИ (`worldEntityInitByType@0xF98B30`):
	//   полу-размер (50, H/2, 50), центр (posX, posY + H/2, posZ), где H = `npc_list.BIN` +0x68
	//   (heightParam; −1 ⇒ 150). То есть по горизонтали это КВАДРАТ ±50 юнитов вокруг NPC.

	// ★★★ ЕДИНАЯ ТОЧКА ПРАВИЛА «когда клик по NPC открывает окно».
	//   Отдельный предикат нужен не «для красоты»: правило — ОТСТУПЛЕНИЕ от эталона, и живая
	//   проверка (`asda.Npc.ClickRule`) обязана спрашивать РОВНО ТО ЖЕ условие, что и ветка клика,
	//   иначе проверялась бы вторая копия правила и разъезд остался бы незамеченным.
	//   `bDouble` — пришёл ли клик по `IE_DoubleClick`.
	bool ShouldOpenNpcTalkOnClick(bool bDouble) const;

	// Начать разговор: в зоне — открыть окно сразу, иначе подбежать и открыть по прибытии.
	void StartNpcTalk(class AAsdaNpcActor* Npc);
	// Снять отложенное «подбежал → заговорил» (★ исправление D5, см. .cpp).
	void ClearPendingNpcTalk(const TCHAR* Why);
	// ★ ЕДИНАЯ ТОЧКА «игрок начал двигаться»: сюда сходятся ВСЕ источники движения (клик по земле,
	//   мини-карта, WASD, подход к цели/луту/навыку) — она снимает отложенный разговор и, если
	//   окно 570 открыто, закрывает его вместе с выделением цели. Разбор — в .cpp.
	void NotifyPlayerStartedMoving(const TCHAR* Why);
	// Игрок внутри зоны разговора этого NPC? (порт `bboxContainsPoint(npc + 183, …)`).
	bool IsInsideNpcTalkZone(const class AAsdaNpcActor* Npc) const;
	// ★ Та же проверка для ПРОИЗВОЛЬНОЙ точки. Отдельный вход нужен живой проверке геометрии зоны:
	//   игровой павн в порту появляется только по серверному 4003, а зону надо уметь проверить и
	//   без сервера — при этом ВТОРОЙ реализации условия не заводим, обе идут сюда.
	bool IsPointInNpcTalkZone(const class AAsdaNpcActor* Npc, const FVector& UePoint) const;
	// Состояние отложенной цели (для живых проверок и дампов: `asda.Npc.TalkZone`).
	bool IsNpcTalkPending() const { return bNpcTalkPending; }
	int32 GetPendingNpcRenderId() const { return PendingNpcRenderId; }
	class AAsdaNpcActor* GetPendingNpcActor() const { return PendingNpcActor.Get(); }

	// ★★ «МЫ МЁРТВЫ» — ЕДИНЫЙ ГЕЙТ ВСЕГО МИРОВОГО ВВОДА (порт isMovementActionState@0x95DBE0).
	//   Смотрит НА ДВА источника: состояние павна (клип/поза смерти) И сетевое зеркало серверного
	//   CUser+79 (`UAsdaNetSubsystem::bSelfDead`). Второй источник обязателен: при входе в мир уже
	//   мёртвым и при рассинхроне по 6001 состояние смерти известно РАНЬШЕ, чем появляется павн, —
	//   без него клиент успевал локально предсказать движение и отправить 4006, который сервер режет.
	bool IsSelfDead() const;

private:
	void AttackTick();
	class AAsdaPlayerCharacter* GetChar() const;

	// ★★★ РАЗБОР ЛКМ ОДИН НА ОБА СОБЫТИЯ, но с ПРИЗНАКОМ «это был двойной».
	//   ЭТАЛОН держит две точки входа (`Input_WorldClickHandler@0x968890` для WM_LBUTTONDOWN и
	//   `world_OnLeftClickPick@0x966980` для WM_LBUTTONDBLCLK), и ветка NPC в них ПОБАЙТОВО
	//   одинакова — то есть у оригинала окно разговора открывается уже с ПЕРВОГО клика.
	//   ★ ОТСТУПЛЕНИЕ (решение владельца 2026-08-21): у нас одиночный клик по NPC ТОЛЬКО выделяет
	//   цель, а окно разговора и подбегание запускает ДВОЙНОЙ. Возврат к эталону —
	//   `asda.Npc.OpenOnSingleClick 1`. Разбор — `Reports/npc_target_ring.md` §3.
	//   Поведение мобов, игроков, лута и клика по земле НЕ зависит от `bDouble` — там признак
	//   только прокидывается, чтобы ветки не разъехались по копиям обработчика.
	void OnLeftClick(bool bDouble);
	// Биндер IE_Pressed. ★ Ревизия В4 ВАЖН-1 (рыбалка, 09.10.2026): помечает кадр, в котором ЭТО нажатие само отправило 4006
	//   (`SinglePressMoveFrame`) — см. гейт бита 2 в `OnLeftClick`.
	void OnLeftClickSingle();
	// ★ Биндер IE_Pressed ЛКМ (решение владельца 09.10.2026, п. 4): второе нажатие двойного клика (в этом кадре у ЛКМ есть
	//   событие IE_DoubleClick) одиночный разбор НЕ исполняет — как WM_LBUTTONDBLCLK эталона вместо WM_LBUTTONDOWN; всё делает
	//   `OnLeftClickDouble`. Откат — `asda.Input.DblClickLegacySingle 1`.
	void OnLeftPressedInput();
	// Это нажатие ЛКМ — второе нажатие двойного клика (в текущем кадре у ЛКМ есть IE_DoubleClick, не больше нажатий, чем двойных).
	bool IsLeftPressPartOfDouble() const;
	// Ход по клику в точку земли (хвост ветки «земля» `OnLeftClick`: троттл 150 мс, гейт активной клавиатуры, 1013,
	//   маршрут, метка). Отдельно — чтобы самотест хода клавишами проходил тот же путь без пикинга курсором.
	void GroundClickMove(const FVector& PointUe);
	void OnLeftClickDouble() { OnLeftClick(/*bDouble*/true);  }   // биндер IE_DoubleClick

	// ★★★ ЕДИНЫЕ ВОРОТА «курсор над интерфейсом» для ВСЕХ мировых реакций мыши.
	//   Эталон (game_main.c, оконная процедура) гасит мировую реакцию по хит-тесту UI у
	//   `WM_LBUTTONDOWN` (:5748), `WM_LBUTTONUP` (:5816) и `WM_MOUSEWHEEL` (:5874); `WM_RBUTTONDOWN/UP`
	//   он НЕ гасит. ★ ПКМ мы гасим ТОЖЕ — прямое требование юзера 2026-08-06 («клик не должен
	//   проходить сквозь любые окна вообще»), осознанное отступление от оригинала.
	bool IsCursorOverUi(FName& OutZone) const;

	void OnRightPressed();
	// ★ ПКМ-отпускание: снимает орбит И открывает контекстное меню по чужому игроку (окно
	//   TRANTOOL_WIN 2000) — эталонный `GameInput_DispatchEvent` case 6. Тело в .cpp.
	void OnRightReleased();
	void OnWheel(float AxisValue);

	// ★★ 10.10.2026 (modal_lock2, СРЕДН-1 ревизии modal_lock). ПОД МОДАЛКОЙ ВЬЮПОРТ ВВОД ИГНОРИРУЕТ: `RefreshModalInput` ставит
	//   `FInputModeUIOnly` → `UGameViewportClient::SetIgnoreInput(true)`, и ни ось колеса, ни `BindKey` ПКМ до контроллера не
	//   доходят. Эталон колесо под модалкой не запирает (game_main.c:5864: очередь окон + `Camera_MouseWheelZoom`, если курсор не
	//   над окном, без `get_o200`). Препроцессор Slate (`FAsdaModalMouseRelay`, .cpp) при `IsWorldInputCapturedByModal` пересылает
	//   колесо в `OnWheel` (тот же путь: окна по z через гейт модалки → «курсор над окном?» → зум), событие НЕ съедает. При ключе
	//   отката `asda.UI.ModalLockRightClick 0` он же пересылает нажатие/отпускание ПКМ (`OnRightPressed/Released`) и проводку
	//   мыши для вращения камеры — ПКМ миру как у оригинала (окнам под модалкой ПКМ не отдаётся ни при каком ключе, modal_lock3).
	//   ★ modal_lock3 (МЕЛК-1): проводка подаётся осями MouseX/MouseY в `UPlayerInput` (чувствительность, поправка на угол обзора и
	//   сглаживание — как без модалки); прежнее поле ручной суммы пикселей `ModalOrbitDelta` убрано.
	friend class FAsdaModalMouseRelay;
	TSharedPtr<class IInputProcessor> ModalMouseRelay;   // препроцессор (регистрируется в BeginPlay, снимается в EndPlay)

	// Исполнить команду по cmd (эталон hotkeyDispatchAction): движение/сесть/скриншот — сразу; прочее — делегат.
	void ExecuteCommand(int32 Cmd);
	// Отправка движения (4006) к цели / локальный старт в оффлайне.
public:
	// ★ Самотест пути игрока (волна В2 ремесла, 25.09.2026): прогнать команду клавиши ТЕМ ЖЕ диспетчером
	//   `ExecuteCommand`, что и опрос клавиш (прецедент — `UAsdaNpcDialogSubsystem::RunCommandForCheck`).
	void RunCommandForCheck(int32 Cmd) { ExecuteCommand(Cmd); }
	void RequestMoveTo(const FVector2D& DstGame);

	// ★ Пометить, что следующий приказ движения — это ШАГ ПУТЕШЕСТВИЯ к цели задания, а не воля
	//   игрока. Зовёт подсистема карты прямо перед `MoveToGamePoint`; без пометки такой приказ
	//   отменил бы путешествие сам на первом же шаге.
	void BeginTravelStep() { bTravelStepIssued = true; }
private:

	// Опрос клавиш каждый тик: движение по флагам (cmd 51-54), прочие команды по фронту ОТПУСКАНИЯ.
	void PollKeys();
	// ★ Волна В2: идёт рыбалка (`Input_IsTextInputFocused(g_pChatInputWnd)` эталона = fishState ≠ 0).
	//   ★ В3 S7 (N14): своей реализации нет — пересылка в одну точку `UAsdaFishingSubsystem::IsFishingNow`.
	bool IsFishingNow() const;
	// ★ Волна В2: Alt зажат — нужен, чтобы Alt-сочетания (WM_SYSKEYDOWN эталона) не засчитывались ответом мини-игры.
	//   ★ Ревизия В2 ВАЖН-1 (волна В4): ставится событиями Alt фильтра (плюс живой модификатор Slate в момент ответа),
	//   гасится `FlushPressedKeys` (потеря фокуса, Alt+Tab) — прежде застревал.
	bool bFishAltHeld = false;
	// ★ 22.10.2026 (ревизия WASD М-2, правка ревизии стана п. 7): одноразовый признак «следующее отпускание ПКМ — синтетическое».
	//   Ставит `FlushPressedKeys`, если ПКМ в этот момент зажата (сброс клавиш на переходе модалки / потере фокуса сам шлёт её
	//   отпускание); гасит первое же `OnRightReleased` — меню игрока под курсором оно не открывает. Ключ отката
	//   `asda.Input.FlushRmbGuard 0`.
	bool bNextRmbReleaseSynthetic = false;
	// ★ 22.10.2026: сколько отпусканий ПКМ от сброса клавиш погашено (журнал и самотест KB29).
	int32 RmbFlushReleaseSuppressed = 0;
	// ★ Волна В4 рыбалки: подмена экранной точки мирового клика (только самотест; пусто — мышь).
	TOptional<FVector2D> DebugClickScreen;
	// ★ Ревизия В4 ВАЖН-1 (рыбалка, 09.10.2026): кадр (`GFrameCounter`), в котором одиночный обработчик ЛКМ сам взвёл бит 2
	//   («запрос хода в пути») своим 4006. UE шлёт второе нажатие двойного клика как `IE_Pressed` + `IE_DoubleClick` в ОДНОМ
	//   кадре, а эталон — как WM_LBUTTONDBLCLK ВМЕСТО WM_LBUTTONDOWN (game.c:23838-23847): второго 4006 у оригинала нет, и бит 2
	//   для `world_OnLeftClickPick` отражает только 4006 первого нажатия. MAX_uint64 — не было.
	uint64 SinglePressMoveFrame = MAX_uint64;
	// ★ Ревизия В4 МЕЛК-6 (рыбалка, 09.10.2026): клик, сделанный во время рыбалки, ждёт её конца (ключ `asda.Fish.LegacyDeferredClick 1`
	//   = эталон: гейт `sub_92A968` не гасит `g_WorldInputFlags`, биты 1/2 исполняются в первом кадре после рыбалки, g:12566/13266).
	bool bFishDeferredSingle = false;
	// То же для двойного (бит 2 `g_WorldInputFlags`, WM_LBUTTONDBLCLK).
	bool bFishDeferredDouble = false;
public:
	// ★ Ревизия В4 ВАЖН-3 (рыбалка, 09.10.2026): отпускание Alt, перехваченное Slate-препроцессором рыбалки
	//   (`UAsdaFishZoneSubsystem::GuardKey`, фокус не у вьюпорта), гасит признак Alt фильтра мини-игры — иначе он застревал.
	void OnFishGuardAltReleased() { bFishAltHeld = false; }
	// Признак Alt фильтра мини-игры (самотест).
	bool IsFishAltHeldForCheck() const { return bFishAltHeld; }
private:
public:
	// ★ Клавиша действия `Cmd` (в т.ч. НАШИХ расширений 86+) сейчас нажата? Читает раскладку из
	//   `system/Shortcutkey.ini` — значит владелец переназначает её БЕЗ ПЕРЕСБОРКИ. Модификатор
	//   бинда проверяется живым состоянием Shift/Ctrl/Alt (эталонный `Keybind_ResolveModifier`).
	//   Нужен подсистемам, у которых клавиши раньше были зашиты в код (свободная камера, dev-меню,
	//   транспорт-пасхалка): своей копии опроса они больше не держат.
	bool IsCmdKeyDown(int32 Cmd) const;
private:
	// Клавиша бинда нажата? Учитывает нумпад-двойник цифр (эталон Keybind_NormalizeVK 96..105→48..57)
	//   и платформенный опрос PrintScreen (VK 44), которого в EKeys у UE нет вовсе.
	bool IsBindKeyDown(int32 Vk) const;
	float Net_LastSpeed() const;   // последняя скорость от сервера (LastServerSpeed) для локального движения

	bool bRMBDown = false;
	// ★ Сколько курсор проехал за текущий зажим ПКМ (px). Нужен ТОЛЬКО правке дефекта
	//   «меню по игроку не выскакивает после разворота камеры» (asda.PlayerMenu.OpenAfterDrag).
	float RMBDragPixels = 0.f;
	double LastMoveSendTime = 0.0;    // время последней отправки 4006 (динамический троттл эталона)
	float CamYawDeg = 0.f;            // орбит-углы (контроллер рулит SpringArm'ом павна)
	float CamPitchDeg = -20.f;

	// Флаги движения (эталон g_bMoveFwd/Back/Left/Right) + предыдущее down-состояние клавиш (фронт отпускания).
	bool bMoveFwd = false, bMoveBack = false, bMoveLeft = false, bMoveRight = false;
	TMap<int32, bool> PrevKeyDown;    // VK → было нажато в прошлом тике (для edge-детекта отпускания)
	bool bWasMoving = false;          // для отправки stop 4008 при полном отпускании

	// ── ★ Клавиатурное движение по эталону (Player_KeyboardMoveTick, math_geom.c:380) ──
	// Скорость плавного разворота A/D: 0.05 рад на кадр-базу flt_15B5B18=31.25мс = 1.6 рад/с.
	static constexpr float KeyTurnDegPerSec = 91.673f;
	// Латч конфликта W+S (эталон g_nMoveFwdBackPriority): 0=нет, 1=W активен, 2=S активен; последняя побеждает.
	int32 MoveFBPriority = 0;
	// Прошлое состояние W/S (после разрешения конфликта) — детект свежего нажатия для мгновенной отправки.
	bool bPrevFwdP = false, bPrevBackP = false;
	FVector2D LastSentMoveDir = FVector2D::ZeroVector;   // последнее отправленное направление (троттл 250/1000мс)
	// ★ МЁРТВАЯ ЗОНА поворота (эталон charMoveToWorldPos@0xA1E3D0): локальное ПЕРЕнацеливание курса —
	//   только когда целевая точка (pos+dir×800) пересекла границу ЯЧЕЙКИ террейна (тайл 100 юнитов).
	//   Малые повороты камеры (<~7° на радиусе 800) игнорируются — персонаж продолжает прежним курсом,
	//   разворот идёт плавной дугой шажками при смене ячейки. Сентинел MAX = «нет закоммиченной цели».
	FIntPoint KeyMoveCommittedCell = FIntPoint(MAX_int32, MAX_int32);
	bool bPrevBackpedal = false;      // смена бег↔пятки перенацеливает сразу (минуя мёртвую зону)

	// ★ Автобег (эталон g_bFxAlwaysRun + удержание W 3000мс → g_bAutoRunActive; S/ЛКМ отменяет).
	//   Опция «всегда бег»; по умолчанию выключена, как в оригинале. ★ 17.09.2026: «будущее
	//   options-JUI» пришло — это чекбокс **70372** окна `SYSTEM_Option_NEW` (loc 30476 «Всегда
	//   бежать», буфер `COptionsWnd + 1120`), а владелец значения — `UAsdaSystemOptionSubsystem`.
	//   Поле остаётся приватным: снаружи его читают и пишут через пару ниже.
	UPROPERTY(EditAnywhere, Category="Asda|Input")
	bool bOptionAlwaysRun = false;

public:
	// Опция «всегда бежать» (эталон `g_bFxAlwaysRun`) — единственная точка доступа для окна настроек.
	bool IsOptionAlwaysRun() const { return bOptionAlwaysRun; }
	void SetOptionAlwaysRun(bool bOn) { bOptionAlwaysRun = bOn; }

private:
	double AutoRunHoldStart = 0.0;    // старт удержания W (0 = не держим)
	bool bAutoRunActive = false;

	// ★★★ ЛАТЧ ГЕЙТА СЦЕНЫ ПРЕМИУМ-ГАЧИ (`sub_922D5D` → `sub_B6F370@0xB6F370`). Нужен только для
	//   ФРОНТА «сцена открылась»: эталон в `premiumGacha_BeginRoll@0xB6DF80` (`world_map.c:4707`)
	//   первым делом зовёт `followTargetCancelReset(&g_bMapTransferActive)` — снимает автобег/маршрут,
	//   и лишь потом ставит `gap_10[629] = 1`. Пока сцена держится открытой, движение не обрабатывается
	//   (`math_geom.c:417`), поэтому маршрут надо погасить РОВНО ОДИН РАЗ, на переходе false→true.
	bool bGachaGateLatched = false;

	// ★ Отложенный подбор лута (эталон state 103): идём к тайлу предмета, по прибытии шлём 5012.
	bool bLootPending = false;
	FIntPoint PendingLootTile = FIntPoint(0, 0);

	// ★ Отложенный разговор с NPC (эталон `localPlayer[1].id5C`, он же `pChar + 52119`): в оригинале
	//   там лежит userIdx NPC, у нас — renderId сетевой сущности (`UAsdaWorldSubsystem::NetNpcs`),
	//   это ОДИН И ТОТ ЖЕ ключ реестра, а не charId (закон `charid-vs-usernum`). Слабая ссылка на
	//   актора нужна затем, чтобы деспавн/уничтожение NPC гасили намерение сами собой.
	bool bNpcTalkPending = false;
	int32 PendingNpcRenderId = -1;
	TWeakObjectPtr<class AAsdaNpcActor> PendingNpcActor;
	// Тик прибытия: проверка зоны + открытие окна (аналог ветки `bLootPending` в `PlayerTick`).
	void NpcTalkTick();

	// ★ ЕДИНАЯ точка «мы у NPC — что дальше»: развилка эталона «карта осады → 6723» / «обычное окно».
	//   Зовётся из `StartNpcTalk` (кликнули стоя в зоне) и из `NpcTalkTick` (подбежали).
	void OpenNpcTalkOrCampWar(class AAsdaNpcActor* Target);

	// ★ Номер карты осады (CampWar). Эталон сравнивает `getField3036(&g_pLocalPlayer) == 19`
	//   в трёх точках клика по NPC (`input.c:331`, `ui_core.c:1119`, `entity_char.c:18937`).
	static constexpr int32 kCampWarMapId = 19;
	// Текущая карта: единственный держатель числа в порту — `UAsdaLoadManagerSubsystem::CurrentMapId`.
	int32 GetCurrentMapIdForCampWar() const;

public:
	// ── ★ МАРШРУТ С ОБХОДОМ ПРЕПЯТСТВИЙ (порт mapClick_BeginPathMove@0xFBC9A0 + pathFindAStar@0xFBC3B0
	//   + Pathfind_BuildCheckpointRoute@0xFBCE50 + потребитель маршрута game_main.c:1592-1644) ──────
	// Оригинал: список чек-пойнтов (self+43 = dword_351F9AC) потребляется тиком игры — берётся ПЕРЕДНЯЯ
	//   точка, к ней шлётся ход; по достижении её ТАЙЛА делается pop_front и ход шлётся к следующей.
	//   То есть на маршрут уходит СЕРИЯ ходов (по одному на узел), а не один ход к финальной точке.
	//   Плюс раз в ~1 с оригинал проверяет LOS до текущего узла и при перекрытии ПЕРЕСТРАИВАЕТ маршрут.
	// Здесь то же самое: Route — узлы в game XZ, RouteIdx — текущий, RouteRenderId — renderId цели
	//   (для боевого подхода: эталонный Combat_SendNormalAttack кладёт его в ход).

	// Пойти к точке С ОБХОДОМ препятствий. Прямая видимость → один прямой отрезок (как раньше).
	//   `bApproach` — подход к цели (боевой `AttackTick`, подход ради навыка): при эталонной схеме отрезок внутри
	//   своей клетки 100×100 идёт без 4006 (см. `bRouteApproach`).
	void StartRoutedMove(const FVector2D& DstGame, int32 TargetRenderId = -1, bool bApproach = false);
	// Сбросить маршрут (клик по другой точке, ручное движение, стоп, смерть).
	void CancelRoute() { Route.Reset(); RouteIdx = 0; RouteRenderId = -1; bRouteLegUnsent = false; bRouteApproach = false; }

	// ★★★ 27.09.2026 — КЛЮЧ ЭТАЛОННОЙ СХЕМЫ СВОЕГО ХОДА `asda.Move.EtalonSpeedSync` (1 = эталон, 0 = прежний порт).
	//   Эталон (`CGame_HandleGroundClickAttack@0x996480` → `Combat_SendNormalAttack@0x9F8CA0` → ждать 4007 →
	//   `Recv_EntityMove@0xF3C2C0` case 1 → `charMoveToWorldPos@0xA1E3D0`): по клику/узлу маршрута уходит 4006, но
	//   персонаж НЕ трогается, пока не придёт грант; скорость шага подгоняется под серверное время
	//   (`расстояние(актор, dst) / (расстояние(cur, dst)/speed − |сейчас − метка 4006|/2)`); время ≤ 0 → 4008 в dst,
	//   позиция = dst, покой. Пока запрос в пути (бит 2) — мировой клик, навыки, перезапрос подхода и финальный
	//   стоп ждут. Полный разбор — `Reports/owner_0927/move_4007.md`.
	//   ★ Клавиатура (WASD): старт и скорость шага ключ НЕ меняет (грант во время WASD правит только скорость), НО
	//   каждый 4006 клавиш (сразу, раз в 250 мс при повороте, раз в 1 с на прямой) взводит бит 2, как у эталона
	//   (клавиши идут через тот же `Combat_SendNormalAttack`, game.c:8977). Поэтому при ключе 1 на время ответа
	//   сервера навыки, клики по миру и подход молча ждут и при беге на клавишах (ревизия С1, move_4007_REVIEW.md).
	//   ★ Подход к цели внутри своей клетки 100×100 — без 4006 (исправление К1, move_4007_fix.md).
	static bool IsEtalonSpeedSync();
	// ★ Ревизия В4 МЕЛК-6 (рыбалка): `asda.Fish.EatSysKeys` — съедать ли при рыбалке Alt-сочетания и F10 (1 — порт, 0 — как
	//   оригинал: WM_SYSKEYDOWN мимо гейта). Читает и Slate-препроцессор рыбалки (`UAsdaFishZoneSubsystem::GuardKey`).
	static bool IsFishEatSysKeys();
	// ★ В3 S7 (дедуп №2, N14; 12.10.2026): «системная» клавиша для гейта рыбалки — WM_SYSKEYDOWN эталона: сама Alt, F10 или
	//   любая клавиша при зажатом Alt (`bAltDown` — признак Alt вызывающего: флаг фильтра, модификатор Slate или события).
	//   Прежде одно и то же правило было собрано трижды (гайд мини-игры, гейт рыбалки контроллера, Slate-гейт окон).
	static bool IsFishSysKey(const FKey& K, bool bAltDown);
	// Пропустить её мимо гейта рыбалки: только при `asda.Fish.EatSysKeys 0` (как оригинал, g:24326); иначе её съедают.
	static bool ShouldPassFishSysKey(const FKey& K, bool bAltDown) { return !IsFishEatSysKeys() && IsFishSysKey(K, bAltDown); }
	// ★ 27.09.2026: свой 4007 с отказом (result не 1 и не 5) при эталонной схеме. Эталон при отказе персонажа не
	//   двигает; у нас маршрут ждал гранта и без этого сам перезапрашивал бы ход раз в секунду — поэтому маршрут
	//   закрывается штатной остановкой (4008 уйдёт, только если есть незакрытый ход). [ПОРТ]
	void OnSelfMoveRefused(int32 Result);
	// ★★★ 16.09.2026 (4062, self-коррекция). Сервер перебивает ЦЕЛЬ ТЕКУЩЕГО ШАГА — маршрут при этом НЕ
	//   отменяется. Отмена рвёт остаток пути И глушит обязательный 4008 на финише (`StopMoveAndNotify` живёт
	//   в `RouteTick`, а тот выходит сразу при пустом маршруте) — сервер остаётся в состоянии «игрок идёт»,
	//   и любой навык отбивается кодом 24 «Сначала нужно остановиться» (лечили это 10.08, см. :1525-1533).
	//   Поэтому правим узел на месте: продвижение по узлам, `RouteRenderId` и финальный 4008 сохраняются.
	//   ★ Возвращает true, только если узел ДЕЙСТВИТЕЛЬНО переназначен. Вызывающий обязан гейтить движение
	//   этим признаком: без маршрута коррекции нет, а ход «в точку» без маршрута не закроется финальным 4008
	//   (`StopMoveAndNotify` живёт в `RouteTick`, который при пустом маршруте выходит сразу).
	bool RetargetCurrentNode(const FVector2D& NewDstGame)
	{
		if (!HasRoute()) { return false; }
		Route[RouteIdx] = NewDstGame;
		if (RouteIdx == Route.Num() - 1) { RouteFinalDst = NewDstGame; }
		// ★ Придержать перепланировщик: серверная цель может не пройти клиентскую проверку проходимости
		//   (расхождение сеток — и есть повод для 4062), и `StartRoutedMove` в течение секунды выбросил бы
		//   коррекцию, построив путь заново от своей конечной точки.
		RouteReplanTime = FPlatformTime::Seconds() + 1.0;
		return true;
	}

	// ★ Полная остановка движения на время смены карты/канала. Маршрут сбрасывается, намерения
	//   («подбежать и заговорить», подход ради навыка) снимаются, пакет остановки НЕ шлётся —
	//   игрок на новом соединении ещё не опознан, и любой игровой пакет там рвёт связь.
	void StopMovementForMapChange();
	bool HasRoute() const { return Route.Num() > 0 && RouteIdx < Route.Num(); }

	// ★★★ ПОЛНАЯ ОСТАНОВКА (эталон: отпускание клавиш движения / вход в бой / каст навыка →
	//   `Net_SendActionPacket@0x9F9C00` = 4008). Гасит локальное предсказание, сбрасывает маршрут и
	//   ОБЯЗАТЕЛЬНО шлёт 4008 — иначе серверный флаг движения `CUser+51` остаётся взведённым и
	//   `RecvChar_SkillUse` отбивает все навыки кодом 24 («Сначала нужно остановиться»).
	void StopMoveAndNotify(const TCHAR* Why);
	// Двигаемся ли мы прямо сейчас (маршрут / клавиши / локальная интерполяция павна).
	bool IsMovingNow() const;

	// ★★★ 27.09.2026 — «СЕСТЬ/ВСТАТЬ» 1:1 с эталоном `charToggleSitStand@0xC54A70` (entity_char.c:45185-45279;
	//   отчёт `Reports/owner_0927/pkt4024_facing.md` §6). ЕДИНАЯ точка для всех вызывающих эталона:
	//   кнопка «функции → сесть/встать» и клавиша 55 (`UI_DispatchTargetContextMenu(2, 0)`, ui_core.c:32162,
	//   input.c:4352), клавиши хода при состояниях 6…9 (math_geom.c:624), клик по миру при 6…9 (input.c:173).
	//   Порядок эталона: [флаги `+47552 == 1` → выход — в порту флага нет, реестр «Биты занятости»]; мёртв (6) →
	//   выход; идёт (`sub_9664C0`: состояние == 1) → 4008 «стоп»; сидит (8) → встать (9) + 4024 код 109;
	//   иначе, если НЕ 7…9 (`charActionStateIs7to9`): «занят» (`objStateAllowsRender`) → молча выход БЕЗ иконок,
	//   иначе сесть (7) + 4024 код 107; в конце — иконка окна действий и хотбара. Нотисов у функции НЕТ.
	//   Why — для лога. Ключ отката `asda.Char.SitToggleEtalon 0` (прежний порт) — у вызывающих.
	void ToggleSitStand(const TCHAR* Why);
	// ★★★ 29.09.2026 (волна Л1 торговли) — ПОЗА ЛИЧНОЙ ЛАВКИ 1:1 с `CharAction_SendStatePose@0xDD71C0` (entity_char.c:48616):
	//   зовут открытие витрины хозяина (`shopDlgOpenSellWindow` → 107) и её скрытие (`shopDlgShow` → 109). Отличие от
	//   `ToggleSitStand`: поза ЗАДАНА (не тумблер), 109 уходит без проверки «сидит», у 107 нет проверки «садится/встаёт».
	//   Порядок эталона: [флаги `+47552 == 1` → выход — в порту флага нет, как у `ToggleSitStand`]; мёртв (6) → выход;
	//   идёт → 4008 «стоп»; 109 → встать (9) + 4024 код 109; 107 → «занят» (`objStateAllowsRender`) ⇒ молча выход БЕЗ
	//   иконок, иначе сесть (7) + 4024 код 107; в конце — `CHotbar_UpdateAnimSlots` (иконки «сесть/встать»).
	//   Тело позы (анимация + 4024 + иконки) — общее с `ToggleSitStand` (`ApplySitPose4024`/`RefreshSitStandIcons`).
	//   Возврат: поза отправлена. `PoseCode` — 107 или 109 (иное — отказ с логом).
	bool SendStallStatePose(int32 PoseCode, const TCHAR* Why);
	// Ключ `asda.Char.SitToggleEtalon`: 1 — «сесть/встать» по эталону (`ToggleSitStand`), 0 — прежний порт.
	static bool IsSitToggleEtalon();
	// Отменить отложенное «подбежать и применить навык» (новый приказ движения, смерть, смена цели).
	// Отменить путешествие, если движение начал сам игрок (клик по земле, по карте, WASD).
	void CancelTravelIfManual(const TCHAR* Why);
	bool bTravelStepIssued = false;   // взводится BeginTravelStep на один приказ движения

	void CancelPendingSkillApproach(const TCHAR* Why);

	// ★ «Узел маршрута достигнут» (< половины тайла). ПУБЛИЧНАЯ намеренно: от неё зависит инвариант
	//   зоны разговора с NPC («зона обязана быть больше порога прибытия»), который проверяется
	//   `static_assert`-ом в `AsdaPlayerController.cpp`. Пока константа была приватной, инвариант
	//   держался «на честном слове» — и был нарушен (окно 570 не открывалось, живой лог 2026-08-20).
	static constexpr float kRouteArriveDist = 45.f;

private:
	// ★ Общее тело позы «сесть/встать» (`CharFx_AutoSabzil_State(7|9)` + `Send_CharStatePos(…, 107|109)`) — одно на
	//   `ToggleSitStand` и `SendStallStatePose` (волна Л1 торговли: без второй копии). bSit — сесть (7, код 107) / встать (9, 109).
	void ApplySitPose4024(bool bSit, const TCHAR* Why);
	// ★ Иконки «сесть/встать» по текущему состоянию: хотбар (`CHotbar_UpdateAnimSlots`) всегда, окно действий (`UI_SetSitStandButtonIcon`) — при bCharaInfoIcon.
	void RefreshSitStandIcons(bool bCharaInfoIcon);
	void RouteTick();                       // продвижение по узлам + пересчёт при перекрытии
	// Отправить ход к узлу маршрута (с античит-гардом «без реального перемещения не слать»).
	void SendRouteLeg(const FVector2D& Cur, const FVector2D& Wp);

	TArray<FVector2D> Route;                // узлы маршрута (game XZ), последний = точка назначения
	int32 RouteIdx = 0;                     // текущий узел
	int32 RouteRenderId = -1;               // renderId цели для боевого подхода (иначе −1)
	FVector2D RouteFinalDst = FVector2D::ZeroVector;   // конечная точка (для пересчёта)
	double RouteReplanTime = 0.0;           // троттл проверки LOS/пересчёта (эталон 1000 мс)
	// ★ 27.09.2026: отрезок маршрута ещё НЕ отправлен — в момент, когда он был нужен, предыдущий 4006 ждал ответа
	//   (бит 2). Эталон такой клик отбрасывал (`CGame_HandleGroundClickAttack` → 0), и маршрут стоял до пересчёта
	//   через секунду; у нас отрезок откладывается и уходит, как только ответ пришёл. [ДЕФЕКТ ОРИГИНАЛА ИСПРАВЛЕН]
	bool bRouteLegUnsent = false;
	// ★★★ 27.09.2026 (исправление К1 ревизии хода 4007) — маршрут является ПОДХОДОМ К ЦЕЛИ (боевой `AttackTick`,
	//   подход ради навыка). При эталонной схеме отрезок такого маршрута, чья точка лежит в той же клетке 100×100,
	//   что и персонаж, уходит БЕЗ 4006 и исполняется локально. [ОРИГИНАЛ] `Player_ExecuteSkillOnTarget@0x9F16D0`
	//   шлёт подход-4006 только при `terrainCellIndexFromXZChecked(цель) != self+0x2E878` (game.c:68302…69064, девять
	//   мест), а сервер `CharMove@0x544EB0` отвечает на ход в свою клетку result 0 (GameServer.c:124534). [ПОРТ]
	//   Разбор и отличия — `Reports/owner_0927/move_4007_fix.md`. Сохраняется при пересчёте маршрута.
	bool bRouteApproach = false;
	static constexpr float kMoveMinSendDist = 25.f;    // античит: ход без перемещения не шлём

public:
	// ══ ★★★ 27.09.2026 — ХОД КЛАВИШАМИ ПО СХЕМЕ ОРИГИНАЛА (решение владельца; `Reports/owner_0927/wasd_impl.md`) ══
	//   Ключ `asda.Move.KeyboardEtalon`: 1 (умолчание) — дословный порт `updatePlayerMovement@0x95CDF0`
	//   (локального хода нет: клавиша шлёт 4006 на точку pos + dir·800 через тот же вход, что клик,
	//   `CGame_HandleGroundClickAttack@0x996480`, персонаж идёт только по гранту 4007; повтор 10/250/1000 мс;
	//   отпускание — покой + 4006 cur→cur + 4008); 0 — прежняя схема порта ДОСЛОВНО (мёртвая зона, мгновенный старт).
	//   Тело эталонной ветки — `AsdaPlayerKeyboardEtalon.cpp`.
	static bool IsKeyboardEtalon();
	// Клавиатура активна по эталону (`g_bWorldTickActive == 0`: клавиша движения держится, разрешение ввода есть, ход
	//   разрешён) — грант 4007 в этот момент принадлежит клавишам. При ключе 0 всегда false.
	//   ★ 22.10.2026 [ИСПРАВЛЕНИЕ] (`Reports/wasd_2026-10-22/REPORT.md`): пока клавиатура активна, но намерения хода нет
	//   (`bKbIntentStopped`: W/S отпущены при зажатой A/D без ПКМ, A+D, W+S), грант клавиш НЕ ждут — опоздавший грант
	//   прежнего направления не уводит остановленного персонажа.
	bool IsKeyboardEtalonMoveActive() const { return IsKeyboardEtalon() && !bKbWorldTickActive && !bKbIntentStopped; }

	// ══ ★★ 22.10.2026 — ЖЁСТКАЯ ОСТАНОВКА ПОД «СТАНОМ» (слово владельца; ключ `asda.Move.StunHardStop`, разбор у `StunHardStopTick`) ══
	// Свой персонаж под обездвиживающим состоянием из набора остановки сервера (лёд 0, камень 1, паутина 2, оглушение 3, сон 8,
	//   тайная паутина 9, самозаморозка 11, оглушение/заморозка хранителя 12/13) или абсолютного оглушения 14 — и ключ включён.
	//   Страх 4 сюда НЕ входит: при нём ход ведёт сервер (грант «Паника»). Пока true — хода нет: ни клавиш, ни клика, ни отрезков маршрута,
	//   грант 4007 не исполняется, автопуть ждёт.
	bool IsStunHardStopped() const;
	// Ввод хода запрещён состоянием (для клика по земле): `IsStunHardStopped()` или эталонный набор `listHasAnyOfSlotTypes@0xA38970`
	//   (0,1,2,3,14,9,4,8 — у эталона клик даёт плашку 1019, input.c:545). При ключе 0 — false (прежнее).
	bool IsMoveInputLockedByState() const;

	// ══ ★★ 22.10.2026 — ЛОКАЛЬНЫЙ СТАРТ (решение владельца «Делать локальный старт»; ключ `asda.Move.LocalStart`, разбор —
	//   у `LocalStartAfter4006`, AsdaPlayerKeyboardEtalon.cpp) ══
	static bool IsLocalStart();
	// Ушёл 4006 клавиш/клика (Cur → Dst): персонаж трогается сразу со скоростью сервера, не дожидаясь гранта 4007.
	void LocalStartAfter4006(class AAsdaPlayerCharacter* Ch, const FVector2D& Cur, const FVector2D& Dst, const TCHAR* Src);
	// Свой 4007 пришёл: result 1/5 — локальный старт принят сервером/поправлен снапом (признак снимается).
	void OnLocalStartAnswered(int32 Result);
	// Свой 4007 — отказ (не 1 и не 5): если ход был начат локально и 4008 с тех пор не уходил — плавный возврат в точку
	//   отправки (там сервер), следующая попытка — без локального старта (до первого гранта). true — возврат сделан.
	bool OnLocalStartRefused(int32 Result);
	bool bLocalStartPending = false;        // ход начат локально, ответа 4007 на его 4006 ещё нет
	FVector2D LocalStartOrigin = FVector2D::ZeroVector;   // cur ушедшего 4006 (точка, где стоит сервер до ответа)
	bool bLocalStartSuppressed = false;     // после отказа — ждать гранта, как оригинал, до первого принятого хода
	// 23.10.2026: на время одного клика по земле, попавшего в зону рыбалки (тип 5 двойного клика), — без локального старта:
	//   иначе персонаж и камера трогаются до второго нажатия и повторный пик двойного клика зону уже не видит (как оригинал — ждём гранта).
	bool bSuppressLocalStartOnce = false;
	int32 LocalStartCount = 0;              // сколько раз персонаж тронулся локально (журнал, самотест)
	int32 LocalStartReturnCount = 0;        // сколько раз сделан возврат по отказу (журнал, самотест)
	// ★ Ревизия локального старта (22.10.2026):
	bool bLocalStartOnRun = false;          // ВАЖНО-3: в момент отправки шёл прежний грант (сервер идёт) — при отказе добегаем его
	FVector2D LocalPrevDst = FVector2D::ZeroVector;   // ВАЖНО-3: цель прежнего хода «в точку»
	float LocalPrevSpeed = 0.f;             // ВАЖНО-3: скорость прежнего хода
	double LocalStartAtS = 0.0;             // ВАЖНО-4: момент локального старта (потолок хода без гранта)
	bool bLocalStartCapped = false;         // ВАЖНО-4: потолок достигнут — персонаж стоит до гранта
	bool bLastGrantWasLocal = false;        // мелочь: грант ответил на локальный старт — скорость не подгонять вниз
	int32 LocalStartCapCount = 0;           // сколько раз сработал потолок (самотест)
	// Ушёл 4006 без локального старта (подход боя, «в свою точку») — признак ожидания снимается (ВАЖНО-5).
	void NoteMove4006WithoutLocalStart() { bLocalStartPending = false; bLocalStartCapped = false; }
	// Покадрово: потолок локального хода без гранта (ВАЖНО-4). Зовёт `PlayerTick`.
	void LocalStartTick(class AAsdaPlayerCharacter* Ch);
	// 4009 своему (`OnNetEntityMove`): true — не снапать (ход начат локально из этой самой точки, 4009 — ответ на прежний 4008).
	bool ShouldSkipStopSnap(const FVector2D& StopPos) const;
	// Ключ `asda.Move.StunHardStop` включён (мелочь: гейты маршрута/карты шире эталона — только под ключом).
	static bool IsStunHardStopKey();

private:
	// Покадровый тик эталонной схемы (порт `updatePlayerMovement`). Зовётся из `PlayerTick` вместо прежнего блока WASD.
	void KeyboardEtalonTick(float DeltaTime, class AAsdaPlayerCharacter* Ch);
	// Верхний гейт всей функции эталона (лавка, `get_o168`, сцена гачи, элитный почтовый ящик, поле игрока, склад) — true = тик
	//   не исполняется вовсе (ни хода, ни ветки отпускания; камера живёт, как у эталонного `cameraUpdate`).
	bool KbTopGateBlocked() const;
	// Гейт главного цикла (`game_main.c:1506`): состояние действия 5/24 или статус-оковы (типы 0,1,2,3,14,9,4,8) —
	//   функция не вызывается вовсе.
	bool KbMainLoopGateBlocked() const;
	// Порт `uiCanOpenWindowGate@0xECA560` (окна, при которых клавиатура «не нажата»); true = ввод разрешён.
	bool KbWindowGateAllows() const;
	// Порт `Party_CanInviteCheck@0x95C870` (миснейм: «можно ли идти»): отказ с нотисом в чат каждый кадр либо молча.
	bool KbCanMoveCheck();
	// Эталонное состояние действия своего персонажа (`+47551`), восстановленное по признакам порта (карта — в .cpp).
	int32 KbEtalonActionState() const;
	// Порт `CGame_HandleGroundClickAttack@0x996480` для клавиш (без эффекта клика): гейты бита 4/2, смерти, занятости,
	//   состояний 5/24/14/15/25, затем 4006 с +25 = цель атаки. true = 4006 ушёл.
	bool KbHandleGroundClick(const FVector2D& DstGame);
	// Ветка отпускания (`!g_bWorldTickActive`, game.c:8989-9013).
	void KbRelease(class AAsdaPlayerCharacter* Ch);
	// ★ 22.10.2026 [ИСПРАВЛЕНИЕ] Остановка «намерения хода нет» при активной клавиатуре (W/S отпущены, A/D держится без ПКМ;
	//   A+D; W+S): покой сразу + 4008 в своей точке, грант прежнего направления больше не ждут (`bKbIntentStopped`). У эталона
	//   такой ветки нет — персонаж добегал старый грант (до 800·cos(наклон) ед.), затем 4006 «в свою точку» откатывал его.
	//   ★ Ревизия В-1: NowMs — время кадра; 4006 «в свою точку» после стопа идёт с каденцией эталона 1000 мс от стопа.
	void KbIntentStop(class AAsdaPlayerCharacter* Ch, const TCHAR* Why, double NowMs);
	// ★ 22.10.2026: покадровая проверка «стана» (`asda.Move.StunHardStop`). true — персонаж обездвижен: тик хода в этом кадре
	//   не исполняется (вызывающий выходит, камера живёт). На фронте наложения — остановка на месте (см. определение).
	bool StunHardStopTick(class AAsdaPlayerCharacter* Ch);
	bool bStunHardStopped = false;          // ★ 22.10: прошлый кадр был под «станом» (фронт наложения/снятия)
	int32 StunHardStopCount = 0;            // ★ 22.10: сколько раз сработала остановка (журнал, самотест KB32)
	// «Сырая» кнопка мыши (эталон `GetAsyncKeyState(1/2)` — без оглядки на окна): 1 = ЛКМ, 2 = ПКМ.
	bool KbRawMouseDown(int32 Button) const;
	// Сброс всего состояния эталонной схемы. Зовут: смена ключа на лету и смерть. Смена карты/канала и выход в логин
	//   сбрасывают его БЕЗ вызова: они идут через `OpenLevel` (`AsdaLoadManagerSubsystem.cpp`), контроллер создаётся заново
	//   и все поля `Kb*` получают умолчания (`bKbWorldTickActive = true` — отпускание на новой карте не срабатывает).
	//   Разрыв без травела безвреден: все отправки схемы — под `bInGameConnected` (ревизия WASD, МЕЛК-4).
	void KbResetState();

	// ── Состояние эталонной схемы (имена эталонных глобалов — в комментариях) ──
	bool bKbWorldTickActive = true;         // g_bWorldTickActive: 1 = клавиатура НЕактивна (ветка отпускания уже отработала)
	bool bKbFlag[4] = { false, false, false, false };   // g_bMoveFwd/Back/Left/Right — ставятся нажатием, снимаются отпусканием и ЛКМ
	bool bKbPrevLevel[4] = { false, false, false, false };   // уровень клавиши прошлого кадра (фронт нажатия = WM_KEYDOWN)
	double KbKeyDownAtMs[4] = { 0.0, 0.0, 0.0, 0.0 };        // когда клавиша нажата (для автоповтора Windows после ЛКМ)
	int32 KbRepeatKey = -1;                 // клавиша автоповтора Windows: ТОЛЬКО последняя нажатая (−1 = нет; МЕЛК-1)
	bool bKbFwdLatch = false, bKbBackLatch = false;         // g_bMoveFwdLatch / g_bMoveBackLatch
	int32 KbTimeoutMs = 10;                 // g_nMsgBoxTimeout — интервал повтора текущего кадра
	double KbLastRepeatMs = 0.0;            // g_dwLastMoveRepeatTime
	FVector3f KbPrevDir = FVector3f::ZeroVector;   // g_vMovePrevTarget — прошлый вектор (сравнение float на точное неравенство)
	bool bKbPrevDirInit = false;            // g_dwTransitionFlags & 2 — вектор обнулён один раз
	FVector3f KbSentDir = FVector3f::ZeroVector;   // ★ 22.10 [ИСПРАВЛЕНИЕ]: вектор последнего УШЕДШЕГО 4006 клавиш (интервал 250 мс — пока новый не ушёл)
	bool bKbSentDirValid = false;           // ★ 22.10: KbSentDir заполнен (после сброса/отпускания — нет)
	bool bKbIntentStopped = false;          // ★ 22.10 [ИСПРАВЛЕНИЕ]: клавиатура активна, но намерения хода нет — персонаж остановлен, гранты не ждут
	int32 KbLastDir128 = 0;                 // LocalPlayer[1].buffFxSlots[128]: 1 = вперёд, 2 = назад (последнее направление)
	bool bKbAutoRunPrev = false;            // g_bAutoRunPrevState
	bool bKbAutoRunNotice = false;          // g_bAutoRunNoticeState
	double KbAutoRunHoldStartMs = 0.0;      // g_dwAutoRunHoldStartTime
	int32 KbSeenMoveEndSerial = 0;          // последний учтённый `GetPointMoveEndSerial` персонажа
	int32 KbDebugLmb = -1;                  // самотест `asda.Move.SelfTestKeyboard`: подмена «сырой» ЛКМ (−1 = нет, 0/1 = отпущена/нажата)
	int32 KbDebugRmb = -1;                  // самотест: подмена «сырой» ПКМ (−1 = нет подмены)
	bool bKbWasEtalon = false;              // ключ прошлого кадра (сброс состояния при смене)
	int32 KbRefuseShownLoc = 0;             // loc отказа, уже показанного за это удержание (`asda.Move.KeyboardNoticeOnce 1`)
};

UCLASS()
class ASDAREBORN_API AAsdaWorldGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AAsdaWorldGameMode();
};
