// Copyright (c) 2026 @sitxovski (Discord)
// Реализация AAsdaNpcActor — см. AsdaNpcActor.h (эталон: тик NPC effectListUpdateType14 @0xF90BD0).
#include "Npc/AsdaNpcActor.h"
#include "World/AsdaOverheadLate.h"   // ★ 30.10.2026: высота квестовой метки — по камере этого кадра
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Npc/AsdaMobFxMaterial.h"   // ★ 01.11.2026 (mob_fx_audit): ключ материалов подсеток-эффектов
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Npc/AsdaNpcMissing.h"   // ★ 09.11.2026: модель-замена для NPC без модели
#include "World/AsdaPickHitbox.h"   // ★ ДЕДУП Д9: политика и подгонка капсулы пикинга — одна точка на порт
#include "World/AsdaCapsuleShadow.h"   // ★ Сет B: капсульная тень тела — одна точка на порт (игрок/моб/NPC)
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д9: ресурс только через менеджер
#include "World/AsdaBlendAnimInstance.h"   // ★ 04.11.2026 (npc_talk_anim): смешивание клипов разговора — общий инстанс AsdaAnimSmooth
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/SkeletalMesh.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaEntityText.h"   // ★ волна 3 п.14: имя NPC из таблицы — одна точка
#include "Text/AsdaLangRows.h"      // ★ 10.10.2026 (en_completeness): FEditorWorldScope — мир редактора без перевода имени
#include "Components/CapsuleComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"                       // IsGameWorld — экранное правило только в игре
#include "GameFramework/PlayerController.h"     // ProjectWorldLocationToScreen (замер пунктов/юнит)
#include "Kismet/GameplayStatics.h"             // GetPlayerController
#include "Blueprint/WidgetLayoutLibrary.h"      // GetViewportScale (DPI) — как у самой плашки
#include "EngineUtils.h"                        // TActorIterator — диагностический дамп высот
#include "Quest/AsdaQuestTypes.h"   // FAsdaNpcQuestNpcRow — npc_list.BIN (+0x68 высота, +0x7C тип-функции)

// Лог анимационных состояний NPC (порт uiPanelSelectTab) и прочих действий актора.
DEFINE_LOG_CATEGORY_STATIC(LogAsdaNpcAct, Log, All);

// ★ Настройка высоты квестовой метки (см. ApplyQuestMarkHeight). Отступление от эталона по просьбе
//   юзера: ориентир — плашка имени, а не габариты меша. −1 = брать значение из поля актора.
//   ⚠ Это ФОЛБЭК-прибавка (мировые единицы) — работает, только когда проекции нет (нет
//     PlayerController, точка за камерой, редакторский мир). Основное правило — экранное, см. ниже.
static float GQuestMarkOffsetOverNameplate = -1.f;
static ASDA_DEV_CVAR_REF(CVarQuestMarkOffsetOverNameplate,
	TEXT("Asda.Quest.MarkOffsetOverNameplate"),
	GQuestMarkOffsetOverNameplate,
	TEXT("ФОЛБЭК-высота квестовой метки НАД якорем плашки имени, мировые единицы (когда проекции ")
	TEXT("нет). -1 = поле актора QuestMarkOffsetOverNameplate."),
	ECVF_Default);

// ★★★ ОСНОВНОЕ ПРАВИЛО (просьба владельца 2026-08-22: «знаки должны быть НАД ИМЕНАМИ, ±10 пунктов»).
//   Зазор между ВЕРХОМ СТРОКИ ИМЕНИ и НИЗОМ МЕТКИ, в ЭКРАННЫХ единицах (пункты = пиксели вьюпорта
//   до деления на DPI — те же, в которых плашка ставится SetPositionInViewport). −1 = поле актора.
static float GQuestMarkGapOverNamePx = -1.f;
static ASDA_DEV_CVAR_REF(CVarQuestMarkGapOverNamePx,
	TEXT("Asda.Quest.MarkGapOverNamePx"),
	GQuestMarkGapOverNamePx,
	TEXT("Зазор «низ метки — верх имени» в ЭКРАННЫХ пунктах. -1 = поле актора QuestMarkGapOverNamePx ")
	TEXT("(по умолчанию 10)."),
	ECVF_Default);

// ★ ПОЛ высоты: метка не должна въезжать в саму модель NPC (она трёхмерная и честно закрывается
//   геометрией головы). Мировые единицы НАД верхом габаритов модели. −1 = поле актора.
static float GQuestMarkMeshClearance = -1.f;
static ASDA_DEV_CVAR_REF(CVarQuestMarkMeshClearance,
	TEXT("Asda.Quest.MarkMeshClearance"),
	GQuestMarkMeshClearance,
	TEXT("Минимальный просвет метки над верхом габаритов модели NPC, мировые единицы. -1 = поле ")
	TEXT("актора QuestMarkMeshClearance (по умолчанию 5)."),
	ECVF_Default);

// ★ Выключатель ПОЛА (для сверки/отката): 0 = пола нет, метка идёт ровно по экранному правилу,
//   1 = пол включён, −1 = брать из поля актора bQuestMarkClearMesh.
static int32 GQuestMarkClearMesh = -1;
static ASDA_DEV_CVAR_REF(CVarQuestMarkClearMesh,
	TEXT("Asda.Quest.MarkClearMesh"),
	GQuestMarkClearMesh,
	TEXT("1 = метка не опускается ниже макушки модели (пол), 0 = только экранное правило, ")
	TEXT("-1 = поле актора bQuestMarkClearMesh (по умолчанию включено)."),
	ECVF_Default);

// Вернуть эталонную формулу «верх габаритов + 60» (глобально, для сверки с оригиналом).
static int32 GQuestMarkUseEtalonHeight = 0;
static ASDA_DEV_CVAR_REF(CVarQuestMarkUseEtalonHeight,
	TEXT("Asda.Quest.MarkUseEtalonHeight"),
	GQuestMarkUseEtalonHeight,
	TEXT("1 = высота квестовой метки по ЭТАЛОНУ (верх габаритов NPC + 60), 0 = плашка имени + оффсет."),
	ECVF_Default);

// ★★★ 29.10.2026 [ПОРТ] ПЛАШКА СТАТИЧНЫХ NPC (тип-функции 1/3/5) — НА ВЕРХ МОДЕЛИ (баг владельца: имя и «!» над доской
//   объявлений висели «оооочень высоко», за кадром). Разбор — в шапке `ResolvePlateHeight`. 0 — прежнее поведение (верх
//   капсулы клика + NameplateTopOffset; у доски капсула поднята на npc_list +0x68 = 1000 ⇒ плашка на ~1150 ед.).
static ASDA_DEV_CVAR(int32, CVarNpcStaticPlateOnModel, TEXT("asda.Npc.StaticPlateOnModel"), 1,
	TEXT("1 — [ПОРТ] плашка имени (и метка над ней) статичных NPC тип-функций 1/3/5 (доски, гробницы, камни, деревья) стоит на ")
	TEXT("верху модели; 0 — прежнее: верх капсулы клика (она поднята на npc_list +0x68 — у доски на 1000) + NameplateTopOffset."),
	ECVF_Default);

AAsdaNpcActor::AAsdaNpcActor()
{
	PrimaryActorTick.bCanEverTick = true;    // ★ тик включается по надобности — см. UpdateTickNeed()
	PrimaryActorTick.bStartWithTickEnabled = false;   // статичный NPC: анимацию гонит SkeletalMeshComponent

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("NpcMesh"));
	SetRootComponent(Mesh);
	// ★ ЗЕРКАЛО (см. AsdaMobActor): игра RH, конвертер дал лишний флип → негейт Y в локальном пространстве.
	Mesh->SetRelativeScale3D(FVector(1.f, -1.f, 1.f));
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->SetCastShadow(true);
	AsdaShadow::ApplyCapsule(Mesh);   // ★ Сет B: непрямая капсульная тень тела NPC (не QuestMark; ключ asda.Shadow.Capsule)
	// Тик позы только когда NPC в кадре (вне экрана анимация не считается — экономия CPU).
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	// ★ Хитбокс пикинга (только трейсы видимости).
	PickHitbox = CreateDefaultSubobject<UCapsuleComponent>(TEXT("PickHitbox"));
	PickHitbox->SetupAttachment(Mesh);
	AsdaPick::Configure(PickHitbox);   // дедуп Д9: политика коллизий — общая точка
	PickHitbox->SetCapsuleSize(45.f, 90.f);

	// ★ Плашка имени над головой (JUI 1:1). В конструкторе → видна в редакторе; у NPC HP нет.
	Nameplate = CreateDefaultSubobject<UAsdaNameplateComponent>(TEXT("Nameplate"));
	Nameplate->SetupAttachment(Mesh);
	Nameplate->Kind = EAsdaNameplateKind::Npc;
	Nameplate->bShowHp = false;

	// ★ Квестовая метка «!»/«?» над головой (модели eff_0_npc_99_exclam-a_0N).
	QuestMark = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("QuestMark"));
	QuestMark->SetupAttachment(Mesh);
	// Родитель (Mesh) отзеркален по Y (см. выше) и актор стоит в масштабе 0.01 (модели импортированы ×100).
	//   Компенсируем зеркало, чтобы «?» не читался наизнанку; итоговый мировой масштаб = 0.01 → 1:1 к игре.
	QuestMark->SetRelativeScale3D(FVector(1.f, -1.f, 1.f));
	// Эталон ставит метку в МИРОВЫХ координатах (posSetWithChildOffset), т.е. поворот NPC она не наследует.
	QuestMark->SetUsingAbsoluteRotation(true);
	QuestMark->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	QuestMark->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	QuestMark->SetCastShadow(false);
	QuestMark->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	QuestMark->SetVisibility(false);

	// ★ ОБВОДКА: второй проход по ТОЙ ЖЕ геометрии (эталон entity_char.c:8986-8990 — в очередь идут
	//   И обычный меш, И glow-узел pbb). Тот же меш и тот же клип, материал выдавливает вершины по
	//   нормали на Thickness. Дочерний к QuestMark → повторяет его трансформ 1:1 без своей логики.
	QuestMarkOutline = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("QuestMarkOutline"));
	QuestMarkOutline->SetupAttachment(QuestMark);
	QuestMarkOutline->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	QuestMarkOutline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	QuestMarkOutline->SetCastShadow(false);
	QuestMarkOutline->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	QuestMarkOutline->SetVisibility(false);
}

