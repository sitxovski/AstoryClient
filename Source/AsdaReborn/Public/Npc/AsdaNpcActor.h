// Copyright (c) 2026 @sitxovski (Discord)
// AAsdaNpcActor — полноценный игровой NPC (порт AsdaStory). Editor-Friendly: размещается в уровне как
//   актор, все поля редактируются в редакторе, меш/идл видны сразу.
// Эталон: NPC = сущность node[116]==14; тик effectListUpdateType14 @0xF90BD0 — «idle = просто проигрывание
//   анима» (клип <model>_0.jbb), поворот статичен (из авторских данных). Позиции — серверные
//   NpcData_Info\<Map>.npc (stride 0xDD: type-id@+4, tileX@+8, tileZ@+0xA, поворот°@+0xC; мир=тайл*100+50,
//   Y=снап террейна). Модель: npc_list.BIN (stride 0x88: имя@+4, модель@+0x46, тип-функции@+0x7C).
// АРХИТЕКТУРА (не ломает сетевой флоу): акторы в уровне = авторская расстановка (тот же источник, что
//   читает сервер). При входе в мир сетевой спавн 0xFAD НЕ создаёт дубль — находит запечённого актора по
//   NpcId(+тайлу) и привязывает renderId (adoption, поле NetRenderId). Мобы в уровень не запекаются.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsdaNpcActor.generated.h"

UCLASS()
class ASDAREBORN_API AAsdaNpcActor : public AActor
{
	GENERATED_BODY()

public:
	AAsdaNpcActor();

	// Скелет-меш NPC (модель <model>.jbm, импортирована в /Game/AsdaAssets/Models/NPC/<model>/).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|NPC")
	TObjectPtr<class USkeletalMeshComponent> Mesh;

	// ★ Хитбокс для пикинга (клик/наведение по габаритам — скелетал без физ-ассета трейсом не ловится).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|NPC")
	TObjectPtr<class UCapsuleComponent> PickHitbox;

	// ★ Над-головная плашка имени (порт JUI 1:1). Создаётся в конструкторе → видна в РЕДАКТОРЕ.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|NPC")
	TObjectPtr<class UAsdaNameplateComponent> Nameplate;

	// ★ КВЕСТОВАЯ МЕТКА НАД ГОЛОВОЙ (эталон: три слота-модели в сущности NPC — dword 207/208/209,
	//   322/323/324, 437/438/439; загрузка entity_char.c:59290-59380, включение
	//   npcSlotLoadModelAndSetup@0xD8A6B0). Модели: eff_0_npc_99_exclam-a_00 (жёлтый «!»),
	//   _01 (красный «!»), _02 (пурпурный «?»); у каждой своя анимация _0.jbb.
	//   Одновременно активна РОВНО ОДНА — поэтому в порту один компонент со сменой меша.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|NPC")
	TObjectPtr<class USkeletalMeshComponent> QuestMark;

	// ★ ОБВОДКА МЕТКИ (эталон: ВТОРОЙ проход по той же геометрии — entity_char.c:8986-8990 ставит в
	//   очередь И обычный меш, И glow-узел pbb). Выдавливание по нормали делает вершинный шейдер
	//   (`mad r0.xyz, normal, c92.w`), цвет+альфа — плоские из c91. В порту: второй скелетал-меш с
	//   тем же мешем/клипом и материалом M_AsdaQuestMarkOutline (WPO = нормаль × Thickness).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|NPC")
	TObjectPtr<class USkeletalMeshComponent> QuestMarkOutline;

	// Обводка включена (эталон её строит всегда; выключатель — для отладки/сравнения).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	bool bQuestMarkOutline = true;

	// Поставить метку: 0 = снять, 1 = жёлтый «!» (квест можно взять), 2 = красный «!» (можно сдать),
	//   3 = пурпурный «?». Считает UAsdaQuestMarkerSubsystem.
	UFUNCTION(BlueprintCallable, Category="Asda|NPC")
	void SetQuestMark(int32 Mark);

