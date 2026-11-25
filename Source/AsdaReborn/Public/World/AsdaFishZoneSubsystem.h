// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaFishZoneSubsystem — ЗОНЫ РЫБАЛКИ В МИРЕ И ОГРАНИЧЕНИЯ ВВОДА (волна В4, 08.10.2026).
//   Реверс: `Reports/owner_1008/fishing_reverse.md` §6, `fishing_parts/world_anim.md` §1 и §5, `ui_book.md` §11.
//   Отчёт волны: `Reports/owner_1008/fishing_impl_W4.md`.
//
// ═════════════════════ ЧТО ЭТО В ЭТАЛОНЕ ═══════════════════════════════════════════════════════════
//   • `worldMapPlaceMarkersByType@0xFC3340` (g:603093; МИСНЕЙМ — ставит НЕ метки карты, а 3D-объекты зон) зовётся при
//     входе на карту (`MainLoop_Tick_SoundUpdate`, g:11540 и g:11958) с id карты своего персонажа:
//     `mapReleaseAllSceneObjects(g_pMap+0x140)` — старые зоны удаляются; для КАЖДОГО спота с `+0 == mapId` —
//     позиция (X·100, h, Y·100), h = высота ВОДЫ в точке (`sub_926A07(&g_pLocalPlayer, v)` → `pickSpriteQuadRayTest`;
//     нет воды над точкой — h остаётся 0), матрица = единичная + перенос → `mapInsertMakeObj0x214@0xFC5D40`.
//   • `cmapInitFishingZoneMesh@0xFC5A30` (g:604423): `+0x1CC` индекс спота; OBB пикинга `+0x1A8` = центр + ПОЛУРАЗМЕРЫ
//     (2·r·100, 100, 2·r·100) (`sub_B5B030`: `[0..2]` размеры, `[3..5]` центр; грани `центр ± размер` — `sub_9D9FF0`,
//     `aabbSizeZ@0x9DA020` = `[5] − [2]`). ★ Поправка к world_anim §1.3 («2r×1м×2r»): числа (2r·100, 100, 2r·100) —
//     ПОЛОВИНЫ сторон, т.е. коробка 4r × 2 м × 4r.
//   • `CMap_LoadFishingZoneMesh@0xFC5BE0`: меш `fs_zone.jbm` + клип `fs_zone.jbb`; `sceneList_UpdateAndRender@0xFC61E0` —
//     клип по кругу (петля), видна ВСЕМ и ВСЕГДА (не только рыбаку).
//   • Пикинг мышью — `pickClosestFromList@0xFC6360` (тип попадания 5, только двойной клик) — `PickZoneByRay`.
//   • Метки карты: `minimapDrawMarkerList@0xB56330` — квад 16×16 текстуры FishingArea на каждый спот показываемой
//     карты, ПОСЛЕДНИМ (после метки цели); миникарта обоих масштабов (режимы 2/3, `g_nCurMapId`) и большая карта
//     (режим 1, `g_nBigMapDisplayedMapId`) — заполнение общего списка `UAsdaMinimapSubsystem::SetMarks(Fishing, …)`.
//   • Пока идёт рыбалка (fishState ≠ 0), `MainWndProc` WM_KEYDOWN (g:24144) не отдаёт клавишу НИ ОДНОМУ окну — в т.ч.
//     полю ввода чата. Порт: нажатия/отпускания клавиш окон игрового UI глушит `FAsdaFishKeyGuard` (Slate-препроцессор,
//     см. .cpp); СИМВОЛЫ (`OnKeyChar`) препроцессор Slate не видит — их не пускает замок «только чтение» на поле ввода с
//     фокусом (однострочное `SEditableText` и многострочное `SMultiLineEditableText`, ревизия В4 МЕЛК-2). Иные виджеты,
//     принимающие символы, в игровом UI порта не используются.
//
// ═════════════════════ ПОРТ ═══════════════════════════════════════════════════════════════════════
//   Координаты: игра (x, y-высота, z) → UE (−z, x, y), масштаб 1:1 (`AsdaCoord::GameToUe3D`). Скелетная модель
//   `Models/Skeletal/Fishing/fs_zone` (В5а) ставится так же, как у мобов/питомцев: узел масштаба (0.01, −0.01, 0.01)
//   (glTF-метры + компенсация хиральности), курс актора +90° — тогда вершина модели (x, y, z) встаёт в
//   (−z, x, y) = `GameToUe3D` (единичная матрица эталона). Проверка — самотест `asda.Fish.ZoneSelfTest`.
//
// ═════════════════════ ★ РАЗБОР ЖАЛОБЫ 12.10.2026 «рыбалка предлагается по клику на СУШЕ у берега» ═══════════════════
//   Отчёт: `Reports/owner_1012/fishzone/fishzone_fix.md`. Это НЕ ошибка разметки порта — так устроен ОРИГИНАЛ:
//   • коробка пикинга зоны — 4r × 2 м × 4r (r = 10 тайлов на Альпии → ±20 тайлов от центра спота) на высоте ПЛОСКОСТИ воды
//     `.WAT` ±1 м (`cmapInitFishingZoneMesh@0xFC5A30`: байты 0xFC5A81 `fadd st,st; fmul [100.0]`, 0xFC5A8D `push 100.0f`;
//     `sub_9333FB` = thunk → `sub_B5B030` — центр + ПОЛУразмеры, грани `центр ± размер`); сами плоскости `.WAT` — прямоугольники
//     по 100 тайлов, лежащие и ПОД сушей («вода под текстурами» владельца — она там действительно есть, у эталона тоже);
//   • террейн в пикинге эталона НЕ участвует: пять списков `Picking_TestRayPickWorldTarget@0x992D60` — игроки (0), мобы (1),
//     предметы на земле (2), NPC (3), объекты карты `collide_rayVsMapObjects@0x1139A00` (4, OBB объектов `Pos.POS`), зоны (5);
//     луч `pick_BuildRaySegment@0x112D650` идёт сквозь землю, режим `mathRayPlaneIntersect` a4 = 1 (t ≥ 0, без верхней границы);
//   • дальше `inputHotkeyAction109` → FSM 109: только дистанция `|зона.xz − игрок.xz| < r·100` (10 тайлов) → диалог 81.
//   Итог эталона: двойной клик по берегу в ±20 тайлах от спота при игроке в 10 тайлах от него — «Хочешь здесь порыбачить?».
//   [ПОРТ] Отступление по выбору владельца — ключ `asda.Fish.ZonePickWaterOnly` (по умолчанию 1 — решение владельца 12.10.2026; 0 = эталон): при 1 тип 5
//   требует, чтобы точка клика лежала на воде (`IsWaterHit`: компонент `SM_water` либо точка не выше поверхности воды `.WAT`).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "GameFramework/Actor.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaFishZoneSubsystem.generated.h"