// Константы tween-а обводки — ровно аргументы pbbBuildStripBatch у метки (entity_char.c:59348):
//   pbbBuildStripBatch(model, R,G,B, 3.0, 5.0, 0.1, 0.15, 0.2, 3000.0, 1500.0, 500.0, ...)
static constexpr float kOutlineThickBegin = 3.0f;      // pbb+52
static constexpr float kOutlineThickEnd   = 5.0f;      // pbb+56
static constexpr float kOutlineAlpha0     = 0.10f;     // pbb+64  ключ 0
static constexpr float kOutlineAlpha1     = 0.15f;     // pbb+392 ключ 1 (средний)
static constexpr float kOutlineAlpha2     = 0.20f;     // pbb+68  ключ 2
static constexpr float kOutlineRampMs     = 3000.0f;   // pbb+60  длительность фазы нарастания
static constexpr float kOutlineMidKeyMs   = 1500.0f;   // pbb+396 время среднего альфа-ключа
static constexpr float kOutlineHoldMs     = 500.0f;    // pbb+40  длительность удержания

// ЦВЕТ ОБВОДКИ (c91) в linear. Эталон: (1.0, 0.5, 0.0) у обоих «!» (entity_char.c:59347/59365),
//   (1.0, 0.5, 0.7529412) у «?» (:59383). ★ Гамма: цвет игры задан в ГАММА-пространстве (D3D9 без
//   sRGB-writes), UE ждёт linear → sRGB→linear покомпонентно (закон ui-srgb-gamma-rule).
static const FLinearColor kOutlineColor[3] = {
	FLinearColor(1.0f, 0.214041f, 0.0f,      1.f),   // _00 жёлтый «!»
	FLinearColor(1.0f, 0.214041f, 0.0f,      1.f),   // _01 красный «!»
	FLinearColor(1.0f, 0.214041f, 0.527115f, 1.f),   // _02 пурпурный «?»
};
static const TCHAR* kOutlineMaster = TEXT("/Game/Materials/M_AsdaQuestMarkOutline.M_AsdaQuestMarkOutline");

// Модели/анимации меток (импортированы вместе с остальными NPC-моделями).
static const TCHAR* kQuestMarkMesh[3] = {
	TEXT("/Game/AsdaAssets/Models/NPC/eff_0_npc_99_exclam-a_00/SkeletalMeshes/eff_0_npc_99_exclam-a_00.eff_0_npc_99_exclam-a_00"),
	TEXT("/Game/AsdaAssets/Models/NPC/eff_0_npc_99_exclam-a_01/SkeletalMeshes/eff_0_npc_99_exclam-a_01.eff_0_npc_99_exclam-a_01"),
	TEXT("/Game/AsdaAssets/Models/NPC/eff_0_npc_99_exclam-a_02/SkeletalMeshes/eff_0_npc_99_exclam-a_02.eff_0_npc_99_exclam-a_02"),
};
static const TCHAR* kQuestMarkAnim[3] = {
	TEXT("/Game/AsdaAssets/Models/NPC/eff_0_npc_99_exclam-a_00/SkeletalMeshes/eff_0_npc_99_exclam-a_00_Anim.eff_0_npc_99_exclam-a_00_Anim"),
	TEXT("/Game/AsdaAssets/Models/NPC/eff_0_npc_99_exclam-a_01/SkeletalMeshes/eff_0_npc_99_exclam-a_01_Anim.eff_0_npc_99_exclam-a_01_Anim"),
	TEXT("/Game/AsdaAssets/Models/NPC/eff_0_npc_99_exclam-a_02/SkeletalMeshes/eff_0_npc_99_exclam-a_02_Anim.eff_0_npc_99_exclam-a_02_Anim"),
};

void AAsdaNpcActor::SetQuestMark(int32 Mark)
{
	if (!QuestMark) { return; }
	Mark = (Mark >= 1 && Mark <= 3) ? Mark : 0;
	if (Mark == QuestMarkKind) { return; }
	QuestMarkKind = Mark;

	if (Mark == 0)
	{
		QuestMark->SetVisibility(false);
		if (QuestMarkOutline) { QuestMarkOutline->SetVisibility(false); }
		UpdateTickNeed();                      // метки нет → тик, возможно, больше не нужен
		return;
	}

	const int32 Idx = Mark - 1;
	USkeletalMesh* SM = UAsdaModelAnimSubsystem::LoadMeshChecked(kQuestMarkMesh[Idx]);   // Д2
	if (!SM)
	{
		QuestMark->SetVisibility(false);
		if (QuestMarkOutline) { QuestMarkOutline->SetVisibility(false); }
		QuestMarkKind = 0;                     // меша нет — метки фактически нет
		UpdateTickNeed();
		return;
	}
	QuestMark->SetSkeletalMeshAsset(SM);
	// ★ ЭФФЕКТ МЕТКИ (реверс 2026-08-01). Отдельного .jeb/частиц у метки НЕТ (полный поиск `exclam`
	//   по архивам клиента: 3 меша + 3 клипа + 3 текстуры, в EffectModel.bnz записи нет). Эффект = два
	//   слагаемых:
	//   (A) КЛИП <модель>_0.jbb — 1 кость, 121 кадр, 30 fps, петля 4 с: парение pos.Y 0→11.57 + спин
	//       вокруг Y 81°/с. Кодовой пульсации нет (uiTweenNodeUpdate@0x11C0490 гоняет только UV-скролл,
	//       а он у меток нулевой) — играем клип лупом, см. ниже.
	//   (B) ГЛОУ-ПРОХОД pbbBuildStripBatch@0x11C15F0 (та же система, что ореол заточенного оружия):
	//       entity_char.c:59348/59366/59384 задаёт цвет (1.0,0.5,0.0) для обоих «!» и (1.0,0.5,0.7529)
	//       для «?»; тот же меш перерисовывается glow-шейдером, тинт едет в VS-константу c91 — В ТУ ЖЕ,
	//       куда обычный путь кладёт ЦВЕТ МАТЕРИАЛА (model_anim.c:24272) ⇒ свечение = текстура × цвет.
	//       objEffectCollectAttachPoints@0xF8FE70 ставит в очередь ТОЛЬКО glow-узел — обычный (lit)
	//       меш метки не рисуется вовсе. В порту это материал-уровень: три материала меток сидят на
	//       мастере M_AsdaMeshGlowTint (Unlit + Masked, Emissive = Tex×GlowTint), тинт = тот же цвет
	//       в linear (Tools/asda_build_questmark_glow.py — там же полный разбор и вывод гаммы).
	if (UAnimSequence* A = UAsdaModelAnimSubsystem::LoadClipChecked(kQuestMarkAnim[Idx]))   // Д2
	{
		QuestMark->PlayAnimation(A, /*bLooping=*/true);
	}

	// ★ ОБВОДКА: тот же меш и тот же клип, материал-выдавливание по нормали (см. блок выше).
	if (QuestMarkOutline)
	{
		const bool bWantOutline = bQuestMarkOutline;
		if (bWantOutline)
		{
			QuestMarkOutline->SetSkeletalMeshAsset(SM);
			if (UAnimSequence* A = UAsdaModelAnimSubsystem::LoadClipChecked(kQuestMarkAnim[Idx]))   // Д2
			{
				QuestMarkOutline->PlayAnimation(A, /*bLooping=*/true);
			}
			if (UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, kOutlineMaster, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				QuestMarkOutlineMID = UMaterialInstanceDynamic::Create(Master, this);
				for (int32 S = 0; S < QuestMarkOutline->GetNumMaterials(); ++S)
				{
					QuestMarkOutline->SetMaterial(S, QuestMarkOutlineMID);
				}
				QuestMarkOutlineMID->SetVectorParameterValue(TEXT("OutlineColor"), kOutlineColor[Idx]);
			}
			// Эталон стартует tween с фазы 0 (толщина = начало, альфа = ключ 0).
			QuestMarkTweenMs = 0.f;
			QuestMarkTweenPhase = 0;
			TickQuestMarkOutline(0.f);
		}
		QuestMarkOutline->SetVisibility(bWantOutline);
	}

	ApplyNameplateHeight();     // высота плашки актуальна (эталонная константа npc_list +0x68)
	ApplyQuestMarkHeight();
	QuestMark->SetVisibility(true);
	UpdateTickNeed();
}

// ★ ЕДИНАЯ ТОЧКА «нужен ли актору тик» — раньше три места включали/выключали тик по своим
//   условиям и уже начали расходиться (ветка «меша метки нет» гасила тик, даже если крутилась
//   цепочка разговорных состояний). Причины ровно три, все перечислены здесь.
void AAsdaNpcActor::UpdateTickNeed()
{
	const bool bNeed =
		(QuestMarkKind != 0)                    // ★ высота метки считается ПОКАДРОВО (экранное правило)
		|| (QuestMarkOutlineMID != nullptr)     // tween обводки метки
		|| (EntityAnimState > 0);               // цепочка разговорных состояний 1→2→3→0
	SetActorTickEnabled(bNeed);
}

// ★★★ [ОРИГИНАЛ] 30.10.2026 (asda.Overhead.LateProject, разбор — `World/AsdaOverheadLate.h`): высота квестовой метки считается
//   по экранному правилу (зазор над именем в пунктах), т.е. от КАМЕРЫ. Тик актора идёт ДО обновления камеры кадра — метка
//   ставилась по камере прошлого кадра и на наезде/повороте «плыла» относительно имени. При ключе 1 NPC с меткой, тикнувшие
//   в кадре, досчитываются одной общей подпиской «после обновления камеры».
namespace AsdaNpcQuestMarkLate
{
	TSet<TWeakObjectPtr<AAsdaNpcActor>> GPending;   // NPC с меткой, ждущие пересчёта по камере этого кадра
	FDelegateHandle GHook;                          // общая подписка «после камеры» (одна на процесс)

	void Enqueue(AAsdaNpcActor* N)
	{
		if (!GHook.IsValid())
		{
			GHook = AsdaOverheadLate::AddPostCamera([](UWorld* W, float /*Dt*/)
			{
				for (auto It = GPending.CreateIterator(); It; ++It)
				{
					AAsdaNpcActor* Npc = It->Get();
					if (!Npc) { It.RemoveCurrent(); continue; }
					if (Npc->GetWorld() != W) { continue; }
					Npc->ApplyQuestMarkHeight();
					It.RemoveCurrent();
				}
			});
		}
		GPending.Add(N);
	}
}

void AAsdaNpcActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// ★ Высота метки зависит от КАМЕРЫ (зазор задан в экранных пунктах), поэтому пересчитывается
	//   каждый кадр, пока метка висит. Две проекции на NPC — на карте их единицы, цена ничтожна.
	//   ★ 30.10.2026: при asda.Overhead.LateProject 1 — после обновления камеры этого кадра (см. выше).
	if (QuestMarkKind != 0)
	{
		if (AsdaOverheadLate::IsOn()) { AsdaNpcQuestMarkLate::Enqueue(this); }
		else { ApplyQuestMarkHeight(); }
	}
	TickQuestMarkOutline(DeltaSeconds);
	TickEntityAnimState();
}

// ── ★★★ ПОРТ ЦЕПОЧКИ РАЗГОВОРНОЙ АНИМАЦИИ NPC (`animEffectUpdateChannel@0xF98430`) ──────────────
//   Диспетчер по текущему состоянию (`entity+161`, model_anim.c:1160) для ТИПА-ФУНКЦИИ 0:
//     состояние 1 → `objAnimUpdateTab2Blend` (model_anim.c:1075): крутит слот 2; по концу клипа —
//                    `uiPanelSelectTab(this, 2, 0)` ⇒ ПЕРЕХОД В СОСТОЯНИЕ 2;
//     состояние 2 → `uiPanelAnimTabSwitch3` (ui_core.c:98645): крутит слот 3; по концу клипа —
//                    если флаг 0x1000 НЕ взведён: `CObject_SetField13(+192, 0)` = ПОВТОР слота 3
//                    (петля «говорит»); если взведён: снять флаг и ПЕРЕЙТИ В СОСТОЯНИЕ 3;
//     состояние 3 → `animSetChannelValueByMode` (model_anim.c:1116): крутит слот 4; по концу клипа —
//                    `uiPanelSelectTab(this, 0, 0)` ⇒ ВОЗВРАТ В IDLE;
//     состояние 0 → `objAnimUpdateFrame` — обычный idle-клип.
//   Флаг 0x1000 взводит `objSetFlag0x1000ById(g_pEntityList, npcRid)` (ui_core.c:98151, `flags |= 0x1000`),
//   а зовёт его ЗАКРЫТИЕ ДИАЛОГА — `dialogOnConfirmClose@0xDA3BB0` (ui_core.c:61646) и
//   `UI_resetAllWindowsAndLeave_5045` (ui_core.c:61241).
//   ⇒ Полный жизненный цикл: открыли окно → приветственный жест (клип 2) → петля «говорит» (клип 3)
//     → закрыли окно → прощальный жест (клип 4) → idle (клип 0).
//   ⚠ `sub_9394A4(g_pEntityList, 1)`, стоящий в закрытии рядом, в срезе эталона тела не имеет —
//     в порт не переносится, отмечено в отчёте.
void AAsdaNpcActor::TickEntityAnimState()
{
	if (EntityAnimState <= 0 || !Mesh) { return; }
	// «Клип доиграл» = порт предиката `Game_IsPausedOrMenu(anim)`: SingleNode без лупа замирает
	//   на последнем кадре, поэтому сравниваем позицию с длиной.
	const UAnimSingleNodeInstance* Inst = Mesh->GetSingleNodeInstance();
	const UAnimSequenceBase* Cur = Inst ? Cast<UAnimSequenceBase>(Inst->GetAnimationAsset()) : nullptr;
	if (!Cur) { return; }
	const float Len = Cur->GetPlayLength();
	if (Len <= 0.f) { return; }
	if (Mesh->GetPosition() < Len - KINDA_SMALL_NUMBER && Mesh->IsPlaying()) { return; }

	switch (EntityAnimState)
	{
	case 1:                                   // приветственный жест доигран → петля «говорит»
		SetEntityAnimState(2);
		break;
	case 2:
		if (bEntityAnimFinishRequested)       // флаг 0x1000 взведён закрытием окна
		{
			bEntityAnimFinishRequested = false;
			SetEntityAnimState(3);            // прощальный жест
		}
		else
		{
			Mesh->SetPosition(0.f, false);    // `CObject_SetField13(+192, 0)` — повтор слота 3
			Mesh->Play(false);
		}
		break;
	case 3:                                   // прощальный жест доигран → idle
		SetEntityAnimState(0);
		break;
	default:
		break;
	}
}

// Порт `objSetFlag0x1000ById(g_pEntityList, npcRid)` — «доиграть разговор и вернуться в idle».
void AAsdaNpcActor::RequestEntityAnimFinish()
{
	if (EntityAnimState <= 0) { return; }
	bEntityAnimFinishRequested = true;
}

// ★ ПОРТ 1:1 uiTweenUpdatePhase@0x1250ED0 (ui_core.c:103744-103890) для обводки квестовой метки.
//   Эталон считает в МИЛЛИСЕКУНДАХ и гоняет 4-фазный ping-pong (filler[N] = pbb+N):
//     фаза 0 (pbb+60 = 3000 мс): толщина filler[52]→filler[56] (3.0→5.0) линейно по t;
//                                альфа: 0..1500 мс — filler[64]→filler[392] (0.10→0.15),
//                                       1500..3000 мс — filler[392]→filler[68] (0.15→0.20);
//     фаза 1 (pbb+40 = 500 мс): удержание на (5.0, 0.20);
//     фаза 2 (3000 мс): обратный ход 5.0→3.0 и 0.20→0.15→0.10;
//     фаза 3 (500 мс): удержание на (3.0, 0.10).  Полный цикл 7000 мс.
void AAsdaNpcActor::TickQuestMarkOutline(float DeltaSeconds)
{
	if (!QuestMarkOutlineMID) { return; }
	QuestMarkTweenMs += DeltaSeconds * 1000.f;

	float Thick = kOutlineThickBegin;
	float Alpha = kOutlineAlpha0;

	// Переход между фазами: длительность фазы зависит от её типа (нарастание/удержание).
	for (int32 Guard = 0; Guard < 8; ++Guard)
	{
		const bool bHold = (QuestMarkTweenPhase == 1 || QuestMarkTweenPhase == 3);
		const float PhaseMs = bHold ? kOutlineHoldMs : kOutlineRampMs;
		if (QuestMarkTweenMs < PhaseMs) { break; }
		QuestMarkTweenMs -= PhaseMs;
		QuestMarkTweenPhase = (QuestMarkTweenPhase + 1) & 3;
	}

	// Альфа по трём ключам с изломом на kOutlineMidKeyMs (эталон lerp-ит двумя отрезками).
	auto AlphaRamp = [](float T) -> float
	{
		return (T < kOutlineMidKeyMs)
			? FMath::Lerp(kOutlineAlpha0, kOutlineAlpha1, T / kOutlineMidKeyMs)
			: FMath::Lerp(kOutlineAlpha1, kOutlineAlpha2, (T - kOutlineMidKeyMs) / (kOutlineRampMs - kOutlineMidKeyMs));
	};

	switch (QuestMarkTweenPhase)
	{
	case 0:   // нарастание
	{
		const float T = FMath::Clamp(QuestMarkTweenMs, 0.f, kOutlineRampMs);
		Thick = FMath::Lerp(kOutlineThickBegin, kOutlineThickEnd, T / kOutlineRampMs);
		Alpha = AlphaRamp(T);
		break;
	}
	case 1:   // удержание на максимуме
		Thick = kOutlineThickEnd;
		Alpha = kOutlineAlpha2;
		break;
	case 2:   // обратный ход (зеркало фазы 0)
	{
		const float T = FMath::Clamp(kOutlineRampMs - QuestMarkTweenMs, 0.f, kOutlineRampMs);
		Thick = FMath::Lerp(kOutlineThickBegin, kOutlineThickEnd, T / kOutlineRampMs);
		Alpha = AlphaRamp(T);
		break;
	}
	default:  // 3 — удержание на минимуме
		Thick = kOutlineThickBegin;
		Alpha = kOutlineAlpha0;
		break;
	}

	QuestMarkOutlineMID->SetScalarParameterValue(TEXT("Thickness"), Thick);
	QuestMarkOutlineMID->SetScalarParameterValue(TEXT("OutlineAlpha"), Alpha);
}

// ★ ЭТАЛОННАЯ ВЫСОТА ПЛАШКИ ИМЕНИ NPC (полный вывод — в шапке поля PlateHeightOverride).
//   Кратко: worldEntityInitByType@0xF98B30 кладёт в сущность AABB {полу-размер (50, H/2, 50);
//   центр (posX, posY + H/2, posZ)}; npcNameplateUpdateLoop@0xF93000 берёт для плашки
//   helper_getSumF(entity+424)@0x97D6D0 = centerY + halfY = posY + H. Значит высота плашки над землёй
//   есть КОНСТАНТА H из npc_list.BIN (+0x68), а не габариты модели.
//   H == −1 ⇒ 150 (эталонный дефолт `if (heightParam == -1) heightParam = 150;`).
//   Тип-функции 2 ⇒ 1500 (ветка case 2 использует жёсткое 1500, поле +0x68 игнорируется).
//   Тип-функции 1/3/5 ⇒ см. «СТАТИЧНЫЕ NPC» у `ResolvePlateHeight` ниже (верх модели, ключ asda.Npc.StaticPlateOnModel).
// ═══════════ ★★★ 22.10.2026 [ОРИГИНАЛ] КОРОБА ВЗАИМОДЕЙСТВИЯ СТАТИЧНЫХ NPC (разбор — у объявлений в заголовке) ═══════════
namespace AsdaNpcBoxes
{
	// Строка таблицы генератора `Tools/asda_gen_npc_interact_boxes.py`: точки костей b_bone01/b_bone02 (первый ключ клипа) и
	//   полуразмеры геометрии `.jbm` — в координатах МОДЕЛИ оригинала (ось Y — вверх).
	struct FRow
	{
		const TCHAR* Model;              // имя модели (нижний регистр), как ассет скелетного меша
		float B1X, B1Y, B1Z;             // b_bone01
		float B2X, B2Y, B2Z;             // b_bone02
		float HX, HY, HZ;                // полуразмеры геометрии модели
	};
	static const FRow kRows[] = {
#include "AsdaNpcInteractBoxes.inl"
	};

	static const FRow* Find(const FString& Model)
	{
		for (const FRow& R : kRows) { if (Model.Equals(R.Model, ESearchCase::IgnoreCase)) { return &R; } }
		return nullptr;
	}