	// Текущая метка (для отладки/редактора).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|NPC", Transient)
	int32 QuestMarkKind = 0;

	// ★★★ ВЫСОТА КВЕСТОВОЙ МЕТКИ — ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА ПО ПРОСЬБЕ ВЛАДЕЛЬЦА.
	//   ЭТАЛОН: метка стоит на «верх габаритов NPC + 60» (entity_char.c case 9/0xA/0xB:
	//     posSetWithChildOffset(slot, x, helper_getSumF(poseMatrix) + 60.0, z), helper_getSumF@0x97D6D0).
	//   ВЛАДЕЛЕЦ (2026-08-01): ориентир — ПЛАШКА ИМЕНИ. (2026-08-22): «знаки нужно поднять, они
	//     должны быть НАД ИМЕНАМИ NPC, плюс-минус на 10 пунктов».
	//   ⚠ ПОЧЕМУ ПРЕЖНЕЕ «якорь плашки + 5» ПРОМАХИВАЛОСЬ: плашка — ЭКРАННЫЙ виджет, её окно
	//     ставится на 50 экранных пунктов ВЫШЕ проекции якоря, а строка имени лежит ещё ниже в
	//     канвасе ⇒ верх имени идёт на 34 пункта выше якоря. Прибавка «+5 мировых» этих 34 пунктов
	//     не покрывает никогда (замер alpen: якорь 150.0 при верхе габаритов модели 194.2 — знак
	//     стоял даже НИЖЕ макушки). Полный вывод правила — в шапке ApplyQuestMarkHeight.
	//   Это поле теперь ФОЛБЭК: применяется, только когда проекции нет (нет PlayerController,
	//     точка за камерой, редакторский мир). CVar Asda.Quest.MarkOffsetOverNameplate (>= 0).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	float QuestMarkOffsetOverNameplate = 5.f;

	// ★★★ ОСНОВНОЕ ПРАВИЛО: зазор «низ метки — ВЕРХ СТРОКИ ИМЕНИ» в ЭКРАННЫХ ПУНКТАХ (пиксели
	//   вьюпорта, делённые на DPI, — те же единицы, в которых плашка ставит своё окно). В мировых
	//   единицах эта величина не выражается: плашка экранная, и один пункт стоит 0.18…1.3 мировых
	//   юнита в зависимости от зума (штанга камеры 170…1270). CVar Asda.Quest.MarkGapOverNamePx.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	float QuestMarkGapOverNamePx = 10.f;

	// ★ ПОЛ высоты: метка — трёхмерный меш и честно закрывается геометрией, поэтому ниже макушки
	//   модели её опускать нельзя (знак утонет в голове). Просвет НАД верхом габаритов модели,
	//   мировые единицы. CVar Asda.Quest.MarkMeshClearance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	float QuestMarkMeshClearance = 5.f;

	// Пол включён. Выключить (CVar Asda.Quest.MarkClearMesh 0) = метка идёт СТРОГО по экранному
	//   правилу — у высоких моделей она тогда въезжает в геометрию, поэтому по умолчанию пол есть.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	bool bQuestMarkClearMesh = true;

	// ★ Вернуть эталонную формулу высоты («верх габаритов + 60») вместо привязки к плашке имени.
	//   Код эталона НЕ удалён — переключается флагом (или CVar Asda.Quest.MarkUseEtalonHeight 1).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	bool bQuestMarkEtalonHeight = false;

	// Пересчитать высоту метки (после смены меша/подгонки капсулы/правки полей). Идемпотентно.
	//   ★ Зовётся ПОКАДРОВО, пока метка висит: экранное правило зависит от положения камеры.
	UFUNCTION(BlueprintCallable, Category="Asda|NPC")
	void ApplyQuestMarkHeight();

	// Верх габаритов МОДЕЛИ над origin актора (мировые единицы, габариты ассета — референс-поза).
	//   Общая база и для пола высоты метки, и для эталонной формулы «верх габаритов + 60».
	UFUNCTION(BlueprintCallable, Category="Asda|NPC")
	float MeshTopZ() const;