class USkeletalMeshComponent;
class USceneComponent;
class UAsdaFishingSubsystem;
class UPrimitiveComponent;
class SWidget;

// ═════════════════════ АКТОР ЗОНЫ (объект 0x214 эталона) ═════════════════════
UCLASS(NotPlaceable, Transient)
class ASDAREBORN_API AAsdaFishZoneActor : public AActor
{
	GENERATED_BODY()
public:
	// Компоненты (корень → узел масштаба/зеркала → скелетный меш стайки).
	AAsdaFishZoneActor();

	// Корень (позиция зоны, курс +90°).
	UPROPERTY(VisibleAnywhere, Category="Asda|Рыбалка") TObjectPtr<USceneComponent> Root;
	// Узел масштаба 0.01 и зеркала по Y (как `MeshRoot` питомца/моба — glTF-метры → см, хиральность).
	UPROPERTY(VisibleAnywhere, Category="Asda|Рыбалка") TObjectPtr<USceneComponent> MeshRoot;
	// Стайка рыб `fs_zone` (клип `fs_zoneanim_00`, петля).
	UPROPERTY(VisibleAnywhere, Category="Asda|Рыбалка") TObjectPtr<USkeletalMeshComponent> Mesh;

	// `+0x1CC` — индекс спота в общей таблице (= `FAsdaFishSpotRow::Order`).
	UPROPERTY(VisibleInstanceOnly, Category="Asda|Рыбалка") int32 SpotIdx = -1;
	// Центр OBB в ИГРОВЫХ координатах (X·100, h, Y·100) — `+0x1B4..+0x1BC` (перенос матрицы `+0x204/+0x208/+0x20C`).
	UPROPERTY(VisibleInstanceOnly, Category="Asda|Рыбалка") FVector CenterGame = FVector::ZeroVector;
	// Полуразмеры OBB в игровых координатах (2·r·100, 100, 2·r·100) — `+0x1A8..+0x1B0`.
	UPROPERTY(VisibleInstanceOnly, Category="Asda|Рыбалка") FVector HalfGame = FVector::ZeroVector;
	// Над точкой спота нашлась вода (иначе h = 0, как у эталона).
	UPROPERTY(VisibleInstanceOnly, Category="Asda|Рыбалка") bool bOverWater = false;
};