	// Координаты модели оригинала → локальные оси меша UE. Оси сняты замером (22.10.2026, кость b_bone01 доски Фламмио: клип
	//   (35.8, 140.7, 8.0) → сокет UE при повороте −90 = смещение (−8, −36, +141)): X = x, Y = −z, Z = y, масштаб 1.
	static FVector ToUeLocal(float X, float Y, float Z) { return FVector(X, -Z, Y); }
}

int32 AAsdaNpcActor::GetEntityClass() const
{
	if (NpcId > 0)
	{
		if (const FAsdaNpcQuestNpcRow* R = AsdaDt::RowByInt<FAsdaNpcQuestNpcRow>(kNpcQuestNpcPath, NpcId, TEXT("GetEntityClass")))
		{
			return R->NpcType;
		}
	}
	return TypeFunc;
}

float AAsdaNpcActor::GetNpcListHeight() const
{
	if (NpcId > 0)
	{
		if (const FAsdaNpcQuestNpcRow* R = AsdaDt::RowByInt<FAsdaNpcQuestNpcRow>(kNpcQuestNpcPath, NpcId, TEXT("GetNpcListHeight")))
		{
			return static_cast<float>(R->PlateHeight);
		}
	}
	return -1.f;
}

// Строка таблицы для этого NPC: только классы 1/3/4/5 и модель с обеими костями.
static const AsdaNpcBoxes::FRow* AsdaNpcBoxRow(const AAsdaNpcActor* A, int32& OutClass)
{
	OutClass = A->GetEntityClass();
	if (OutClass != 1 && OutClass != 3 && OutClass != 4 && OutClass != 5) { return nullptr; }
	const USkeletalMesh* SK = A->Mesh ? A->Mesh->GetSkeletalMeshAsset() : nullptr;
	return SK ? AsdaNpcBoxes::Find(SK->GetName()) : nullptr;
}

bool AAsdaNpcActor::GetPickBoxWorld(FBox& Out) const
{
	int32 Cls = 0;
	const AsdaNpcBoxes::FRow* R = AsdaNpcBoxRow(this, Cls);
	if (!R) { return false; }
	// Эталон: обе ТОЧКИ костей поворачиваются на угол NPC (`Dispatch_CallPtr15B6008(m, [203])` + `mathMatTransformPoint3`),
	//   затем min/max по осям и сдвиг в позицию (entity_char.c:59561-59603). Угол NPC в порту — рыскание актора.
	const FRotator Yaw(0.f, GetActorRotation().Yaw, 0.f);
	const FVector Pos = GetActorLocation();
	const FVector P1 = Pos + Yaw.RotateVector(AsdaNpcBoxes::ToUeLocal(R->B1X, R->B1Y, R->B1Z));
	const FVector P2 = Pos + Yaw.RotateVector(AsdaNpcBoxes::ToUeLocal(R->B2X, R->B2Y, R->B2Z));
	FBox B(P1.ComponentMin(P2), P1.ComponentMax(P2));
	const float H = GetNpcListHeight();
	if (H != -1.f)
	{
		if (Cls == 4) { B.Min.Z = Pos.Z; B.Max.Z = Pos.Z + H; }   // класс 4: высота ЗАДАЁТ короб от земли (entity_char.c:60294-60297)
		else          { B.Max.Z += H; }                             // классы 1/3/5: верх поднимается на h (entity_char.c:59604-59605)
	}
	Out = B;
	return true;
}

// ★★★ 04.11.2026 [PORT, OWNER REQUEST] BULLETIN BOARDS (class 5: NPC 9 Silaris, NPC 10 Flammio, model mdel_raidboard-a_00) —
//   THE WHOLE INTERACTION ZONE 2× SMALLER (owner: "the bulletin board has a really big interaction trigger zone").
//   What the owner saw: the green ring R≈264 and the click capsule R≈264 × ~1180 tall come from the select box = bones ∪
//   `Mesh->Bounds`; the skinned component bounds of this model are inflated far past its geometry (UE world half-extents
//   192×264 vs the `.jbm`/asset geometry 53×158 — measured in `Reports/npc_pick_zone_2026-10-22/probe_after.log` and
//   re-measured by `asda.SelfTest.BoardZone`), so the ring/capsule circle is ~1.7× the board. The capsule stood 14 units from
//   Wizard Chmella's (Silaris, NPC 20, 353 units away) and, being ~1180 tall, covered rays to her from behind the board.
//   Rule: for class 5 the HORIZONTAL half-extents of the select box (→ click capsule and ring) and of the talk box (→ "arrived"
//   trigger) are multiplied by `asda.Npc.BoardZoneScale` around their centres. Height: the talk box keeps it; the CLICK column
//   (select box → capsule) ends at the model top instead of npc_list +104 = 1000 above the bones (entity_char.c:59602-59603).
//   [PORT DEFECT FIXED] in the original that +1000 sits on the thin bone pillar (±10×±37) and covers almost nothing; the port
//   put it on the wide model-sized capsule, a ~1180-tall column of air that ate clicks on NPCs standing behind the board
//   (Wizard Chmella: 943 of 2805 silhouette pixels, clickable from 4 of 6 sides — `asda.SelfTest.BoardZone`, 04.11.2026).
//   The whole board face stays clickable (model bounds are in the box).
//   The original has no ring at all and its click box is the thin bone box (±10×±37), talk = that +200 (entity_char.c:59640-59648):
//   ×0.5 of the talk box is an owner-requested deviation from the original, marked [PORT]. Other classes are untouched.
//   Key: 1 (or ≤0 / >1) — the previous size.
static ASDA_DEV_CVAR(float, CVarNpcBoardZoneScale, TEXT("asda.Npc.BoardZoneScale"), 0.5f,
	TEXT("[PORT] Bulletin boards (NPC class 5): horizontal scale of the click capsule, the selection ring and the talk ")
	TEXT("(arrived) zone around their centres. 0.5 — owner request 04.11.2026 (2x smaller); 1 — previous size. ")
	TEXT("Applies to the next selection (ring) and the next NPC adoption by 4067 (capsule)."),
	ECVF_Default);

// ★ 04.11.2026: non-board static NPCs — select box from the real model geometry (reasoning in `GetSelectBoxWorld`).
static ASDA_DEV_CVAR(int32, CVarNpcStaticZoneFromGeometry, TEXT("asda.Npc.StaticZoneFromGeometry"), 1,
	TEXT("[PORT FIX] Static NPCs of classes 1/3/4 (signs, stones, graves, trees…): click capsule and selection ring from the model ")
	TEXT("geometry and the bone points without the npc_list height rise. 0 — previous box (bones+height ∪ physics-asset bounds)."),
	ECVF_Default);

// Horizontal zone scale of this NPC: `asda.Npc.BoardZoneScale` for a bulletin board (class 5), 1 for everyone else.
static float AsdaNpcZoneScaleXY(const AAsdaNpcActor* A)
{
	if (!A || A->GetEntityClass() != 5) { return 1.f; }
	const float S = CVarNpcBoardZoneScale.GetValueOnGameThread();
	return (S > 0.f && S < 1.f) ? S : 1.f;
}

// Box with its X/Y half-extents multiplied by S around the same centre (height untouched).
static FBox AsdaScaleBoxXY(const FBox& B, float S)
{
	if (S == 1.f || !B.IsValid) { return B; }
	const FVector C = B.GetCenter();
	const FVector E = B.GetExtent();
	const FVector H(E.X * S, E.Y * S, E.Z);
	return FBox(C - H, C + H);
}

float AAsdaNpcActor::GetZoneScaleXY() const
{
	return AsdaNpcZoneScaleXY(this);
}

bool AAsdaNpcActor::GetTalkBoxWorld(FBox& Out) const
{
	int32 Cls = 0;
	const AsdaNpcBoxes::FRow* R = AsdaNpcBoxRow(this, Cls);
	if (!R) { return false; }
	if (Cls == 4)
	{
		// Класс 4: `+183` = `+106` — полуразмеры меша с центром в ПОЗИЦИИ NPC (без поворота), +200 по X и Z (entity_char.c:60335-60342).
		const FVector Half = AsdaNpcBoxes::ToUeLocal(R->HX, R->HY, R->HZ).GetAbs() + FVector(200.f, 200.f, 0.f);
		const FVector Pos = GetActorLocation();
		Out = FBox(Pos - Half, Pos + Half);
		return true;
	}
	// Классы 1/3/5: `+183` = `+189` (короб выбора) +200 по трём осям (entity_char.c:59640-59648).
	FBox B;
	if (!GetPickBoxWorld(B)) { return false; }
	Out = AsdaScaleBoxXY(B.ExpandBy(200.f), AsdaNpcZoneScaleXY(this));   // ★ 04.11.2026: boards ×0.5 (see above)
	return true;
}