	// ★ 29.10.2026 [ПОРТ] Статичный NPC (класс сущности 1/3/5 — `TypeFunc` или `DT_NpcQuestNpc.NpcType`): плашка — на верх модели.
	bool IsStaticPlateType() const;

	// ★ Единая точка «нужен ли актору тик» (высота метки / tween обводки / цепочка состояний).
	void UpdateTickNeed();

	// ★ ВЫСОТА ПЛАШКИ ИМЕНИ — ФИКСИРОВАННАЯ КОНСТАНТА ИЗ ДАННЫХ, НЕ ГАБАРИТЫ МЕША.
	//   Эталон: worldEntityInitByType@0xF98B30 строит AABB сущности как
	//     полу-размер (50, H/2, 50) и центр (posX, posY + H/2, posZ)  — sub_9333FB@0x9333FB →
	//     sub_B5B030@0xB5B030 (this[0..2]=полу-размер, this[3..5]=центр, оффсеты сущности +424…+444).
	//   Плашку ставит npcNameplateUpdateLoop@0xF93000: мировая точка Y =
	//     helper_getSumF(entity+424)@0x97D6D0 = centerY + halfY = posY + H  (K = 0), экран `screenY − 50`.
	//   H = npc_list.BIN +0x68 (heightParam); −1 ⇒ 150; для тип-функции (+0x7C) == 2 эталон берёт 1500.
	//   0 = взять из DT_NpcQuestNpc по NpcId; > 0 — ручное переопределение в редакторе.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	float PlateHeightOverride = 0.f;

	// ★ Тип-функции 1/3/5 (доски, гробницы, камни, деревья): у ОРИГИНАЛА плашки имени у них НЕТ вовсе — контролы 1660/1661
	//   создаются только классу 0 (`Entity_StreamLoadAndInitFSM@0xF99380`, подсостояние 0xC; разбор — у `ResolvePlateHeight`
	//   в .cpp). `npc_list +0x68` у них — НЕ высота плашки, а подъём верха короба КЛИКА (у доски 1000). Поэтому порт ставит
	//   плашку на ВЕРХ МОДЕЛИ ([ПОРТ], ключ `asda.Npc.StaticPlateOnModel`, 0 — прежнее: верх капсулы + NameplateTopOffset).
	//   true — применять табличную высоту `+0x68` и к ним (только для сверки).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	bool bPlateHeightForStaticTypes = false;

	// Высота плашки для этого NPC (см. PlateHeightOverride): класс 0/4 — константа данных, 2 — 1500, статичные 1/3/5 —
	//   верх модели; 0 — данных нет (плашка садится на верх капсулы + NameplateTopOffset).
	UFUNCTION(BlueprintCallable, Category="Asda|NPC")
	float ResolvePlateHeight() const;
	// ★ Объектный путь `DT_NpcQuestNpc` для высоты плашки (15.09.2026, dt_gc_hold): читает `ResolvePlateHeight`,
	//   удерживает на сессию `UAsdaWorldSubsystem::HoldSpawnTables(Npc)`.
	static constexpr const TCHAR* kNpcQuestNpcPath = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcQuestNpc.DT_NpcQuestNpc");

	// Проставить плашке эталонную высоту (идемпотентно; зовётся из OnConstruction и BeginPlay).
	UFUNCTION(BlueprintCallable, Category="Asda|NPC")
	void ApplyNameplateHeight();

	// Идл-анимация (клип <model>_0.jbb → <model>_Anim). Эталон: NPC всегда проигрывает этот клип лупом.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	TObjectPtr<class UAnimSequence> IdleAnim;