// ═════════════════════ ПОДСИСТЕМА ═════════════════════
UCLASS()
class ASDAREBORN_API UAsdaFishZoneSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
	friend struct FAsdaFishZoneSelfTest;   // самотест `asda.Fish.ZoneSelfTest` (пересоздание, внутренние поля)

public:
	// Подписка Slate-препроцессора клавиш (гейт рыбалки для окон).
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Снять препроцессор, удалить зоны.
	virtual void Deinitialize() override;
	// Каждый кадр: сверка «зоны созданы для текущей карты» (эталон — вызов при входе на карту), метки карты, замок
	//   поля ввода при рыбалке.
	virtual void Tick(float DeltaTime) override;
	// Стат тикера.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaFishZoneSubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }

	// ── Числа эталона ──
	static constexpr float kTileUnits = 100.f;      // `flt_1396D8C` = 100.0: тайл → единицы мира (X·100, Y·100, r·100)
	static constexpr float kObbHalfY = 100.f;       // полувысота OBB (`v21[14] = 0x42C80000` = 100.0, g:604437)
	static constexpr int32 kMarkSizePx = 16;        // метка FishingArea 16×16 (`minimapDrawMarkerList`, смещение −8)

	// `pickClosestFromList@0xFC6360` + `entityRayVsObbHit@0x993BE0` → `collide_rayVsObb6FacesHit@0x1199C20`:
	//   ближайшая по лучу зона ТЕКУЩЕЙ карты (равенство — за ранее созданной). Луч — в координатах UE (Dir — единичный).
	//   OutDist — дальность попадания вдоль луча. Возврат — индекс спота, −1 — промах.
	int32 PickZoneByRay(const FVector& UeOrigin, const FVector& UeDir, float& OutDist) const;
	// `collide_rayVsObb6FacesHit` 1:1: грани по порядку X−, X+, Y+, Y−, Z+, Z− (выходящие тоже), берётся ПЕРВАЯ найденная
	//   (а не ближайшая); `mathRayPlaneIntersect` в режиме луча (t ≥ 0), границы грани включительно. Всё в игровых осях.
	static bool RayVsObb6FacesHit(const FVector& OGame, const FVector& DGame, const FVector& Center, const FVector& Half, float& OutT);
	// Попадание `PickForClick` пришлось в ОБЪЕКТ карты (тип 4 эталона `collide_rayVsMapObjects`), а не в террейн/воду/небо.
	//   ★ Ревизия В4 ВАЖН-2: по КОМПОНЕНТУ попадания (объекты карты — экземпляры ISM на акторе `AsdaMapObjects_<карта>`).
	//   ★ В45fix2 (МЕЛК-4): только ISM/HISM на простом `AActor` (держатель объектов карты), прочие статические меши — не соперник.
	static bool IsMapObjectHit(const UPrimitiveComponent* HitComp);
	// ★ [ПОРТ] 12.10.2026 (ключ `asda.Fish.ZonePickWaterOnly`): точка попадания `PickForClick` лежит НА ВОДЕ — компонент воды
	//   (`SM_water`) либо над точкой есть плоскость воды `.WAT` и точка не выше её поверхности (+ допуск kWaterHitTolerance:
	//   у `SM_water` простой коллизии нет, луч упирается в дно под водой). У эталона такой проверки НЕТ (см. шапку).
	static bool IsWaterHit(UWorld* W, const UPrimitiveComponent* HitComp, const FVector& UePoint);
	// Допуск по высоте для `IsWaterHit` (ед. UE = см): погрешность трейса/плоскости, не «берег у кромки».
	static constexpr float kWaterHitTolerance = 5.f;
	// ★ Ревизия В4 МЕЛК-9: виджет лежит внутри игрового вьюпорта (игровой UI), а не в окне редактора/приложения.
	static bool IsGameUiWidget(const TSharedPtr<SWidget>& W);

	// Созданные зоны текущей карты (порядок = порядок спотов = порядок списка эталона).
	const TArray<TWeakObjectPtr<AAsdaFishZoneActor>>& GetZones() const { return Zones; }
	// Карта, для которой созданы зоны (−1 — нет).
	int32 GetZonesMapId() const { return ZonesMapId; }
	// Удалить зоны и пересоздать на следующем кадре (консоль/самотест). ★ Ревизия В4 МЕЛК-4б: заодно сбрасывается кэш
	//   «в этом мире воды нет» (`AsdaGround::ResetWaterCache`) — вода, появившаяся позже (подуровень), будет найдена.
	void ForceRebuild();
	// Дамп (консоль `asda.Fish.ZoneDump`).
	FString DumpZones() const;
	// Идёт ли рыбалка (fishState ≠ 0) — для препроцессора клавиш. ★ В3 S7 (N14): пересылка в `UAsdaFishingSubsystem::IsFishingNow`.
	bool IsFishingNow() const;
	// Число событий клавиатуры, поглощённых гейтом окон (самотест).
	int32 GetKeyGuardEaten() const { return KeyGuardEaten; }
	// Поле ввода, запертое на время рыбалки (самотест/диагностика).
	bool IsTextFieldLocked() const { return LockedEdit.IsValid(); }

	// Препроцессор: решить судьбу клавиши (true — поглощена). Зовётся из `FAsdaFishKeyGuard`.
	bool GuardKey(const struct FKeyEvent& Ev, bool bDown);