// ★ 22.10.2026: у статичных объектов (доски, гробницы…) капсула клика — по коробу выделения (кости ∪ модель), стоит
//   вертикально в его центре. Раньше она висела в начале координат меша со сдвигом по ЛОКАЛЬНОЙ вертикали меша — у повёрнутой
//   модели сдвиг уходил вбок (доска Силариса: капсула в 270 ед. рядом с щитом, клик «игнорировался»). Зовётся из
//   `OnConstruction` (редактор) и `BeginPlay` (в собранной игре конструкция расставленных акторов не перезапускается).
// ★★★ 08.11.2026 [ОРИГИНАЛ + ИСПРАВЛЕНИЕ] ЗОНА КЛИКА ОБЫЧНЫХ NPC (класс 0) — КОРОБКА ОРИГИНАЛА, А НЕ ГАБАРИТЫ ФИЗ-АССЕТА.
//   Баг владельца (пачка 19, 08.11): щелчок по Бумибуми (NPC 131, Альпия) выбирал PrePre-v19 (NPC 146 — робот на пружине в 200 ед.).
//   ОРИГИНАЛ: `Input_WorldClickHandler` (input.c:1021-1058) перебирает NPC и берёт БЛИЖАЙШЕЕ по лучу попадание в короб +189
//     (`collide_rayVsObb6FacesHit`); у классов 0/4 короб +189 = +106 = центр (x, y + h/2, z), полуразмеры (50, h/2, 50), где h —
//     высота `npc_list +104`, при −1 — 150 (`Npc_InitByClass`, entity_char.c:58726-58745; 1112014848 = 50.0f). Класс 4 потом
//     перезаписывает короб костями (`GetPickBoxWorld`), у класса 0 он так и остаётся коробкой 100 × h × 100 у ног NPC.
//   ПОРТ ДО ПРАВКИ: капсула класса 0 — `AsdaPick::FitToMeshBounds` по `Mesh->Bounds` при постройке уровня. У скелетных мешей
//     NPC это AABB ФИЗ-АССЕТА, а не модели: у робота PrePre-v19 ½ (69, 230, 125) → капсула R 230 × полувысота 230,
//     и Бумибуми в 200 ед. стояла в ней почти целиком (замер `asda.SelfTest.NpcClickPick map=3`: при прежней зоне 1250 пикселей
//     её тела из 2382 видимых выбирали робота, на 25 из 46 ракурсов). Отчёт `Reports/batch19/npc_click/`.
//   ПРАВИЛО: капсула = короб оригинала: радиус 50, от земли до h (−1 → 150); [ИСПРАВЛЕНИЕ] верх не ниже верха модели (габариты
//     ассета, не физ-ассета), чтобы голова высокого NPC оставалась кликабельной. Тело шире короба (руки, шляпа) ловит
//     щелчок через `asda.Npc.ClickByBody` (AsdaPlayerController.cpp). Ключ `asda.Npc.ClickBoxOriginal` 1; 0 — прежнее правило
//     (`FitToMeshBounds` по `Mesh->Bounds`). Действует при BeginPlay и при «удочерении» NPC пакетом 4067.
static ASDA_DEV_CVAR(int32, CVarNpcClickBoxOriginal, TEXT("asda.Npc.ClickBoxOriginal"), 1,
	TEXT("[ORIGINAL+FIX] Click capsule of class 0 NPCs = the original box: radius 50, ground..npc_list height (-1 -> 150), top not below ")
	TEXT("the model top. 0 - previous rule: capsule from Mesh->Bounds (physics-asset bounds; the PrePre-v19 banner got R 230). ")
	TEXT("Applies on BeginPlay and on the next NPC adoption by 4067."),
	ECVF_Default);

static constexpr float kNpcOrigBoxHalfXY = 50.f;    // полуразмер короба +106 по X/Z у оригинала (entity_char.c:58734/58736)
static constexpr float kNpcOrigBoxDefaultH = 150.f; // высота при `npc_list +104` = −1 (entity_char.c:58731-58732)

// Класс 0: капсула по коробу оригинала (ключ 1) или прежнее правило `FitToMeshBounds` (ключ 0). false — не класс 0 / нет капсулы.
static bool AsdaApplyClass0ClickBox(AAsdaNpcActor* A)
{
	if (!A || !A->PickHitbox || !A->Mesh || A->GetEntityClass() != 0) { return false; }
	UCapsuleComponent* Cap = A->PickHitbox;
	if (CVarNpcClickBoxOriginal.GetValueOnGameThread() == 0)
	{
		// Прежнее правило (как `OnConstruction`): капсула по габаритам меша, прикреплена к мешу.
		Cap->SetUsingAbsoluteRotation(false);
		Cap->SetRelativeRotation(FRotator::ZeroRotator);
		AsdaPick::FitToMeshBounds(Cap, A->Mesh, A->GetActorScale3D());
		return true;
	}
	const FVector Pos = A->GetActorLocation();
	const float H = A->GetNpcListHeight();
	float Top = (H > 0.f) ? H : kNpcOrigBoxDefaultH;
	if (const USkeletalMesh* SK = A->Mesh->GetSkeletalMeshAsset())
	{
		// [ИСПРАВЛЕНИЕ] верх модели: габариты ассета в позе привязки, поставленные трансформом меша (не `Mesh->Bounds` — физ-ассет).
		const FBox Geo = SK->GetBounds().GetBox().TransformBy(A->Mesh->GetComponentTransform());
		if (Geo.IsValid) { Top = FMath::Max(Top, (float)(Geo.Max.Z - Pos.Z)); }
	}
	const float S = FMath::Max((float)FMath::Abs(Cap->GetComponentScale().Z), 1e-4f);
	const float HalfH = FMath::Max(Top * 0.5f, kNpcOrigBoxHalfXY);
	Cap->SetUsingAbsoluteRotation(true);
	Cap->SetWorldRotation(FRotator::ZeroRotator);
	Cap->SetWorldLocation(Pos + FVector(0.f, 0.f, HalfH));
	Cap->SetCapsuleSize(kNpcOrigBoxHalfXY / S, HalfH / S);
	return true;
}

void AAsdaNpcActor::ApplyStaticPickBox()
{
	if (AsdaApplyClass0ClickBox(this)) { return; }   // ★ 08.11.2026: класс 0 — короб оригинала (блок выше)
	FBox Sel;
	if (!PickHitbox || !GetSelectBoxWorld(Sel)) { return; }
	PickHitbox->SetUsingAbsoluteRotation(true);
	PickHitbox->SetWorldRotation(FRotator::ZeroRotator);
	const FVector Ext = Sel.GetExtent();
	const float S = FMath::Max((float)FMath::Abs(PickHitbox->GetComponentScale().Z), 1e-4f);
	const float Rad = FMath::Max3((float)Ext.X, (float)Ext.Y, 25.f);
	FVector Ctr = Sel.GetCenter();
	// ★ 04.11.2026 [PORT DEFECT FIXED] wide flat models (graves, colonies: radius > half-height): a capsule is at least a sphere, so
	//   centred in the box its dome rose up to (R − half-height) of air above the model (grave NPC 79: 465 units over a 1084-tall
	//   model) and took clicks behind it. With `asda.Npc.StaticZoneFromGeometry` on, the dome's apex is put at the model top instead.
	if (Rad > Ext.Z && CVarNpcStaticZoneFromGeometry.GetValueOnGameThread() != 0) { Ctr.Z = Sel.Max.Z - Rad; }
	PickHitbox->SetWorldLocation(Ctr);
	PickHitbox->SetCapsuleSize(Rad / S, FMath::Max((float)Ext.Z, Rad) / S);
}

bool AAsdaNpcActor::GetSelectBoxWorld(FBox& Out) const
{
	FBox B;
	if (!GetPickBoxWorld(B)) { return false; }
	const float ZoneS = AsdaNpcZoneScaleXY(this);
	if (ZoneS < 1.f)
	{
		// ★ 04.11.2026 boards: the click column ends at the MODEL top, not 1000 above it (see the block above GetTalkBoxWorld).
		const float H = GetNpcListHeight();
		if (H > 0.f) { B.Max.Z = FMath::Max(B.Min.Z, B.Max.Z - H); }
	}
	else if (CVarNpcStaticZoneFromGeometry.GetValueOnGameThread() != 0 && Mesh && Mesh->GetSkeletalMeshAsset())
	{
		// ★ 04.11.2026 [PORT DEFECT FIXED] every other static NPC (classes 1/3/4: signs, stones, graves, trees, wells, boxes…):
		//   the select box = the two bone points (WITHOUT the npc_list +104 rise) ∪ the model's real geometry (asset bounds placed by
		//   the mesh transform), instead of ∪ `Mesh->Bounds`. For these skinned meshes `Mesh->Bounds` is the physics-asset AABB,
		//   far bigger than the model (bulletin board: 192×264 vs 53×158); with the +h rise the capsule became a tall wide column
		//   of air that took clicks meant for NPCs/ground behind it. Measured per NPC by `asda.SelfTest.BoardZone all`.
		//   The talk ("arrived") box is untouched — it is the original's (bones +200). Key 0 — previous box.
		const FVector Pos = GetActorLocation();
		const float H = GetNpcListHeight();
		if (H != -1.f)
		{
			if (GetEntityClass() == 4) { B.Min.Z = B.Max.Z = Pos.Z; }      // class 4: the bone box Z was [ground, ground + h]
			else { B.Max.Z = FMath::Max(B.Min.Z, B.Max.Z - H); }          // classes 1/3: drop the rise
		}
		B += Mesh->GetSkeletalMeshAsset()->GetBounds().GetBox().TransformBy(Mesh->GetComponentTransform());
		Out = B;
		return true;
	}
	if (Mesh) { B += Mesh->Bounds.GetBox(); }   // видимые габариты модели (поза кадра)
	Out = AsdaScaleBoxXY(B, ZoneS);   // ★ 04.11.2026: boards ×0.5 — capsule and ring follow (see above)
	return true;
}

// ★★★ 29.10.2026 СТАТИЧНЫЕ NPC (тип-функции 1/3/5: доски объявлений, гробницы, камни, деревья) — ВЫСОТА ПЛАШКИ. [ПОРТ]
//   ОРИГИНАЛ: плашки имени у них НЕТ. Контролы 1660/1661 (`uiNameplateRoot` +0xA94) создаёт только ветка класса 0 загрузчика
//     `Entity_StreamLoadAndInitFSM@0xF99380` (подсостояние 0xC, entity_char.c:59494; запись 0xF99FBB); ветки 1/3/5
//     (59521-59756), 2 и 4 — нет; конструктор `sub_F93D80` обнуляет поле (0xF93FE6), и `npcNameplateUpdateLoop@0xF93000`
//     сущность без него пропускает (`if (*(node+2708))`). Второй метод: скан exe 13 344 768 б — запись в [reg+0xA94] у NPC
//     только 0xF93FE6/0xF99FBB/0xF9A06A, `push 1660` только 0x991D01 (UI), 0xA1DC26 (игрок), 0xF99F85 (NPC класс 0).
//     Метки «!» у типа 5 у оригинала тоже нет (`asda.Board.FixOverheadMark`, 28.10).
//   Если бы плашка была, её точкой стал бы `helper_getSumF(+424)` = центрY + полувысотаY. У классов 1/3/5 загрузчик
//     ПЕРЕЗАПИСЫВАЕТ +424 (entity_char.c:59629-59637): полуразмер = короб меша `.jbm` (`modelObjectPtr+332`), а центр =
//     ПОЗИЦИЯ NPC (`sub_93AC87` → `sub_F081A0` пишет this[3..5]) ⇒ точка = земля + ПОЛОВИНА высоты модели (середина доски).
//   ПОРТ ДО ПРАВКИ: плашка на верху капсулы клика + NameplateTopOffset. С 22.10 капсула статичных NPC = короб выделения
//     (кости b_bone01/02, ВЕРХ поднят на npc_list +0x68 — так короб клика `+189` строит оригинал, entity_char.c:59602-59603).
//     У доски `mdel_raidboard-a_00` это поле = 1000 ⇒ плашка и «!» на ~1150 ед. над землёй при высоте доски ~400 — за кадром.
//   ПРАВИЛО ПОРТА: плашка — на ВЕРХ МОДЕЛИ (`MeshTopZ`, габариты ассета в позе привязки), прибавка K = 0, как у NPC класса 0
//     в оригинале (имя поднимает экранный сдвиг плашки −50, `npcNameplateUpdateLoop`). Метка «!» считается от плашки
//     (`ApplyQuestMarkHeight`: над именем по экрану, не ниже верха модели + просвет) — уходит вниз вместе с ней.
//   Ключ `asda.Npc.StaticPlateOnModel` 0 — прежнее поведение (возврат 0 ⇒ верх капсулы + NameplateTopOffset).
bool AAsdaNpcActor::IsStaticPlateType() const
{
	auto IsStatic = [](int32 T) { return T == 1 || T == 3 || T == 5; };
	if (IsStatic(TypeFunc)) { return true; }
	if (NpcId > 0)
	{
		if (const FAsdaNpcQuestNpcRow* R = AsdaDt::RowByInt<FAsdaNpcQuestNpcRow>(kNpcQuestNpcPath, NpcId, TEXT("IsStaticPlateType")))
		{
			return IsStatic(R->NpcType);
		}
	}
	return false;
}