	// ★★★ ПОРТ `uiPanelSelectTab@0xF947A0` (мисномер: это НЕ панель UI, а СМЕНА АНИМАЦИОННОГО
	//   СОСТОЯНИЯ СУЩНОСТИ; ui_core.c:98500). Ставит состояние `entity+161` и ПЕРЕЗАПУСКАЕТ клип
	//   этого состояния с нулевого кадра (`CObject_SetField13(anim, 0)`).
	//   Зачем в порту: эталон при открытии окна разговора зовёт `uiPanelSelectTab(npc, 1, 0)` —
	//   ОБА пути ведут в эту строку (клик в зоне: ui_core.c:1131; прибытие по тику:
	//   entity_char.c:18321). Без него NPC при разговоре продолжал стоять в idle.
	//   ★ Гейт эталона: у типа-функции 1 функция не делает НИЧЕГО (`if (entity[182] != 1)`),
	//     а состояния 1/2/3 включаются только у типов 0 и 4 — воспроизведено дословно.
	UFUNCTION(BlueprintCallable, Category="Asda|NPC")
	void SetEntityAnimState(int32 State);

	// Текущее анимационное состояние сущности (эталон `entity+161`). −1 = ещё не задано.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|NPC", Transient)
	int32 EntityAnimState = -1;

	// ★ Порт `objSetFlag0x1000ById(g_pEntityList, rid)` (`flags |= 0x1000`) — «доиграть разговорную
	//   петлю и уйти в idle». Зовётся закрытием окна диалога (`dialogOnConfirmClose@0xDA3BB0`).
	UFUNCTION(BlueprintCallable, Category="Asda|NPC")
	void RequestEntityAnimFinish();

	// TYPE-ID NPC (1-based, строка npc_list.BIN = id-1). Ключ adoption при сетевом спавне 0xFAD.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	int32 NpcId = 0;

	// Отображаемое имя (npc_list.BIN +4) — для будущего nameplate (uiNameplateUpdateType14 @0xF92EA0).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	FString NpcName;

	// Тип-функции NPC (npc_list.BIN +0x7C): 0=квестодатель («!» eff_0_npc_99_exclam), 1/3=статичный,
	//   прочие 2..8 — ветки клика (диалог/данж-окно, g_NpcTextTable+124). Пока информационно.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	int32 TypeFunc = 0;

	// Авторские данные размещения (из NpcData_Info\<Map>.npc) — для adoption/отладки.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	int32 TileX = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	int32 TileZ = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	int32 FacingDeg = 0;

	// ★ Отключённый NPC: серверный критерий — отрицательный tileX/tileZ в .npc (LoadNpcInfo пропускает
	//   запись). В игре скрыт (BeginPlay), в редакторе виден (папка NPC/Disabled).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|NPC")
	bool bDisabled = false;