private:
	// `worldMapPlaceMarkersByType@0xFC3340` для MapId в мире W.
	void SpawnZones(UWorld* W, int32 MapId);
	// `mapReleaseAllSceneObjects(g_pMap+0x140)`.
	void DestroyZones();
	// Сверка (Tick): нужна ли пересборка.
	void ReconcileZones();
	// Метки FishingArea: если список миникарты пуст (его чистит сброс сессии при смене карты) — заполнить ВСЕМИ спотами.
	void EnsureMapMarks();
	// Замок поля ввода текста на время рыбалки (символы `OnKeyChar` препроцессор Slate не видит).
	void TickTextFieldLock();
	// Снять замок (вернуть прежний «только чтение»).
	void ReleaseTextFieldLock();

	// Зоны текущей карты (список `g_pMap+0x140+0x724` = 0x35202D4).
	TArray<TWeakObjectPtr<AAsdaFishZoneActor>> Zones;
	// Мир и карта, для которых созданы зоны.
	TWeakObjectPtr<UWorld> ZonesWorld;
	// id карты зон (−1 — нет).
	int32 ZonesMapId = -1;
	// Препроцессор клавиш (Slate).
	TSharedPtr<class FAsdaFishKeyGuard> KeyGuard;
	// Счётчик поглощённых событий (самотест).
	int32 KeyGuardEaten = 0;
	// Поле ввода, запертое замком (`SEditableText` или `SMultiLineEditableText`, ревизия В4 МЕЛК-2).
	TWeakPtr<SWidget> LockedEdit;
	// Прежний «только чтение» запертого поля.
	bool bLockedEditWasReadOnly = false;
	// Запертое поле — многострочное (`SMultiLineEditableText`).
	bool bLockedEditMulti = false;
	// Поле с фокусом, признанное НЕ игровым (окно редактора в PIE, МЕЛК-9) — чтобы не искать путь виджета каждый кадр.
	TWeakPtr<SWidget> NotGameFocus;
	// Предупреждение «нет меша fs_zone» уже было.
	bool bMeshMissingWarned = false;
};