// Высота плашки статичного NPC: верх модели (ключ 1) или 0 — «данных нет, верх капсулы» (ключ 0, модели нет).
static float AsdaStaticPlateHeight(const AAsdaNpcActor* A)
{
	if (CVarNpcStaticPlateOnModel.GetValueOnGameThread() == 0) { return 0.f; }
	const float Top = A->MeshTopZ();
	return (Top > 1.f) ? Top : 0.f;
}

float AAsdaNpcActor::ResolvePlateHeight() const
{
	if (PlateHeightOverride > 0.f) { return PlateHeightOverride; }
	if (TypeFunc == 2) { return 1500.f; }

	const bool bStaticType = (TypeFunc == 1 || TypeFunc == 3 || TypeFunc == 5);
	if (bStaticType && !bPlateHeightForStaticTypes) { return AsdaStaticPlateHeight(this); }

	// ★ Аудит №4 (B2/A30): через общую точку `AsdaDt` — ту же таблицу читает и подсистема маркеров
	//   квестов, и до сведения она грузилась ДВАЖДЫ. Ключ строки = NpcId (1-based, как в npc_list.BIN).
	//   ⚠ Подсистему звать нельзя: высота плашки считается в `OnConstruction` запечённого NPC.
	if (NpcId > 0)
	{
		if (const FAsdaNpcQuestNpcRow* R = AsdaDt::RowByInt<FAsdaNpcQuestNpcRow>(
				kNpcQuestNpcPath, NpcId, TEXT("ResolvePlateHeight")))
		{
			if (R->NpcType == 2) { return 1500.f; }
			if ((R->NpcType == 1 || R->NpcType == 3 || R->NpcType == 5) && !bPlateHeightForStaticTypes)
			{
				return AsdaStaticPlateHeight(this);   // ★ 29.10.2026: верх модели (шапка выше)
			}
			return (R->PlateHeight == -1) ? 150.f : static_cast<float>(R->PlateHeight);
		}
	}
	// Строки нет (NpcId вне таблицы) — эталонный дефолт heightParam == −1 (статичный — верх модели, см. шапку).
	return bStaticType ? AsdaStaticPlateHeight(this) : 150.f;
}

// Проставить плашке фиксированную эталонную высоту. 0 (эталонных данных нет) — оставляем прежний
//   расчёт по капсуле/габаритам, чтобы плашка не легла на землю.
void AAsdaNpcActor::ApplyNameplateHeight()
{
	if (!Nameplate) { return; }
	// Правило «высота из данных, иначе расчёт по капсуле» одно на порт — оно у самой плашки
	//   (`UAsdaNameplateComponent::ApplyHeightFromData`, дедуп Д13). Своей копии здесь нет.
	Nameplate->ApplyHeightFromData(ResolvePlateHeight());
}

// ★ ВЕРХ ГАБАРИТОВ МОДЕЛИ над origin актора (мировые единицы). Берутся габариты АССЕТА
//   (референс-поза), а НЕ `Mesh->Bounds`: покадровые габариты дышат вместе с idle-клипом, и высота
//   метки от них дёргалась бы каждый кадр. Корень актора = сам `Mesh`, поэтому вычитать позицию
//   актора не нужно — асcетные габариты уже в локальном пространстве модели.
float AAsdaNpcActor::MeshTopZ() const
{
	if (!Mesh) { return 0.f; }
	const float S = FMath::Max(FMath::Abs(Mesh->GetComponentScale().Z), 1e-4f);
	if (const USkeletalMesh* SM = Mesh->GetSkeletalMeshAsset())
	{
		const FBoxSphereBounds B = SM->GetBounds();
		return (B.Origin.Z + B.BoxExtent.Z) * S;
	}
	const FBoxSphereBounds B = Mesh->Bounds;                      // меша нет — фолбэк на компонент
	return (B.Origin.Z + B.BoxExtent.Z) - GetActorLocation().Z;
}

// Сколько ПИКСЕЛЕЙ вьюпорта приходится на один мировой юнит ВЫСОТЫ в точке P (перспектива ⇒
//   величина зависит от удаления камеры). Меряется двумя проекциями — точно и без знания FOV/аспекта.
//   false = точка за камерой либо вырожденная разность (проекции доверять нельзя).
static bool AsdaScreenPxPerWorldUnitZ(APlayerController* PC, const FVector& P, float& OutPxPerUnit)
{
	OutPxPerUnit = 0.f;
	if (!PC) { return false; }
	static constexpr float kProbe = 100.f;               // база замера, мировые единицы
	FVector2D S0, S1;
	if (!PC->ProjectWorldLocationToScreen(P, S0, /*bPlayerViewportRelative=*/true)) { return false; }
	if (!PC->ProjectWorldLocationToScreen(P + FVector(0.f, 0.f, kProbe), S1, /*bPlayerViewportRelative=*/true)) { return false; }
	const float D = S0.Y - S1.Y;                          // вверх по миру = МЕНЬШЕ экранный Y
	if (D <= KINDA_SMALL_NUMBER) { return false; }
	OutPxPerUnit = D / kProbe;
	return true;
}

// ★★★ ВЫСОТА КВЕСТОВОЙ МЕТКИ.
//   ЭТАЛОН (bQuestMarkEtalonHeight = true): entity_char.c case 9/0xA/0xB ставит метку в
//     Y = helper_getSumF(poseMatrix) + 60, где helper_getSumF@0x97D6D0 = posY + высота модели,
//     т.е. «верх габаритов NPC + 60 игровых единиц» (мировые единицы порта = игровые).
//
//   ПО УМОЛЧАНИЮ (просьба владельца: метка должна стоять НАД ИМЕНЕМ). Эталонную формулу применить
//   дословно нельзя, и вот почему: в оригинале плашка имени — тоже элемент экрана
//   (`npcNameplateUpdateLoop`: мировая точка проецируется, затем `screenY − 50`), а метка — меш в
//   мире. Значит «над именем» в МИРОВЫХ единицах не выражается вообще: одна и та же прибавка даёт
//   разный зазор на разном удалении камеры (замер: при длине штанги 170…1270 один экранный пункт
//   стоит 0.18…1.3 мировых юнита — разброс в 7 раз). Прежняя реализация («якорь плашки + 5»)
//   промахивалась именно здесь: она целилась в ЯКОРЬ, а строка имени рисуется на 34 пункта ВЫШЕ
//   якоря, поэтому знак оказывался НИЖЕ имени (и вообще ниже макушки модели: замер по alpen — якорь
//   плашки 150.0 при верхе габаритов 194.2).
//
//   ПРАВИЛО ПОРТА (одно, здесь; второй копии нет):
//     1) ЭКРАННОЕ. Низ метки ставится на `QuestMarkGapOverNamePx` пунктов ВЫШЕ ВЕРХА СТРОКИ ИМЕНИ.
//        Где верх строки — спрашиваем у самой плашки (`GetPlateTopScreenOffset`, общая точка), а
//        перевод «пункты → мировые юниты» делается ЗАМЕРОМ проекции в этой же точке. Отсюда зазор
//        одинаков и у высоких, и у низких NPC, и на любом зуме — считается от ФАКТИЧЕСКОГО верха
//        плашки, а не подбирается константой.
//     2) ПОЛ. Метка — трёхмерный меш и честно закрывается геометрией: ниже макушки модели её
//        опускать нельзя, иначе знак утонет в голове. Поэтому берётся `max(экранное правило,
//        верх габаритов + QuestMarkMeshClearance)`. У обычного NPC (макушка 194) на среднем зуме
//        побеждает пол, у гигантов (`Moving Crystal`, якорь 800) — тоже пол, на дальнем зуме —
//        экранное правило. Обе ветки считаются от ДАННЫХ модели, поэтому «плавания» между
//        моделями нет.
//     ★ Ноль метки-меша = её НИЗ (замер габаритов ассета: origin.z − extent.z = 0.00 у «!»,
//       −12.02 у «?»), поэтому позиция компонента и есть «низ знака» — компенсировать нечего.
//   Считается ПОКАДРОВО (см. Tick): экранное правило зависит от камеры.
void AAsdaNpcActor::ApplyQuestMarkHeight()
{
	if (!QuestMark) { return; }
	float ScaleZ = FMath::Abs(GetActorScale3D().Z);
	if (Mesh) { ScaleZ = FMath::Abs(Mesh->GetComponentScale().Z); }
	ScaleZ = FMath::Max(ScaleZ, 1e-4f);

	const bool bEtalon = bQuestMarkEtalonHeight || (GQuestMarkUseEtalonHeight != 0);
	if (!bEtalon && Nameplate)
	{
		// ⚠ `ApplyNameplateHeight()` здесь НЕ зовём: функция работает ПОКАДРОВО, а высота плашки —
		//   константа из данных, её ставят вызывающие (OnConstruction / BeginPlay / SetQuestMark).
		// Плашка и метка висят на ОДНОМ родителе (Mesh) → мировая высота якоря над origin актора.
		const float PlateZ = Nameplate->GetRelativeLocation().Z * ScaleZ;

		// (1) ЭКРАННОЕ ПРАВИЛО. Фолбэк (проекции нет) — прежняя мировая прибавка к якорю.
		float Rise = (GQuestMarkOffsetOverNameplate >= 0.f)
			? GQuestMarkOffsetOverNameplate : QuestMarkOffsetOverNameplate;
		UWorld* W = GetWorld();
		APlayerController* PC = (W && W->IsGameWorld()) ? UGameplayStatics::GetPlayerController(W, 0) : nullptr;
		float PxPerUnit = 0.f;
		const FVector Anchor = Nameplate->GetComponentLocation();
		if (AsdaScreenPxPerWorldUnitZ(PC, Anchor, PxPerUnit))
		{
			const float Gap = (GQuestMarkGapOverNamePx >= 0.f)
				? GQuestMarkGapOverNamePx : QuestMarkGapOverNamePx;
			// Пункты плашки — экранные (после деления на DPI), замер PxPerUnit — в пикселях вьюпорта.
			const float PxUp = (Nameplate->GetPlateTopScreenOffset() + Gap)
				* UWidgetLayoutLibrary::GetViewportScale(W);
			Rise = PxUp / PxPerUnit;
			// ★ УТОЧНЕНИЕ СЕКУЩЕЙ. Первый замер взят по пробе 100 юнитов, а перспектива нелинейна:
			//   на первом прогоне зазор гулял 8.1…10.7 пункта вместо ровно 10. Меряем ФАКТИЧЕСКИЙ
			//   экранный сдвиг на найденную высоту и правим пропорцией — зазор садится на заданный.
			FVector2D S0, S1;
			if (Rise > KINDA_SMALL_NUMBER
				&& PC->ProjectWorldLocationToScreen(Anchor, S0, /*bPlayerViewportRelative=*/true)
				&& PC->ProjectWorldLocationToScreen(Anchor + FVector(0.f, 0.f, Rise), S1, true))
			{
				const float ActualPx = S0.Y - S1.Y;
				if (ActualPx > KINDA_SMALL_NUMBER) { Rise *= PxUp / ActualPx; }
			}
			Rise = FMath::Max(Rise, 0.f);   // проекция за краем кадра может дать отрицательное — не опускаем
		}
		float MarkZ = PlateZ + Rise;

		// (2) ПОЛ: не ниже макушки модели плюс просвет.
		const bool bFloor = (GQuestMarkClearMesh >= 0) ? (GQuestMarkClearMesh != 0) : bQuestMarkClearMesh;
		if (bFloor)
		{
			const float Clear = (GQuestMarkMeshClearance >= 0.f)
				? GQuestMarkMeshClearance : QuestMarkMeshClearance;
			MarkZ = FMath::Max(MarkZ, MeshTopZ() + Clear);
		}

		QuestMark->SetRelativeLocation(FVector(0.f, 0.f, MarkZ / ScaleZ));
		return;
	}

	float TopWorldZ = MeshTopZ();
	if (TopWorldZ <= 1.f) { TopWorldZ = 90.f; }     // фолбэк: половина капсулы пикинга
	QuestMark->SetRelativeLocation(FVector(0.f, 0.f, (TopWorldZ + 60.f) / ScaleZ));
}

void AAsdaNpcActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	AsdaShadow::ApplyCapsule(Mesh);   // ★ Сет B: повтор — значение ключа из ini может прийти позже конструктора CDO
	AsdaMobFx::ApplyDepth(Mesh);   // ★ 01.11.2026 (mob_fx_audit): Custom Depth для подсеток-эффектов `__mobfx` (материалы — в ассете)
	// ★ Имя плашки — ИЗ ТАБЛИЦЫ DT_NpcNames по NpcId (локализованное, RU). Запечённое NpcName —
	//   только фолбэк: оно взято из АНГЛИЙСКОГО билда, и раньше оставалось у NPC, которых не
	//   «удочерил» сетевой пакет 4067 (отсюда были английские имена части NPC).
	if (PickHitbox)
	{
		// Размер — по габаритам МЕША (не актора: иначе капсула измеряет сама себя и вырождается);
		//   вся арифметика — в общей точке `AsdaPick::FitToMeshBounds` (дедуп Д9).
		AsdaPick::FitToMeshBounds(PickHitbox, Mesh, GetActorScale3D());
		ApplyStaticPickBox();   // ★ 22.10.2026: статичные объекты — капсула по коробу выделения
	}
	if (Nameplate)
	{
		// ★ 10.10.2026 (en_completeness): актор мира редактора — имя русское даже во время PIE (поле сохраняется в карту).
		const AsdaLangRows::FEditorWorldScope EditorWorldGuard(this);
		Nameplate->DisplayName = AsdaEntityText::NpcDisplayName(NpcId, NpcName);   // ★ волна 3 п.14: таблица, иначе запечённое
		ApplyNameplateHeight();        // ★ эталонная фиксированная высота (npc_list +0x68), см. ResolvePlateHeight
		Nameplate->RefreshNameplate();
	}
	// ★ Метка привязана к плашке имени — пересчитываем после подгонки капсулы/смены меша.
	if (QuestMarkKind != 0) { ApplyQuestMarkHeight(); }
}

// ── ★★★ ПОРТ СМЕНЫ АНИМАЦИОННОГО СОСТОЯНИЯ СУЩНОСТИ `uiPanelSelectTab@0xF947A0` ────────────────
//   (мисномер декомпилятора: аргумент — не «вкладка панели», а НОМЕР СОСТОЯНИЯ сущности мира.)
//
//   ЭТАЛОН (ui_core.c:98500-98640), дословно:
//     if (entity[182] /*тип-функции*/ == 1) return;         // статичный NPC состояний не имеет
//     entity[162] = entity[161];  entity[161] = state;      // prev ← cur, cur ← новое
//     switch (prev) { case 7/8/9: погасить 4 эффект-узла + корневой }   // в порту эффектов NPC нет
//     switch (cur) {
//        case 0: flags &= ~0x1000; CObject_SetField13(anim[0], 0);              // idle
//        case 1: if (тип==0 || тип==4) CObject_SetField13(anim_base + 128, 0);  // ← РАЗГОВОР
//        case 2: if (тип==0 || тип==4) CObject_SetField13(anim_base + 192, 0);
//        case 3: if (тип==0 || тип==4) CObject_SetField13(anim_base + 256, 0);
//        case 4: flags &= ~0x700; CObject_SetField13(anim_base +  64, 0);
//        case 5: flags &= ~0x700; CObject_SetField13(anim_base + 128, 0);
//        case 6: flags &= ~0x700; CObject_SetField13(anim_base +   0, 0);
//        case 7: flags &= ~0x700; CObject_SetField13(anim_base + 192, a3);
//        case 8: flags &= ~0x700; CObject_SetField13(anim_base + 256, a3);
//        case 9: flags &= ~0x700; CObject_SetField13(anim_base + 320, a3);
//     }
//   ЗАПИСЬ КЛИПА — 64 байта (смещения идут ровно 0/64/128/192/256/320), значит НОМЕР КЛИПА =
//   смещение / 64. Отсюда таблица `состояние → клип`, ниже она выписана явно. `CObject_SetField13(…, 0)`
//   = «поставить время клипа в 0», то есть ПЕРЕЗАПУСТИТЬ его с первого кадра.
//   ⚠ Флаги 0x1000/0x100/0x200/0x400 сущности — это ветки эффектов и слотов, которых у NPC порта нет
//     (метка квеста живёт своим компонентом); поэтому из switch-а воспроизводится ИМЕННО клип.
static constexpr int32 kNpcAnimStateToClip[10] = { 0, 2, 3, 4, 1, 2, 0, 3, 4, 5 };

void AAsdaNpcActor::SetEntityAnimState(int32 State)
{
	// `if (entity[182] != 1) { … }` — у статичного типа 1 функция эталона пустая.
	if (TypeFunc == 1) { return; }
	if (State < 0 || State >= UE_ARRAY_COUNT(kNpcAnimStateToClip)) { return; }
	// Состояния 1/2/3 эталон включает ТОЛЬКО у типов 0 и 4 (`if (!entity[182] || entity[182] == 4)`).
	if (State >= 1 && State <= 3 && TypeFunc != 0 && TypeFunc != 4) { return; }

	EntityAnimState = State;
	if (!Mesh) { return; }

	const int32 Clip = kNpcAnimStateToClip[State];
	// Клип 0 у актора уже лежит в `IdleAnim` (запечён импортёром) — берём его, чтобы не грузить дубль.
	UAnimSequence* A = nullptr;
	if (Clip == 0)
	{
		A = IdleAnim;
	}
	if (!A)
	{
		const USkeletalMesh* SM = Mesh->GetSkeletalMeshAsset();
		if (!SM) { return; }
		static const FString kNpcSkelRoot(AsdaAssetPaths::kSkelRootNpc);   // ★ волна 2 п. 7: корень — реестр путей
		A = UAsdaModelAnimSubsystem::LoadClipChecked(                       // Д2: ресурс через менеджер
			UAsdaModelAnimSubsystem::BuildSkelAnimPath(kNpcSkelRoot, SM->GetName(), Clip));
	}
	if (!A)
	{
		UE_LOG(LogAsdaNpcAct, Warning,
			TEXT("[NPC-анимация] npcId=%d состояние %d → клип %d: клипа нет, состояние не сменено"),
			NpcId, State, Clip);
		return;
	}
	// Эталон перезапускает клип с нулевого кадра. Луп: клип 0 (idle) эталон крутит постоянно
	//   (`effectListUpdateType14` зовёт `objMeshAnimUpdate` каждый кадр), а состояния 1…3 — это
	//   ЖЕСТ, он играется один раз и замирает на последнем кадре (SingleNode без лупа).
	// ★★★ [ОРИГИНАЛ] 04.11.2026 (баг владельца «анимация Ian ломается при начале разговора», `Reports/npc_talk_anim/REPORT.md`):
	//   смена состояния у оригинала — СО СМЕШИВАНИЕМ, не рывком. `uiPanelSelectTab` (ui_core.c:98518-98532) при смене состояния
	//   запоминает слот, показанный последним (`entity[163] = entity[112]`), и его время (`entity[164]`), взводит `entity[168] = 1`;
	//   тики состояния 1 (`objAnimUpdateTab2Blend@0xF980F0`) и 0 (`objAnimUpdateFrame`, model_anim.c:1025-1046) пока
	//   `время_нового / 60 < 1` зовут `animBlendBetweenSlots@0x114A2C0` (lerp/slerp каждой кости, вес прежней позы 1 − t/60, мс).
	//   Состояние 2 бленд сбрасывает (case 2: `entity[168] = 0`), 3 его не считает — там клипы стыкуются в данных.
	//   Порт: общий инстанс со смешиванием (`AsdaAnimSmooth`, как у игроков/мобов), длительность — ключ `asda.Anim.BlendSec`
	//   (0.15 по слову владельца «смягчить переходы»; оригинал 0.06) на ВСЕХ сменах [ИСПРАВЛЕНИЕ: и на 2→3/3→0 — у части моделей
	//   стыки в данных рвутся: karas 57 ед., bumibumi 27]. `asda.Anim.BlendSec 0` — прежняя смена рывком.
	AsdaAnimSmooth::PlayClip(Mesh, A, /*bLoop=*/ Clip == 0, AsdaAnimSmooth::BlendSec());
	Mesh->SetPosition(0.f, /*bFireNotifies=*/ false);
	// Тик нужен, пока крутится цепочка состояний 1→2→3→0 (или пока висит обводка метки).
	UpdateTickNeed();
	UE_LOG(LogAsdaNpcAct, Log, TEXT("[NPC-анимация] npcId=%d состояние %d → клип anim_%02d «%s»"),
		NpcId, State, Clip, *A->GetName());
}