	// ★★★ 22.10.2026 [ОРИГИНАЛ] КОРОБА ВЗАИМОДЕЙСТВИЯ СТАТИЧНЫХ NPC (доски, гробницы, камни, стены — классы сущности 1/3/4/5).
	//   Эталон `Entity_StreamLoadAndInitFSM` (entity_char.c:59550-59649 — классы 1/3/5, 60240-60342 — класс 4; разбор
	//   `Reports/npc_pick_zone_re_2026-10-22.md`): у таких сущностей ни хитбокс выбора мышью (`+189`), ни зона «дошёл»
	//   (`+183`) не строятся вокруг начала координат, как у говорящих NPC (±50), — их задают ДВЕ служебные кости модели
	//   `b_bone01`/`b_bone02` (первый ключ клипа `_0.jbb`), повёрнутые на угол NPC и сдвинутые в его позицию; при высоте
	//   `npc_list +104` ≠ −1 верх короба поднимается на неё. Зона «дошёл»: классы 1/3/5 — этот короб +200 по трём осям;
	//   класс 4 — короб геометрии `.jbm` с центром в позиции NPC +200 по X и Z. Таблица костей и полуразмеров — генератор
	//   `Tools/asda_gen_npc_interact_boxes.py` (в импортированных скелетах эти кости выброшены — у них нет весов).
	//   Без короба (обычный NPC или модели нет в таблице) — false, вызывающий ведёт себя как раньше.
	// Короб выбора мышью в мире UE (кости модели).
	UFUNCTION(BlueprintCallable, Category="Asda|NPC")
	bool GetPickBoxWorld(FBox& Out) const;
	// Зона «дошёл» в мире UE (см. выше).
	UFUNCTION(BlueprintCallable, Category="Asda|NPC")
	bool GetTalkBoxWorld(FBox& Out) const;
	// Короб, по которому показывается выделение и ловится клик: короб костей ∪ видимые габариты модели ([ПОРТ]: у досок
	//   короб костей уже самой доски — 74 ед. у столба, — и клик по щиту мимо него был бы промахом; владелец 22.10.2026:
	//   «доска игнорирует клики»). false — как у `GetPickBoxWorld`.
	UFUNCTION(BlueprintCallable, Category="Asda|NPC")
	bool GetSelectBoxWorld(FBox& Out) const;
	// Поставить капсулу клика по коробу выделения (статичные объекты; остальным — ничего). Зовут OnConstruction, BeginPlay и
	//   «удочерение» NPC сетевым пакетом 4067 (там же включается идл-анимация — габариты модели меняются).
	UFUNCTION(BlueprintCallable, Category="Asda|NPC")
	void ApplyStaticPickBox();
	// Класс сущности `npc_list +124` (DT_NpcQuestNpc.NpcType, иначе запечённый TypeFunc) и высота `+104` (−1 = не задана).
	int32 GetEntityClass() const;
	float GetNpcListHeight() const;
	// ★ 04.11.2026: horizontal scale applied to the select box (click capsule, ring) and the talk box — `asda.Npc.BoardZoneScale`
	//   (0.5) for bulletin boards (class 5), 1 for every other NPC. Reasoning — above `GetTalkBoxWorld` in the .cpp.
	float GetZoneScaleXY() const;

	// renderId сетевой сущности (4067) — только у РАНТАЙМ-копий (-1 = запечённый превью-актор).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|NPC")
	int32 NetRenderId = -1;

	// ★ СЕТЕВАЯ АРХИТЕКТУРА (2026-07-23, как оригинал): запечённые акторы = ПРЕВЬЮ РЕДАКТОРА (каталог
	//   шаблонов; в игре скрыты BeginPlay-ом). Игровые NPC = рантайм-КОПИИ шаблона (SpawnActor Template)
	//   по сетевым пакетам 4067; живут/умирают по сети (4011). bNetSpawned=true только у копий.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|NPC", Transient)
	bool bNetSpawned = false;

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;   // имя в плашку (видно в редакторе)
	virtual void Tick(float DeltaSeconds) override;                      // только пока висит обводка метки

private:
	// ★ ПОРТ ТWEEN-А ЭТАЛОНА uiTweenUpdatePhase@0x1250ED0 (ui_core.c:103744-103890) для обводки:
	//   4-фазный ping-pong, константы из вызова pbbBuildStripBatch у метки (entity_char.c:59348):
	//   толщина 3.0→5.0, альфа 0.1→0.15→0.2, фаза 3000 мс, средний ключ 1500 мс, удержание 500 мс.
	void TickQuestMarkOutline(float DeltaSeconds);
	// ★ Порт `animEffectUpdateChannel@0xF98430` для типа-функции 0: цепочка 1→2→(0x1000)→3→0.
	void TickEntityAnimState();
	// Флаг сущности 0x1000 «закончить разговор» (эталон `entity+197 |= 0x1000`).
	bool bEntityAnimFinishRequested = false;
	// Динамический материал обводки (цвет/толщина/альфа гоняются покадрово).
	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> QuestMarkOutlineMID = nullptr;
	// Таймер тween-а, МИЛЛИСЕКУНДЫ (эталон считает в мс) и номер фазы 0..3.
	float QuestMarkTweenMs = 0.f;
	int32 QuestMarkTweenPhase = 0;
};