void AAsdaNpcActor::BeginPlay()
{
	Super::BeginPlay();
	// ★ 04.11.2026 (npc_talk_anim): инстанс со смешиванием клипов (см. SetEntityAnimState). Ставится в игре, а не в конструкторе:
	//   запечённые акторы уровней сохранены в одиночном режиме, превью редактора не меняется. Компонент уже зарегистрирован —
	//   `Install` пересоздаёт инстанс (клип из AnimationData подхватывается), до показа актора, т.е. без видимого скачка.
	if (Mesh && GetWorld() && GetWorld()->IsGameWorld() && !AsdaAnimSmooth::IsInstalled(Mesh)) { AsdaAnimSmooth::Install(Mesh); }
	AsdaMobFx::ApplyDepth(Mesh);   // ★ 01.11.2026 (mob_fx_audit): в игре OnConstruction не перезапускается
	AsdaNpcMissing::ApplyModelFallback(this);   // ★ 09.11.2026: NPC без модели / из списка — модель стражника (Public/Npc/AsdaNpcMissing.h)
	// ★ Высота плашки — эталонная константа из npc_list (не габариты). Повтор после OnConstruction:
	//   в игре плашке высоту переставляет FillNameplate → ApplyHeight, и флаг должен быть уже выставлен.
	ApplyNameplateHeight();
	if (QuestMarkKind != 0) { ApplyQuestMarkHeight(); }
	ApplyStaticPickBox();   // ★ 22.10.2026: в собранной игре OnConstruction расставленных акторов не перезапускается
	// ★ Запечённый актор (превью редактора / шаблон клона) — в ИГРЕ скрыт И БЕЗ КОЛЛИЗИИ: невидимые
	//   капсулы-хитбоксы блокировали трейс земли/пикинг («невидимые бугры» — юзер 2026-07-29).
	if (!bNetSpawned) { SetActorHiddenInGame(true); SetActorEnableCollision(false); return; }
	// Отключённый NPC (.npc: отрицательный тайл — сервер запись пропускает): в игре скрыт.
	if (bDisabled) { SetActorHiddenInGame(true); SetActorEnableCollision(false); return; }
	// Эталон: idle = просто проигрывание клипа _0 лупом (отдельного «дыхания»/поворота нет).
	//   ★ 04.11.2026: через общий инстанс (штатный PlayAnimation вернул бы одиночный режим и снял смешивание); первый клип — без бленда.
	if (IdleAnim && Mesh) { AsdaAnimSmooth::PlayClip(Mesh, IdleAnim, /*bLoop=*/true, /*бленд*/0.f); }
}

// ── ★ ДИАГНОСТИКА ВЫСОТЫ КВЕСТОВОЙ МЕТКИ ────────────────────────────────────────────────────────
//   Печатает по каждому NPC с меткой ФАКТИЧЕСКИЕ числа: мировые высоты (якорь плашки, верх модели,
//   низ метки) И экранные (верх строки имени, низ метки, ЗАЗОР В ПУНКТАХ). Зазор — это ровно то,
//   что просил владелец, поэтому доказывать «метка над именем» надо им, а не картинкой на глаз.
//   Аргумент — необязательная подстрока имени NPC (фильтр).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestMarkHeightDump(
	TEXT("Asda.Quest.MarkHeightDump"),
	TEXT("Asda.Quest.MarkHeightDump [подстрока имени] — высоты квестовых меток: мир, экран, зазор над именем."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (!World) { return; }
			APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
			const float DPI = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(World), 0.01f);
			const FString Filter = (Args.Num() >= 1) ? Args[0] : FString();
			// ★ Начало координат ИГРОВОГО ВЬЮПОРТА в абсолютных Slate-координатах: без него нельзя
			//   сравнить проекцию (пиксели вьюпорта) с геометрией виджета (абсолютные единицы).
			FVector2D VpAbs(0.f, 0.f), VpSize(0.f, 0.f);
			if (GEngine && GEngine->GameViewport)
			{
				if (TSharedPtr<SWidget> VW = GEngine->GameViewport->GetGameViewportWidget())
				{
					VpAbs = VW->GetCachedGeometry().GetAbsolutePosition();
					VpSize = VW->GetCachedGeometry().GetAbsoluteSize();
				}
			}
			UE_LOG(LogAsdaNpcAct, Log,
				TEXT("[метка-высота] DPI=%.3f вьюпорт абс=(%.0f,%.0f) разм=(%.0f,%.0f) | колонки: якорьZ верхМодели меткаZ | экран: имяY меткаY ЗАЗОР(пункты)"),
				DPI, VpAbs.X, VpAbs.Y, VpSize.X, VpSize.Y);
			int32 N = 0;
			for (TActorIterator<AAsdaNpcActor> It(World); It; ++It)
			{
				AAsdaNpcActor* Npc = *It;
				if (!Npc || Npc->QuestMarkKind == 0 || Npc->IsHidden()) { continue; }
				if (!Filter.IsEmpty() && !Npc->GetName().Contains(Filter) && !Npc->NpcName.Contains(Filter)) { continue; }
				const UAsdaNameplateComponent* NP = Npc->Nameplate;
				const USceneComponent* QM = Npc->QuestMark;
				if (!NP || !QM) { continue; }
				const float BaseZ = Npc->GetActorLocation().Z;
				const float PlateZ = NP->GetComponentLocation().Z - BaseZ;
				const float MarkZ  = QM->GetComponentLocation().Z - BaseZ;
				FString Screen = TEXT("экран: проекции нет");
				if (PC)
				{
					FVector2D SA, SM2;
					if (PC->ProjectWorldLocationToScreen(NP->GetComponentLocation(), SA, true)
						&& PC->ProjectWorldLocationToScreen(QM->GetComponentLocation(), SM2, true))
					{
						const float NameTopY = SA.Y - NP->GetPlateTopScreenOffset() * DPI;
						// ★ И РАСЧЁТ, И ФАКТ: рядом печатается абсолютный прямоугольник строки имени
						//   (как её реально нарисовал Slate) — расхождение сразу видно.
						FVector2D AP, AS;
						const bool bGeom = NP->GetNameAbsRect(AP, AS);
						Screen = FString::Printf(
							TEXT("якорь=(%.0f,%.0f) имяY=%.1f меткаY=%.1f ЗАЗОР=%.1f пункт. | факт имя абс=(%.0f,%.0f) разм=(%.0f,%.0f) метка абс.проекц=(%.0f,%.0f)"),
							SA.X, SA.Y, NameTopY / DPI, SM2.Y / DPI, (NameTopY - SM2.Y) / DPI,
							bGeom ? AP.X : -1.f, bGeom ? AP.Y : -1.f, bGeom ? AS.X : -1.f, bGeom ? AS.Y : -1.f,
							SM2.X, SM2.Y);
					}
				}
				UE_LOG(LogAsdaNpcAct, Log,
					TEXT("[метка-высота] npc %-4d %-24s метка=%d | якорь=%7.1f верхМодели=%7.1f метка=%7.1f | %s"),
					Npc->NpcId, *Npc->NpcName.Left(24), Npc->QuestMarkKind,
					PlateZ, Npc->MeshTopZ(), MarkZ, *Screen);
				++N;
			}
			UE_LOG(LogAsdaNpcAct, Log, TEXT("[метка-высота] NPC с меткой в кадре мира: %d"), N);
		}));
#endif // ASDA_DEV_CONSOLE

// ── ★ ДИАГНОСТИКА: ПОКАЗАТЬ ЗАПЕЧЁННЫХ NPC БЕЗ СЕРВЕРА ──────────────────────────────────────────
//   В игре запечённый NPC скрыт до «удочерения» пакетом 4067 (`BeginPlay`: !bNetSpawned ⇒ скрыт).
//   Сервер локально не поднимается, поэтому на PIE не видно ни моделей, ни плашек, ни меток — и
//   любая проверка «на глаз» дала бы ложное «не работает» (закон `false-negatives-method`).
//   Команда делает ровно то же, что делает удочерение: показывает актора, включает коллизию и
//   idle-клип. Ничего в игровом пути не меняет — только диагностика.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaNpcShowBaked(
	TEXT("Asda.Npc.ShowBaked"),
	TEXT("Asda.Npc.ShowBaked [0|1] — показать/скрыть запечённых NPC карты без сервера (диагностика)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (!World) { return; }
			const bool bShow = (Args.Num() < 1) || (FCString::Atoi(*Args[0]) != 0);
			int32 N = 0;
			for (TActorIterator<AAsdaNpcActor> It(World); It; ++It)
			{
				AAsdaNpcActor* Npc = *It;
				if (!Npc || Npc->bNetSpawned || Npc->bDisabled) { continue; }
				Npc->SetActorHiddenInGame(!bShow);
				Npc->SetActorEnableCollision(bShow);
				if (bShow && Npc->IdleAnim && Npc->Mesh) { AsdaAnimSmooth::PlayClip(Npc->Mesh, Npc->IdleAnim, true, 0.f); }   // ★ 04.11: инстанс со смешиванием
				if (Npc->Nameplate) { Npc->Nameplate->RefreshNameplate(); }
				++N;
			}
			UE_LOG(LogAsdaNpcAct, Log, TEXT("[NPC] запечённых %s: %d"), bShow ? TEXT("показано") : TEXT("скрыто"), N);
		}));
#endif // ASDA_DEV_CONSOLE
