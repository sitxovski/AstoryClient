// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerCharacter_Attack.cpp — ★ 23.10.2026 часть AAsdaPlayerCharacter, вынесенная из AsdaPlayerCharacter.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaPlayerCharacter.plan.json).
//   Эмоции (наблюдение, конец клипа), клипы и комбо атаки, замах по серверным часам, ровный темп, замах соседа от пакетов.
//   #include — те же, что у AsdaPlayerCharacter.cpp, в том же порядке; общее — AsdaPlayerCharacterInternal.h.
#include "World/AsdaPlayerCharacter.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "World/AsdaPlayerController.h"   // ★ ритм ударов задаёт тик анимации (OnSwingCycleCompleted)
#include "World/AsdaNockedArrow.h"        // ★ 05.11.2026: выход из атаки снимает стрелу на тетиве
#include "Misc/App.h"   // FApp::GetDeltaTime — оценка кадра (точка отправки за кадр до дедлайна)
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: серверные часы атаки — темп отрезка замаха до точки отправки
#include "Engine/Texture2D.h"          // ★ постоянный форс мипов у текстур персонажа
#include "Engine/StreamableRenderAsset.h"        // ★ charsel_preload: владение флагом полных мипов
#include "Resources/AsdaTextureSubsystem.h"      // ★ charsel_preload: реестр владения флагом полных мипов
#include "Materials/MaterialInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "Resources/AsdaSoundSubsystem.h"       // ★ Аудит №3 (A9): звук транспорта — только через менеджер
#include "Items/AsdaItemDbRows.h"     // FAsdaComboTimeRow — тайминги ударов замаха (combo_time)
#include "Engine/DataTable.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCapsuleShadow.h"   // ★ Сет B: капсульная тень тела — одна точка на порт (игрок/моб/NPC)
#include "World/AsdaCoord.h"                    // ★ Аудит №3 (B1/B2/B9): единый перевод координат игра↔UE
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaFadeComponent.h"      // ★ режим «душа»: снять спавн-фейд (эталон его у души не умножает)
#include "Components/MeshComponent.h"     // ★ режим «душа»: подмена слотов материалов тела и частей
#include "Components/CapsuleComponent.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Characters/AsdaCharAssembly.h"   // кости/трансформы оружия «в руке ↔ за спиной»
#include "World/AsdaWorldSubsystem.h"   // LocalPlayerActor — «свой» персонаж (звук 2D против 3D)
#include "World/AsdaAnimThrottle.h"     // ★ 30.10.2026: часы боевого клипа чужого игрока — без урезания на время замаха
#include "Engine/GameInstance.h"         // ★ В5fix: GetGameInstance()->GetSubsystem (сборка без unity)
#include "AsdaEffectActor.h"            // шлейф езды = AAsdaEffectActor (attach-режим 6)
#include "AsdaFxNames.h"               // ★ дедуп №2 В1-C (F6): теги эффектов персонажа + общее гашение по тегу
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10): пути мастеров и имена параметров
#include "World/AsdaWalkGrid.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"   // поза трупа: перемотка клипа death01 в последний кадр
#include "Engine/PostProcessVolume.h"                  // ч/б сцены на смерти: unbound-объём с высшим приоритетом
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"                 // ★ 10.10.2026 (копание): меш инструмента на `8_a1`
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "World/AsdaFishingSubsystem.h"      // ★ В5: конец клипа своего действия рыбалки → автомат; Fish_data (модель рыбы)
#include "TimerManager.h"                  // ★ В5: таймер конца клипа действия рыбалки
#include "World/AsdaCombatTrace.h"         // ★ 18.10.2026: журнал боя — замах, кадры удара, стойка, смерть персонажа
#include "Npc/AsdaMobActor.h"              // ★ 18.10.2026: журнал боя — имя и rid цели разворота в замахе
#include "World/AsdaBlendAnimInstance.h"   // ★ 22.10.2026: плавность — смешивание клипов, доворот модели, хвост сетевых поправок
#include "Misc/ScopeExit.h"                // ★ 22.10.2026: доворот/хвост на любом выходе из Tick
#include "World/AsdaAttackExit.h"          // ★ 01.11.2026: 4024/400 по концу удержания цикла
#include "AsdaPlayerCharacterInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaPlayerCharacter*.cpp (LogAsdaPlayer, чистые хелперы)


// ★ Сторож эмоции: ловит подмену клипа на ходу. Только журнал, поведения не меняет.
void AAsdaPlayerCharacter::TickEmotionWatch()
{
	if (GetEmotionState() < 0 || !GetWorld() || !EmotionClipAsset.IsValid()) { return; }   // ★ В3 S7: из единого поля
	if (!BaseSkel || !BaseSkel->GetSingleNodeInstance()) { return; }
	const UAnimSequence* Now = Cast<UAnimSequence>(BaseSkel->GetSingleNodeInstance()->GetAnimationAsset());
	if (Now == EmotionClipAsset.Get()) { return; }
	const float Elapsed = (float)(GetWorld()->GetTimeSeconds() - EmotionStartedS);
	UE_LOG(LogAsdaPlayer, Warning,
		TEXT("[эмоция] КЛИП ПОДМЕНЁН на %.2f с из %.2f: было '%s', стало '%s' (состояние %d, AnimState=%d, замах=%d, стойка=%d)"),
		Elapsed, (float)(EmotionUntilS - EmotionStartedS),
		*GetNameSafe(EmotionClipAsset.Get()), *GetNameSafe(Now),
		GetEmotionState(), (int32)AnimState, IsAttackAnimActive() ? 1 : 0, bCombatStance ? 1 : 0);
	EmotionClipAsset = nullptr;   // сообщаем один раз на эмоцию
}

void AAsdaPlayerCharacter::StopEmotionClip()
{
	// ★ 30.10.2026: позы 47/51 перебиваются той же безусловной записью `+47551` (ход 1, покой 0, сидение 7, смерть 6, 4025 0/400/300…),
	//   что и эмоция — все такие места уже зовут эту точку.
	if (const int32 Cp = GetCampPoseState()) { LeaveCampPose(Cp, TEXT("смена состояния")); return; }
	if (!AsdaActionState::IsEmote((int32)BusyAction)) { return; }
	SetBusyAction(EAsdaActionState::Idle, TEXT("эмоция снята"));   // ★ В3 S7: таймер и время клипа снимает единая запись
	UpdateWeaponMotionHidden();   // ★ эмоция снята — оружие возвращается в руку (если зона позволяет)
}

// Конец прохода клипа эмоции = эталонный возврат в состояние 0 (или в текущее, если уже бежим/сидим).
void AAsdaPlayerCharacter::OnEmotionClipEnd()
{
	// ★ В3 S7: таймер живёт только при стоящей эмоции (любая смена состояния его снимает) — проверка на всякий случай.
	if (!AsdaActionState::IsEmote((int32)BusyAction)) { return; }
	SetBusyAction(EAsdaActionState::Idle, TEXT("конец клипа эмоции"));
	UpdateWeaponMotionHidden();   // ★ эмоция доиграла — оружие возвращается в руку
	if (IsAttackAnimActive()) { return; }   // замах успел перебить — его таймер вернёт клип сам
	switch (AnimState)
	{
	case EAsdaPlayerState::Move: PlayWorldClip(MoveClip(), true); break;
	// Сидя — возвращаемся В ЦИКЛ «сидит» (кадр 1500/2000 мс), а не в начало клипа: иначе персонаж
	//   после эмоции садился бы заново.
	case EAsdaPlayerState::Sit:  ResumeSitIdle(); break;
	case EAsdaPlayerState::Dead: break;                                  // труп клип не меняет
	default:                     PlayWorldClip(IdleClipIdx(), true); break;
	}
}

float AAsdaPlayerCharacter::AttackClipLen(int32 WeaponSubType)
{
	const FString Path = FString::Printf(TEXT("%sanim_%02d"), *CharBase(Sex), AttackClipIdx(WeaponSubType));
	if (UAnimSequence* Clip = UAsdaModelAnimSubsystem::LoadClipChecked(Path))   // Д2
	{
		return Clip->GetPlayLength();
	}
	return 0.f;
}

// Имя клипа атаки для combo_time: ch4_{m|f}_<группа>_normalattackNN (реверс Char_LoadMotionAnim).
FString AAsdaPlayerCharacter::AttackClipName(int32 W) const
{
	const TCHAR* G = (Sex == 2) ? TEXT("f") : TEXT("m");
	switch (W)
	{
	case 0: case 4: return FString::Printf(TEXT("ch4_%s_01_normalattack01"), G);
	case 1:  return FString::Printf(TEXT("ch4_%s_01_normalattack03"), G);
	case 3:  return FString::Printf(TEXT("ch4_%s_02_normalattack01"), G);
	case 8: case 10: return FString::Printf(TEXT("ch4_%s_03_normalattack01"), G);
	case 9:  return FString::Printf(TEXT("ch4_%s_04_normalattack01"), G);
	case 2:  return FString::Printf(TEXT("ch4_%s_05_normalattack01"), G);
	case 5:  return FString::Printf(TEXT("ch4_%s_06_normalattack01"), G);
	case 11: return FString::Printf(TEXT("ch4_%s_08_normalattack02"), G);
	case 6:  return FString::Printf(TEXT("ch4_%s_09_normalattack01"), G);
	default: return FString::Printf(TEXT("ch4_%s_01_normalattack01"), G);
	}
}

// ★★★ Аудит №3 (A3/Дд2): ЕДИНАЯ ЗАГРУЗКА И ЕДИНЫЙ ИНДЕКС `DT_combo_time` НА ВЕСЬ ПОРТ.
//   Таблица есть в манифесте `UAsdaItemTableSubsystem` (имя `combo_time`) — берём её ТОЛЬКО у
//   менеджера (закон [[resource-subsystems]]): своя `LoadObject` держала вторую копию строк.
//   ★ TWeakObjectPtr, а НЕ сырой static: сырой указатель не является GC-рутом — в собранном билде
//   первый же GC собирает таблицу, а кэш продолжает её отдавать → чтение мусорного RowMap → краш
//   (тот же класс бага, что ронял char-select через DT_hairtable). Weak-ptr после GC станет невалиден
//   и индекс перестроится по свежей таблице.
const TMap<FString, const FAsdaComboTimeRow*>& AAsdaPlayerCharacter::ComboTimeIndex(const UObject* WorldCtx)
{
	static TWeakObjectPtr<UDataTable> ComboCached;                  // сама таблица (владелец — менеджер)
	static TMap<FString, const FAsdaComboTimeRow*> Index;           // имя клипа (нижний регистр) → строка
	if (ComboCached.IsValid()) { return Index; }
	Index.Reset();
	const UGameInstance* GI = WorldCtx ? UGameplayStatics::GetGameInstance(WorldCtx) : nullptr;
	const UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UDataTable* Combo = Items ? Items->GetDataTable(TEXT("combo_time")) : nullptr;
	if (!Combo)
	{
		UE_LOG(LogTemp, Warning, TEXT("[combo_time] таблица недоступна у UAsdaItemTableSubsystem — тайминги комбо по умолчанию"));
		return Index;
	}
	ComboCached = Combo;
	for (const TPair<FName, uint8*>& It : Combo->GetRowMap())
	{
		const FAsdaComboTimeRow* R = reinterpret_cast<const FAsdaComboTimeRow*>(It.Value);
		if (!R || R->ClipName.IsEmpty()) { continue; }
		// ПЕРВАЯ строка обхода выигрывает — как в прежнем линейном поиске `GetComboTiming` (`break`).
		const FString Key = R->ClipName.ToLower();
		if (!Index.Contains(Key)) { Index.Add(Key, R); }
	}
	UE_LOG(LogTemp, Log, TEXT("[combo_time] разобрано %d клипов (кадры × 1000/30 → мс), индекс один на порт"),
		Index.Num());
	return Index;
}

const FAsdaComboTimeRow* AAsdaPlayerCharacter::FindComboTimeRow(const UObject* WorldCtx, const FString& ClipName)
{
	if (ClipName.IsEmpty()) { return nullptr; }
	const TMap<FString, const FAsdaComboTimeRow*>& Index = ComboTimeIndex(WorldCtx);
	const FAsdaComboTimeRow* const* Found = Index.Find(ClipName.ToLower());
	return Found ? *Found : nullptr;
}

AAsdaPlayerCharacter::FAsdaComboTiming AAsdaPlayerCharacter::GetComboTiming(int32 WeaponSubType)
{
	FAsdaComboTiming T;
	// ★ Аудит №3 (A3/Дд2): строка берётся из ЕДИНОГО индекса, линейный обход таблицы снят.
	const FAsdaComboTimeRow* R = FindComboTimeRow(this, AttackClipName(WeaponSubType));
	if (!R) { return T; }
	T.StepCount = FMath::Clamp(R->StepCount, 1, 9);
	// Кадры УРОНА = трек B (эталон GetEntryFloat84 = запись+0x54+4i).
	const float B[9] = { R->FrameTimeB0, R->FrameTimeB1, R->FrameTimeB2, R->FrameTimeB3, R->FrameTimeB4,
	                     R->FrameTimeB5, R->FrameTimeB6, R->FrameTimeB7, R->FrameTimeB8 };
	const float A[9] = { R->FrameTimeA0, R->FrameTimeA1, R->FrameTimeA2, R->FrameTimeA3, R->FrameTimeA4,
	                     R->FrameTimeA5, R->FrameTimeA6, R->FrameTimeA7, R->FrameTimeA8 };
	// ★★★ ПЕРВАЯ ТОЧКА ПРИМЕНЕНИЯ ТЕМПА АТАКИ (вторая — скорость клипа в `PlayWorldClip`).
	//   Эталон умножает на (M+1) ШАГ ВРЕМЕНИ часов замаха, а от этих часов у него зависят момент
	//   урона, звук замаха, шаг комбо и ОТПРАВКА ПАКЕТА АТАКИ. Наши часы — реальное время, поэтому
	//   те же величины ДЕЛЯТСЯ на множитель: быстрее темп — раньше удар и чаще заявки.
	T.TempoMul = FMath::Max(0.1f, AttackTempoMul());
	for (int32 i = 0; i < T.StepCount; ++i)
	{
		const float Fr = (B[i] > 0.f) ? B[i] : A[i];
		T.HitTimesSec.Add(Fr * kFrameToMs * 0.001f / T.TempoMul);   // ★ Аудит №3 (Дд6): одна константа кадр→мс
	}
	// Полная длительность замаха: окно комбо (эталон WindowTime) либо последний конец удара.
	const float LastEnd = A[FMath::Max(0, T.StepCount - 1)];
	const float WinFr = (R->WindowTime > 0.f) ? R->WindowTime : LastEnd;
	// ★ Нижняя граница 0.3 с — по КЛИП-ВРЕМЕНИ (она страхует вырожденную строку таблицы, а не темп),
	//   поэтому делим уже после зажима: иначе на быстром темпе замах не мог бы стать короче 0.3 с.
	T.TotalSec = FMath::Max(0.3f, WinFr * kFrameToMs * 0.001f) / T.TempoMul;
	T.bValid = true;
	return T;
}

// ★★★ 18.10.2026 (ревизия Ф4, ВАЖНО 3; `Reports/combat_sync_2026-10-17/09_REVIEW_F4.md`) [ПОРТ] — ЦИФРЫ ПРОШЛОГО ЗАМАХА НЕ
//   ПЕРЕНОСЯТСЯ НА НОВЫЙ «ВЗВОД». Таймер цифры своего удара хранит цель как «кадр клипа» (номер прохода + позиция) и при смене
//   скорости отрезка переставляется (`UAsdaWorldSubsystem::RetimeOwnHits`). «Взвод» нового замаха (после навыка, смены цели,
//   убийства) тоже меняет скорость, а номер прохода при взводе/выходе прежде не менялся — невыстреленные цифры прошлого замаха
//   уезжали на кадры нового (позже, иногда за закрытие записи ленты — двойной вычет). Теперь номер прохода сдвигается на взводе и
//   выходе из цикла, а таймер помнит номер замаха (`GetAttackSwingGen`): таймеры чужого номера не переставляются — срабатывают в
//   свой исходный момент. 0 = как до 18.10.2026 (номер прохода не сдвигается, переставляются все таймеры).
static ASDA_DEV_CVAR(int32, CVarAsdaOwnHitSwingGuard,
	TEXT("asda.Combat.OwnHitSwingGuard"), 1,
	TEXT("1 (по умолчанию) = таймеры цифр своего замаха переставляются только в пределах своего замаха: «взвод» следующего (после ")
	TEXT("навыка, смены цели, убийства) их не двигает; номер прохода сдвигается на взводе и выходе из цикла. 0 = как до 18.10.2026: ")
	TEXT("невыстреленные цифры прошлого замаха уезжают на кадры нового."),
	ECVF_Default);

bool AAsdaPlayerCharacter::IsOwnHitSwingGuardOn()
{
	return CVarAsdaOwnHitSwingGuard.GetValueOnGameThread() != 0;
}

// ★★ 18.10.2026 (ревизия Ф4, мелочь 1) [ПОРТ]: перелёт таймера конца прохода (таймер срабатывает в кадре, позже расчётного) уходит в
//   позицию клипа (`OnAttackClipEnd`). С Ф2 так было только у соседа с замахом от пакетов; Ф4 распространил на всех (эталонный тик
//   кадра непрерывен — свой клип отставал на перелёт каждый проход). Это исправление, ключ — для отката.
static ASDA_DEV_CVAR(int32, CVarAsdaClipCarryOverAll,
	TEXT("asda.Combat.ClipCarryOverAll"), 1,
	TEXT("1 (по умолчанию) = перелёт таймера конца прохода замаха уходит в позицию клипа у ВСЕХ персонажей (свой, сосед); 0 = только у ")
	TEXT("соседа с замахом от пакетов (как Ф2, до 17.10.2026 Ф4) — свой клип отстаёт на перелёт каждый проход."),
	ECVF_Default);

void AAsdaPlayerCharacter::StopAttackAnim()
{
#if ASDA_COMBAT_TRACE
	const int32 CtLoopWas = AttackLoopClipIdx;   // журнал боя: какой цикл клипа снимается
#endif
	// ★ Снять ЦИКЛ атаки ДО гейта активности: на границе между проходами клипа (until только что истёк)
	//   ранний return оставлял бы живой таймер перемотки — и цикл бил бы по мёртвой/сброшенной цели.
	AttackLoopClipIdx = -1; AttackLoopHoldUntilS = 0.0;
	AttackSyncRate = 0.f;   // ★ 15.10.2026: скорость отрезка по серверным часам живёт только внутри цикла
	bAttackSegEvened = false; AttackSegPlannedS = 0.f;   // ★ волна mobpos_swing: план отрезка живёт только внутри цикла
	ResetPeerPktState();    // ★ Ф2: замах соседа от пакетов живёт только внутри цикла
	ResetHitFrameScan();    // ★ Ф4: отсчёт кадров удара PvP живёт только внутри цикла
	// ★ 18.10.2026 (ВАЖНО 3): выход из цикла кончает замах — таймеры его цифр больше не переставляются (`OwnHitSwingGuard`).
	if (IsOwnHitSwingGuardOn()) { ++AttackPassSerial; }
	++AttackSwingGen;
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(AttackLoopTimer); GetWorld()->GetTimerManager().ClearTimer(AttackSendTimer); }
	// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Bow.NockLifecycle`): выход из атаки снимает стрелу на тетиве — эталон `clearActiveEffectSlots`
	//   из `CCharacter_SetAction` case 0 / `CChar_SetState` (entity_char.c:16589, 19707, 19770). Стрелы в полёте — отдельные акторы, летят дальше.
	if (AsdaNockedArrow::LifecycleOn()) { AsdaNockedArrow::Hide(this, TEXT("выход из атаки (StopAttackAnim)")); }
#if ASDA_COMBAT_TRACE
	if (CtLoopWas >= 0 || IsAttackAnimActive())   // журнал боя: только когда было что снимать (35 вызывающих, многие — «на всякий случай»)
	{
		ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Swing, TEXT("%s: СТОП замаха (StopAttackAnim) — цикл клипа %d снят, клип %s (позиция %.3f с), цель %s, номер замаха → %d → %s"),
			*AsdaCtPcWho(this), CtLoopWas, IsAttackAnimActive() ? TEXT("шёл") : TEXT("уже кончился"), GetAttackClipPositionSec(),
			*AsdaCtPcTarget(AttackFaceTarget.Get()), AttackSwingGen,
			!IsAttackAnimActive() ? TEXT("клип не трогаем")
				: (AnimState == EAsdaPlayerState::Dead ? TEXT("мёртв: стойка, клип смерти не трогаем")
				: (AnimState == EAsdaPlayerState::Move ? TEXT("боевая стойка, клип бега") : TEXT("боевая стойка idle01 (подтип 20), без фиджетов"))));
	}
#endif
	if (!IsAttackAnimActive()) { return; }
	// ★ 1:1 CChar_SetState(pChar, t, 0, 0, 20) (эталон CCharacter_SetActionState case 0xA: при смерти
	//   цели клиент проходит по всем сущностям и у каждой, чья цель = умерший, при state==5 переводит
	//   состояние в 0 с ПОДТИПОМ 20 = боевой покой). Минимальной длительности/защёлки у состояния
	//   атаки в оригинале НЕТ — но и обрыв там наступает не по приходу пакета, а РОВНО когда HP цели
	//   уходит в 0, т.е. на кадре удара (урон применяет сам тик анимации атаки). Вызывающие обязаны
	//   держать этот момент: ранний вызов = «моб умирает, а замах не отрабатывает» (юзер 2026-07-31).
	AttackAnimUntilS = 0.0;
	AttackFaceTarget = nullptr;                 // эталон: выход из атаки сбрасывает targetRenderId (=-1)
	bCombatStance = true;                       // подтип 20 = БОЕВОЙ покой (idle01), не мирный idle02
	CombatStanceSinceS = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;   // ★ эталон: вход в подтип 20 = fAutoActionStartTime
	IdleTimer = 0.f; FidgetRemaining = 0.f;     // в стойке фиджетов нет (эталон: боевой покой статичен)
	if      (AnimState == EAsdaPlayerState::Move) { PlayWorldClip(MoveClip(), true); }
	else if (AnimState != EAsdaPlayerState::Dead) { PlayWorldClip(IdleClipIdx(), true); }
}

bool AAsdaPlayerCharacter::IsAttackAnimActive() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < AttackAnimUntilS;
}

// ★ Состояния 24/14 эталона (применение навыка / каст с полосой) — см. комментарий у SkillFaceUntilS.
bool AAsdaPlayerCharacter::IsSkillFaceActive() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < SkillFaceUntilS;
}

// ★ Эталонное состояние 24 «идёт» — см. комментарий у SkillAnimUntilS в заголовке.
bool AAsdaPlayerCharacter::IsSkillAnimActive() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < SkillAnimUntilS;
}

// ★★★ ЗВУК ЗАМАХА. Эталон `Player_ProcessAttackTick` (entity_char.c:788-812):
//       if ( !pThis[47779] )                                  // field_398 — на этом шаге не играли
//       { if ( loopIdx*clipLen + trackC[step] <= curFrame )   // момент из трека C
//         { pThis[47779] = 1;
//           switch ( weaponType ) { 0→S_c_ssnormal1(12) 1,4→S_c_lsnormal1(15) 2→S_c_spnormal1(18)
//                                   3→S_c_gsnormal1(18) 5→S_c_stnormal1(32) } } }
//   ★ ДЕФЕКТ ДАННЫХ ОРИГИНАЛА (D9): трек C пуст ВО ВСЕХ 308 строках `combo_time.csv` — момент
//   всегда 0, то есть звук приходится на начало прохода клипа. Поэтому играем его там же, а не
//   изобретаем свой кадр: воспроизводим фактическое поведение оригинала, а не намерение.
//   ⚠ Приставка у оружия 0 — `ss`, а у звука ПОПАДАНИЯ того же оружия — `s`. Таблицы разные.
//   Дальнобойное сюда не попадает: у лука/арбалета/баллисты звук выстрела едет со снарядом.
void AAsdaPlayerCharacter::PlaySwingSound(int32 WeaponSubType)
{
	const TCHAR* Nm = nullptr;
	switch (WeaponSubType)
	{
	case 0:         Nm = TEXT("S_c_ssnormal1"); break;
	case 1: case 4: Nm = TEXT("S_c_lsnormal1"); break;
	case 2:         Nm = TEXT("S_c_spnormal1"); break;
	case 3:         Nm = TEXT("S_c_gsnormal1"); break;
	case 5:         Nm = TEXT("S_c_stnormal1"); break;
	default: return;                            // эталон: у прочих типов звука замаха нет
	}
	UGameInstance* GI = GetGameInstance();
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound3DAs(FName(Nm), GetActorLocation(), EAsdaSoundClass::Action, this);   // ★ 30.10: замах — «действие»
	}
}

float AAsdaPlayerCharacter::PlayAttackClip(int32 WeaponSubType)
{
	// ★ АВТОВЫСАДКА ПО АТАКЕ (эталон mount_system_re.md: вход в боевое состояние ссаживает с транспорта).
	//   Сюда приходит чужой по принятому сервером удару; свой при живом транспорте сюда не доходит (06.11, гейт в `StartAttackSwing`,
	//   ключ `asda.Mount.ListHoldsModel`): прежнее снятие на отправке оставляло сервер верхом и давало отказ 8.
	if (bMounted) { ClearMount(); }
	// ★ Волна В5: состояние атаки (5) перебивает действие рыбалки 60…67 (`CChar_SetState` пишет новое состояние;
	//   у своего атака закрыта гейтами «идёт рыбалка» и запретом удочки 110707).
	if (IsFishActionActive()) { StopFishAction(TEXT("атака")); }
	// ★ 30.10.2026: и позы 47/51 (`CChar_SetState(…, 5)` переписывает `+47551`).
	if (const int32 Cp = GetCampPoseState()) { LeaveCampPose(Cp, TEXT("атака")); }
	// ★ ItemSubType 10 НЕОДНОЗНАЧЕН: у удочек (ItemVariant 1) эталон подменяет класс на 2 (набор _05),
	//   у буров (Variant 2) — на 10 (набор _03). Разрешённый класс уже лежит в EquippedWeaponSubType.
	const int32 W = (WeaponSubType == 10 && EquippedWeaponSubType != 99) ? EquippedWeaponSubType : WeaponSubType;
	const float Len = PlayWorldClip(AttackClipIdx(W), false);
	if (Len > 0.f)
	{
		PlaySwingSound(W);   // ★ звук замаха — на начало прохода клипа (трек C пуст, см. PlaySwingSound)
		// ★★★ ТЕМП: `Len` — длина клипа в его СОБСТВЕННОМ времени, а клип играет со скоростью
		//   (M+1) (см. PlayWorldClip). Реальная длительность прохода, по которой живут гейт замаха
		//   и таймер конца клипа, во столько же раз короче.
		const float RealLen = Len / FMath::Max(0.1f, AttackTempoMul());
		AttackAnimUntilS = GetWorld()->GetTimeSeconds() + (double)RealLen;   // гейт: движение не перебивает замах
		// ★ Конец прохода клипа ведёт ЕДИНЫЙ таймер OnAttackClipEnd: либо перемотка цикла на WindowTime
		//   (эталон, взвод не переигрывается), либо восстановление клипа состояния (idle/бег).
		GetWorld()->GetTimerManager().SetTimer(AttackLoopTimer, this, &AAsdaPlayerCharacter::OnAttackClipEnd, RealLen, false);
		ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Swing, TEXT("%s: СТАРТ клипа атаки anim_%02d «%s» (оружие subType %d → класс %d) длина %.3f с, темп ×%.3f → проход %.3f с, цель %s"),
			*AsdaCtPcWho(this), AttackClipIdx(W), *AttackClipName(W), WeaponSubType, W, Len, AttackClipTempo, RealLen,
			*AsdaCtPcTarget(AttackFaceTarget.Get()));
	}
	else
	{
		ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Refuse, TEXT("%s: клип атаки anim_%02d (оружие subType %d → класс %d) НЕ найден — замаха нет"),
			*AsdaCtPcWho(this), AttackClipIdx(W), WeaponSubType, W);
	}
	return Len;
}

// ★★ Вход в замах по эталонной модели «взвод один раз → цикл» (см. заголовок).
float AAsdaPlayerCharacter::StartAttackSwing(int32 WeaponSubType, float HoldSec)
{
	if (!GetWorld()) { return 0.f; }
	// ★ 06.11.2026 [ОРИГИНАЛ] (`asda.Mount.ListHoldsModel`): свой замах при живом транспорте не стартует — заявка 4026/4201 ушла, сервер
	//   ответит отказом 8 (нотис 2038), всадник остаётся в седле. Чужой сюда приходит по принятому сервером удару — его не трогаем.
	if (bMounted && IsLocalPlayerCharacter() && IsMountHeldByServer())
	{
		ASDA_CTRACE(EAsdaCT::Swing, TEXT("%s: замах не начат — транспорт числится за игроком, ждём ответа сервера (отказ 8)"), *AsdaCtPcWho(this));
		return 0.f;
	}
	const int32 W = (WeaponSubType == 10 && EquippedWeaponSubType != 99) ? EquippedWeaponSubType : WeaponSubType;
	const int32 ClipIdx = AttackClipIdx(W);
	const double Now = GetWorld()->GetTimeSeconds();
	const double NewHold = (HoldSec <= 0.f) ? -1.0 : (Now + (double)HoldSec);
	// ★ Гейт эталона `field_8 != 5` (Recv_CastState, network.c:42051) + прямая запись state=5 у своего
	//   (input.c:1647, entity_char.c:24048 — БЕЗ сброса клипа): цикл этого же клипа уже идёт →
	//   НЕ перезапускаем, только продлеваем удержание. Смена оружия (другой ClipIdx) = новый набор
	//   клипов (SetupModelFromItem) → полный вход со «взводом».
	const bool bLoopAlive = (AttackLoopClipIdx == ClipIdx) &&
		(AttackLoopHoldUntilS < 0.0 || Now < AttackLoopHoldUntilS || IsAttackAnimActive());
	if (bLoopAlive)
	{
		AttackLoopHoldUntilS = NewHold;
		ASDA_CTRACE_V(EAsdaCT::Swing, TEXT("%s: цикл клипа %d уже идёт — замах НЕ перезапускаем, удержание %s (позиция клипа %.3f с)"),
			*AsdaCtPcWho(this), ClipIdx, NewHold < 0.0 ? TEXT("до конца серии") : *FString::Printf(TEXT("+%.2f с"), HoldSec),
			GetAttackClipPositionSec());
		return 0.f;
	}
	// ★ 24.10.2026 (выход из сидения 1:1): новый замах = `CChar_SetState(…, 5/15)` — переписывает 7/8/9 без «встаёт»;
	//   по концу цикла замаха — покой (`OnAttackClipEnd`, AnimState Idle), а не застывший «сидит».
	DropSitForEtalonState(/*bIdleClip*/ false, TEXT("замах 5/15"));
	AttackSyncRate = 0.f;   // ★ 15.10.2026: «взвод» стартует обычным темпом, `BeginSendSegment` ниже задаст скорость отрезка
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(AttackSendTimer); }
	// ★★★ 30.10.2026 [ИСПРАВЛЕНИЕ]: у чужого игрока урезание анимации (URO, волна 29.10) снимается ДО «взвода» — по позиции клипа
	//   строятся отрезки замаха и кадры цифр урона от 4029; под урезанием она отставала на 1–3 кадра (разбор у
	//   `AsdaAnimThrottle::HoldCombatClock`, ключ `asda.Anim.ThrottleCombatHold`). Свой персонаж не урезается — вызов без действия.
	AsdaAnimThrottle::HoldCombatClock(BaseSkel);
	const float Len = PlayAttackClip(WeaponSubType);   // полный клип с кадра 0 — «взвод» (интро [0..W))
	if (Len > 0.f)
	{
		AttackLoopClipIdx = ClipIdx;
		AttackLoopLenS = Len;
		AttackLoopHoldUntilS = NewHold;
		// ★ Точка перемотки = WindowTime из combo_time (эталонное поле char+47749; в сек уже посчитано
		//   GetComboTiming().TotalSec). Кадр анимации в оригинале ставится в W и растёт до конца клипа —
		//   визуально это fmod(W, длина): меч W=60к=длина → 0 (полный повтор, интро у ближников нет),
		//   лук W=24к=0.8с (взвод пропускается), арбалет 18к, баллиста 35к (эталон мотает на B0=35к=W).
		const FAsdaComboTiming CT = GetComboTiming(W);
		// ★★★ ТОЧКА ПЕРЕМОТКИ — ЭТО ПОЗИЦИЯ В КЛИПЕ, А НЕ МОМЕНТ ВРЕМЕНИ, и от темпа она НЕ зависит:
		//   темп меняет лишь то, как быстро анимация до неё доезжает. `CT.TotalSec` отдаётся в
		//   реальном времени (уже поделено на темп), поэтому возвращаем его в клип-время умножением
		//   на `CT.TempoMul` — иначе на ускоренном темпе цикл мотался бы не на окно комбо.
		const float WindowClipS = CT.TotalSec * CT.TempoMul;
		float RewindS = CT.bValid ? FMath::Fmod(WindowClipS, Len) : 0.f;
		if (RewindS < 0.f || Len - RewindS < 0.25f) { RewindS = 0.f; }   // вырожденный хвост → полный повтор
		AttackLoopRewindS = RewindS;
		UE_LOG(LogAsdaPlayer, Log, TEXT("[бой] ВЗВОД (один раз): клип=%d длина=%.2fс, далее цикл с %.2fс (эталон WindowTime)"),
			ClipIdx, Len, RewindS);
		// ★ 15.10.2026: эталонные точки отправки 4028 в проходе и скорость первого отрезка по серверным часам.
		bAttackFirstPass = true;
		AttackSegRates.Reset();   // ★ волна mobpos_swing: новая серия — новый список скоростей отрезков
		ComputeAttackSendPoints(W, Len, RewindS, AttackSendPtsFirst, AttackSendPtsLoop);
		ComputeAttackHitFrames(W, Len, RewindS, AttackHitFirst, AttackHitLoop);   // ★ ревизия СРЕДН-5: кадры ударов для цифр
		ResetHitFrameScan();   // ★ Ф4: «взвод» — отсчёт кадров удара PvP с кадра 0
		bHitScanRanged = WeaponSubType >= 8 && WeaponSubType <= 11;   // ★ Ф4: дальнобойное — выстрел раз в проход
#if ASDA_COMBAT_TRACE
		if ((AsdaCombatTrace::IsOn(1) && (AsdaCombatTrace::IsOn(2) || AsdaCtPcOurs(this))))   // волна 2: сосед вне нашего боя — только уровень 2
		{
			FString CtHits, CtSends;   // журнал боя: кадры удара и точки отправки первого прохода (клип-с)
			for (int32 i = 0; i < AttackHitFirst.Num(); ++i)
			{
				CtHits += FString::Printf(TEXT(" %.3f(ш%d)"), AttackHitFirst[i], AttackHitFirstStep.IsValidIndex(i) ? AttackHitFirstStep[i] : -1);
			}
			for (const float P : AttackSendPtsFirst) { CtSends += FString::Printf(TEXT(" %.3f"), P); }
			ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Swing, TEXT("%s: ВЗВОД клип=%d «%s» combo_time %s шагов=%d окно=%.3f с (реальн., темп ×%.3f), длина %.3f с, перемотка %.3f с, удержание %s; кадры удара [%s ] точки 4028 [%s ]%s"),
				*AsdaCtPcWho(this), ClipIdx, *AttackClipName(W), CT.bValid ? TEXT("есть") : TEXT("НЕТ строки"), CT.StepCount, CT.TotalSec,
				CT.TempoMul, Len, RewindS, NewHold < 0.0 ? TEXT("до конца серии") : *FString::Printf(TEXT("%.2f с"), HoldSec),
				*CtHits, *CtSends, bHitScanRanged ? TEXT(", дальнобойное: выстрел раз в проход") : TEXT(""));
		}
#endif
		// ★ 18.10.2026 (ВАЖНО 3): новый замах — новый номер ДО смены скорости «взвода»: перестановка таймеров (`RetimeOwnHits`)
		//   по сигналу `BeginSendSegment` таймеры прошлого замаха уже не тронет.
		if (IsOwnHitSwingGuardOn()) { ++AttackPassSerial; }
		++AttackSwingGen;
		BeginSendSegment(TEXT("взвод"));
	}
	return Len;
}

void AAsdaPlayerCharacter::HoldAttackLoop(float Sec)
{
	if (AttackLoopClipIdx < 0 || !GetWorld()) { return; }
	AttackLoopHoldUntilS = GetWorld()->GetTimeSeconds() + (double)FMath::Max(Sec, 0.1f);
	ASDA_CTRACE_V(EAsdaCT::Swing, TEXT("%s: удержание цикла клипа %d продлено на %.2f с"), *AsdaCtPcWho(this), AttackLoopClipIdx, FMath::Max(Sec, 0.1f));
}

// ═══ ★★★ ЗАМАХ СВОЕГО ПЕРСОНАЖА ПО СЕРВЕРНЫМ ЧАСАМ (15.10.2026) — разбор у объявления `BeginSendSegment` ═════════

// Границы скорости клипа на отрезке. Быстрее 3× картинка теряет читаемость (самый быстрый законный случай — первый отрезок
//   баллисты: «взвод» 2.0 с против 900 мс сервера = ×2.22); медленнее 0.25× — уже не замах, а стоп-кадр. За границей
//   отправка всё равно ждёт часов (отложенный 4028), то есть быстрее сервера удар не уйдёт ни при какой скорости.
static constexpr float kAsdaSyncRateMin = 0.25f;
static constexpr float kAsdaSyncRateMax = 3.0f;

// ═══ ★★★ РОВНЫЙ ТЕМП СЕРИИ (15.10.2026, волна mobpos_swing, `Reports/anticheat/wave_PORT_mobpos_swing.md` §2) ═══
//   Жалоба владельца: «пампам-пам-пам» вместо «пам-пам-пам» (баллиста). Живой лог 15.10: первый отрезок серии («взвод»)
//   проигрывал ВЕСЬ клип 0.000→2.000 с за ~919 мс (скорость 2.18), следующие — только хвост 1.167→2.000 с за ~926 мс
//   (скорость 0.90). Выстрел (кадр B0 = 35 к = 1.167 с) в первом проходе приходил через 0.54 с после 4026, второй — сразу
//   после перемотки на B0 в 0.92 с: промежуток 0.38 с против 0.93 с дальше — отсюда «пампам».
//   [ОРИГИНАЛ] (`skill_buff.c:1350-1380` баллиста, :1054-1080 лук, :1244-1270 арбалет; `Player_ProcessAttackTick`): темп
//   клипа ОДИН на всю серию — `(M+1)·dt`; первый проход идёт с кадра 0 до конца (интро-натяжение + выстрел на B0), 4028 —
//   на конце прохода, дальше перемотка на B0 и тот же темп. Поэтому у оригинала промежутки между выстрелами равны, а первый
//   4028 уходит позже минимума сервера на длину интро (сервер это принимает: его проверка — «не раньше»).
//   Порт: отрезок ПЕРВОГО прохода не может идти быстрее темпа цикла больше чем на `kAsdaEvenTempoTol` — иначе его скорость
//   приравнивается темпу цикла (самый короткий отрезок прохода цикла / серверная длительность). Точки отправки в клипе и
//   серверная длительность прочих интервалов не меняются; первый 4028 дальнобойного уходит на длину интро позже (как у
//   оригинала). Ближний бой это правило не задевает: его первые отрезки и так не быстрее цикла (самотест: 1.02 против 1.07).
static ASDA_DEV_CVAR(int32, CVarAsdaEvenSwingTempo,
	TEXT("asda.Combat.EvenSwingTempo"), 1,
	TEXT("1 (по умолчанию) = первый проход замаха («взвод») идёт темпом цикла — выстрелы/удары серии через равные промежутки, ")
	TEXT("первый 4028 дальнобойного позже минимума сервера на длину интро (как оригинал); ")
	TEXT("0 = прежний порт: первый отрезок сжимается под серверный минимум (баллиста ×2.2, «пампам-пам»)."),
	ECVF_Default);
static constexpr float kAsdaEvenTempoTol = 0.10f;   // допуск «быстрее цикла» для отрезка первого прохода

float AAsdaPlayerCharacter::SteadyLoopSegmentClip() const
{
	const TArray<float>& P = AttackSendPtsLoop;
	const float L = AttackLoopLenS, R = AttackLoopRewindS;
	if (P.Num() == 0 || L <= 0.f) { return 0.f; }
	constexpr float Eps = 1e-3f;
	float Best = TNumericLimits<float>::Max();
	auto Take = [&Best](float D) { if (D > Eps) { Best = FMath::Min(Best, D); } };
	for (int32 i = 1; i < P.Num(); ++i) { Take(P[i] - P[i - 1]); }
	if (P.Last() >= L - Eps) { Take(P[0] - R); }                    // последняя точка = конец прохода: отрезок от перемотки
	else { Take((L - P.Last()) + (P[0] - R)); }                       // отрезок через конец прохода и перемотку
	return (Best < TNumericLimits<float>::Max()) ? Best : 0.f;
}

bool AAsdaPlayerCharacter::IsServerRhythmOwner() const
{
	return UAsdaAttackRhythmSubsystem::IsEnabled() && AAsdaPlayerController::RhythmFromAnimEnabled()
		&& Cast<AAsdaPlayerController>(GetController()) != nullptr;
}

// Эталон (`Skill_SpawnHitFeedback@0x99B0F0`, skill_buff.c:744-818): порог шага — `петля·W + B[шаг]`, по переходу шаг++;
//   при петле 0 и шаге == N — петля := 1, шаг := 0 (:752-756); 4028 — когда новый шаг чётный (:757). Конец клипа: кадр := W,
//   петля 1, шаг 0, и 4028 — при нечётном N (:802-818). Ветки 8/9/0xA/0xB (дальнобойное) шлют один 4028 на конце прохода
//   (:1066-1074, :1256-1264, :1362-1376). Кадры B — трек урона `combo_time` (B = 0 → A, как `GetComboTiming`).
//   ★ Клипы ближнего боя в порту длиной 2·W (меч 21: 6.0 с при W = 3.0 с): ПЕРВОЕ проигрывание идёт от кадра 0 и вмещает
//   ОБЕ половины — пороги петли 0 (B[шаг]) и, после сброса шага, петли 1 (W + B[шаг]); проходы цикла — [W..конец] с порогами
//   W + B[шаг]. Отсюда: петля 0 — B[нечётные] (+ B[N−1] при нечётном N: сброс шага в 0 = чётный); петля 1 — W + B[нечётные];
//   конец клипа — при нечётном N. Перемотка порта = fmod(W, длина): при W = длине (0) проходы цикла — весь клип.
//   ⚠ [ОРИГИНАЛ] При петле 1 шаг у эталона не сворачивается (сброс только при петле 0): после W + B[N−1] пороги
//   W + B[N…8] = W + 0 уже позади, и до конца клипа каждый тик даёт шаг++ — на чётных шагах эталон по декомпилу шлёт лишние
//   4028 (сервер их молча отбрасывает; живьём не проверялось — оригинал не запускаем). Порт эти «хвостовые» удары не
//   воспроизводит (дефект оригинала, `fix-original-defects`).
void AAsdaPlayerCharacter::ComputeAttackSendPoints(int32 W, float Len, float Rewind, TArray<float>& OutFirst, TArray<float>& OutLoop)
{
	OutFirst.Reset(); OutLoop.Reset();
	constexpr float Eps = 1e-3f;
	const FAsdaComboTimeRow* R = FindComboTimeRow(this, AttackClipName(W));
	const int32 N = R ? FMath::Clamp(R->StepCount, 1, 9) : 1;
	if (R && N >= 2)
	{
		const float B[9] = { R->FrameTimeB0, R->FrameTimeB1, R->FrameTimeB2, R->FrameTimeB3, R->FrameTimeB4,
		                     R->FrameTimeB5, R->FrameTimeB6, R->FrameTimeB7, R->FrameTimeB8 };
		const float A[9] = { R->FrameTimeA0, R->FrameTimeA1, R->FrameTimeA2, R->FrameTimeA3, R->FrameTimeA4,
		                     R->FrameTimeA5, R->FrameTimeA6, R->FrameTimeA7, R->FrameTimeA8 };
		auto At = [&B, &A](int32 S) { return ((B[S] > 0.f) ? B[S] : A[S]) * kFrameToMs * 0.001f; };
		auto AddIn = [Eps, Len](TArray<float>& Out, float T, float From) { if (T > From + Eps && T < Len - Eps) { Out.AddUnique(T); } };
		for (int32 S = 1; S < N; S += 2) { AddIn(OutFirst, At(S), 0.f); }          // петля 0: шаг S → S+1 чётный
		if (N % 2 == 1) { AddIn(OutFirst, At(N - 1), 0.f); }                         // петля 0: шаг N → сброс в 0 (чётный)
		if (Rewind > Eps)                                                             // петля 1 внутри первого проигрывания
		{
			for (int32 S = 1; S < N; S += 2) { AddIn(OutFirst, Rewind + At(S), Rewind); }
		}
		for (int32 S = 1; S < N; S += 2) { AddIn(OutLoop, Rewind + At(S), Rewind); }  // проходы цикла: W + B[нечётные]
	}
	if (N % 2 == 1) { OutFirst.Add(Len); OutLoop.Add(Len); }   // нечётное число шагов и дальнобойное (N = 1) — конец прохода
	if (OutFirst.Num() == 0) { OutFirst.Add(Len); }            // вырожденная строка — как прежде, один удар на проход
	if (OutLoop.Num() == 0) { OutLoop.Add(Len); }
	OutFirst.Sort(); OutLoop.Sort();
}

float AAsdaPlayerCharacter::NextSendDistClip(float Pos, bool& bOutAtClipEnd) const
{
	return SendDistFromClip(Pos, bAttackFirstPass, bOutAtClipEnd);
}

float AAsdaPlayerCharacter::SendDistFromClip(float Pos, bool bFirst, bool& bOutAtClipEnd) const
{
	constexpr float Eps = 1e-3f;
	// ★ Точка, до которой осталось меньше 50 мс клип-времени, считается ПРОЙДЕННОЙ: таймер точки отправки срабатывает по
	//   кадру и может прийти на доли кадра раньше позиции точки — без этого допуска отрезок «в ту же точку» длиной в
	//   несколько миллисекунд сбивал фазу (самотест: посох, сдвиг 0.69 с). Точки отправки отстоят друг от друга ≥ 0.6 с.
	constexpr float Consumed = 0.05f;
	const float L = AttackLoopLenS;
	const TArray<float>& Cur = bFirst ? AttackSendPtsFirst : AttackSendPtsLoop;
	for (const float P : Cur)
	{
		if (P > Pos + Consumed || (P >= L - Eps && P > Pos + Eps)) { bOutAtClipEnd = (P >= L - Eps); return P - Pos; }
	}
	// В этом проходе точек не осталось — первая точка следующего прохода (после перемотки).
	const float P0 = AttackSendPtsLoop.Num() > 0 ? AttackSendPtsLoop[0] : L;
	bOutAtClipEnd = false;
	return FMath::Max(0.f, L - Pos) + FMath::Max(0.f, P0 - AttackLoopRewindS);
}

float AAsdaPlayerCharacter::FirstSendSegmentClipSec(int32 WeaponSubType)
{
	const int32 W = (WeaponSubType == 10 && EquippedWeaponSubType != 99) ? EquippedWeaponSubType : WeaponSubType;
	const float Len = AttackClipLen(W);
	if (Len <= 0.f) { return 0.f; }
	TArray<float> F, Lp;
	ComputeAttackSendPoints(W, Len, 0.f, F, Lp);
	return F.Num() > 0 ? F[0] : Len;
}

void AAsdaPlayerCharacter::BeginSendSegment(const TCHAR* Why)
{
	if (!IsServerRhythmOwner() || AttackLoopClipIdx < 0 || !BaseSkel || !GetWorld()) { return; }
	const float L = AttackLoopLenS;
	if (L <= 0.f) { return; }
	const float Pos = FMath::Clamp(GetAttackClipPositionSec(), 0.f, L);
	bool bAtEnd = false;
	const float Dist = NextSendDistClip(Pos, bAtEnd);
	UGameInstance* GI = GetGameInstance();
	const UAsdaAttackRhythmSubsystem* Rh = GI ? GI->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr;
	const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetController());
	// Сколько РЕАЛЬНОГО времени должен длиться отрезок: до серверного «можно» (с запасом). Удар поставлен к дедлайну —
	//   дедлайн плюс следующий интервал; ждёт повтора таймером — самый ранний момент плюс интервал; ждать нечего (удар не
	//   ушёл: цель вне дальности/мертва) — естественный интервал текущего шага.
	// ★ Ревизия СРЕДН-2: точка отправки целится на КАДР РАНЬШЕ этого момента — сам удар уходит точно к дедлайну с потока
	//   точной отправки, а таймер точки срабатывает только в кадре: целясь в дедлайн, он пришёл бы на кадр позже
	//   (при 20 FPS — +50 мс, больше бюджета «+5 %»).
	// ★ Ревизия 15.10 (НИЗК-3): «сейчас» — НАЧАЛО кадра (`FApp::GetCurrentTime`, те же часы, что `FPlatformTime`), а не
	//   середина кадра: таймер точки и клип отсчитывают время от начала кадра (`DeltaTime`), и после синхронной подгрузки
	//   (`FlushAsyncLoading`) в кадре «взвода» план по середине кадра приводил точку замаха раньше на длину рывка
	//   (r8_full: меч 33 мс, копьё 19 мс). Удар к дедлайну по-прежнему ставит поток точной отправки.
	const double NowS = FApp::GetCurrentTime();
	double EndS = 0.0;
	if (Rh)
	{
		const bool bRetryTimer = PC && PC->IsAttackSendDeferred() && !Rh->HasPending4028();
		EndS = bRetryTimer ? (Rh->EarliestNext4028S() + Rh->NominalNextIntervalS()) : Rh->PlanSegmentEndS(NowS);
	}
	// Оценка кадра — сглаженная (`FrameDtEma`): один рывок кадра (подгрузка ассета) не должен уводить точку на 90 мс вперёд.
	const double FrameS = FMath::Clamp((double)FrameDtEma, 0.004, 0.1);
	const double WaitS = (EndS > 0.0) ? FMath::Max(0.01, EndS - NowS - FrameS) : 0.0;
	float Rate = 0.f;
	bAttackSegEvened = false;
	if (WaitS > 0.0 && Dist > 0.f)
	{
		Rate = FMath::Clamp((float)((double)Dist / WaitS), kAsdaSyncRateMin, kAsdaSyncRateMax);
		// ★ Ровный темп серии (разбор у `CVarAsdaEvenSwingTempo`): отрезок первого прохода — не быстрее темпа цикла.
		if (bAttackFirstPass && Rh && CVarAsdaEvenSwingTempo.GetValueOnGameThread() != 0)
		{
			const float Seg = SteadyLoopSegmentClip();
			const double NomS = Rh->NominalNextIntervalS();
			if (Seg > 0.f && NomS > FrameS + 0.01)
			{
				const float SteadyRate = FMath::Clamp((float)((double)Seg / (NomS - FrameS)), kAsdaSyncRateMin, kAsdaSyncRateMax);
				if (Rate > SteadyRate * (1.f + kAsdaEvenTempoTol))
				{
					UE_LOG(LogAsdaPlayer, Log, TEXT("[ритм] ровный темп: отрезок первого прохода %.3f→+%.3f с просил скорость %.3f, темп цикла %.3f (отрезок %.3f с / %.0f мс) — идём темпом цикла, удар уйдёт через %.0f мс (сервер ждёт %.0f) [ОРИГИНАЛ: темп клипа один на серию]"),
						Pos, Dist, Rate, SteadyRate, Seg, NomS * 1000.0, (double)Dist / SteadyRate * 1000.0, WaitS * 1000.0);
					Rate = SteadyRate;
					bAttackSegEvened = true;
				}
			}
		}
	}
	else
	{
		// У сервера проверки нет (тип оружия без строки Combo.lst) — обычный темп (состояния, без предметов).
		const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		Rate = FMath::Max(0.1f, WS ? WS->GetAttackTempoMul(this) : 1.f);
	}
	AttackSyncRate = Rate;
	AttackClipTempo = Rate;
	if (AttackSegRates.Num() < 256) { AttackSegRates.Add(Rate); }
	OnSegmentRateChanged.Broadcast();   // ★ ревизия Ф2 (мелочь 5): таймеры кадров своего замаха — под новую скорость
	AttackSegPlannedS = (Rate > 0.f) ? Dist / Rate : 0.f;   // самотест ровности темпа (серия сбрасывается «взводом»)
	BaseSkel->SetPlayRate(Rate);
	FTimerManager& TM = GetWorld()->GetTimerManager();
	const float RestReal = FMath::Max(0.001f, (L - Pos) / Rate);
	TM.SetTimer(AttackLoopTimer, this, &AAsdaPlayerCharacter::OnAttackClipEnd, RestReal, false);
	AttackAnimUntilS = GetWorld()->GetTimeSeconds() + (double)RestReal;
	if (bAtEnd) { TM.ClearTimer(AttackSendTimer); }
	else { TM.SetTimer(AttackSendTimer, this, &AAsdaPlayerCharacter::OnAttackSendPoint, FMath::Max(0.001f, Dist / Rate), false); }
	UE_LOG(LogAsdaPlayer, Log, TEXT("[ритм] отрезок замаха (%s): клип %.3f→+%.3f с%s, ждать %.0f мс → скорость %.3f"),
		Why ? Why : TEXT("?"), Pos, Dist, bAtEnd ? TEXT(" (конец прохода)") : TEXT(""), WaitS * 1000.0, Rate);
	ASDA_CTRACE_V(EAsdaCT::Swing, TEXT("%s: отрезок замаха (%s) клип %.3f→+%.3f с%s, до серверного «можно» %.0f мс → скорость клипа ×%.3f%s, след. точка 4028 через %.0f мс"),
		*AsdaCtPcWho(this), Why ? Why : TEXT("?"), Pos, Dist, bAtEnd ? TEXT(" (конец прохода)") : TEXT(""), WaitS * 1000.0, Rate,
		bAttackSegEvened ? TEXT(" (ровный темп серии)") : TEXT(""), (double)(Dist / Rate) * 1000.0);
}

void AAsdaPlayerCharacter::OnAttackSendPoint()
{
	if (!GetWorld() || AttackLoopClipIdx < 0) { return; }
	// ★ Ф2: у соседа с замахом от пакетов точка отправки — граница отрезка (своего 4028 у соседа нет).
	if (bPeerPktDriven) { BeginPeerSegment(TEXT("точка отправки"), /*bInclStart=*/false); return; }
	ASDA_CTRACE_V(EAsdaCT::Swing, TEXT("%s: точка отправки 4028 внутри прохода (клип %.3f с, %s проход) → контроллер (цель/дальность/часы)"),
		*AsdaCtPcWho(this), GetAttackClipPositionSec(), bAttackFirstPass ? TEXT("первый") : TEXT("цикловой"));
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetController()))
	{
		PC->OnSwingCycleCompleted();   // проверки цели/дальности/часов — там же; ранний момент → отложенная отправка
	}
	BeginSendSegment(TEXT("точка отправки"));
}

void AAsdaPlayerCharacter::SuspendAttackLoopForSkill()
{
	if (!GetWorld()) { return; }
	const bool bWas = AttackLoopClipIdx >= 0;
	GetWorld()->GetTimerManager().ClearTimer(AttackLoopTimer);
	GetWorld()->GetTimerManager().ClearTimer(AttackSendTimer);
	AttackLoopClipIdx = -1; AttackLoopHoldUntilS = 0.0;
	AttackSyncRate = 0.f;
	AttackAnimUntilS = 0.0;   // гейт замаха снят: клип навыка (5257) и покой больше не перебиваются циклом
	// ★ 17.10.2026 (ревизия Ф2, ВАЖНО 3): тот же выход годится и СОСЕДУ, чей замах ведут пакеты, — навык соседа посреди серии
	//   (эталон: состояния 24/25, тик атаки стоит) снимает отрезки, их таймеры (`SetPlayRate` по клипу навыка, перебивка навыка
	//   клипом атаки в `OnAttackClipEnd`) и «тишину». Следующий 4029/4204 соседа начнёт замах «взводом». Отсчёт кадров PvP — тоже.
	ResetPeerPktState();
	ResetHitFrameScan();
	// ★ 18.10.2026 (ВАЖНО 3): навык снял цикл — замах кончен, таймеры его цифр больше не переставляются (`OwnHitSwingGuard`).
	if (IsOwnHitSwingGuardOn()) { ++AttackPassSerial; }
	++AttackSwingGen;
	if (bWas)
	{
		UE_LOG(LogAsdaPlayer, Log, TEXT("[ритм] заявка навыка: цикл замаха снят (эталон: состояние 25), возобновит боевой тик"));
		ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Swing, TEXT("%s: навык — цикл замаха СНЯТ (эталон: состояние 25), клип навыка не перебивается; цель %s"),
			*AsdaCtPcWho(this), *AsdaCtPcTarget(AttackFaceTarget.Get()));
	}
}

// ★★ Ревизия СРЕДН-1: ВЫХОД ИЗ БОЕВОЙ СТОЙКИ ПО ТАЙМЕРУ ЭТАЛОНА — для всех, кем не правит свой контроллер.
//   Эталон: тик покоя каждого персонажа `CGameUI_UpdateFadeAnim@0xA17B00` (game_main.c:7856-7876 и :8105-8125) в подтипе 20
//   сравнивает `timeGetTime() − fAutoActionStartTime > 10000` и уводит покой в подтип 15 (фейд → мирный покой).
//   `fAutoActionStartTime` ставит `CChar_SetState` при входе в подтип 20 (entity_char.c:19306-19313). Своего персонажа
//   ведёт контроллер (`CombatStanceUntil`, тот же 10 с, перевзвод каждым замахом); соседу раньше флаг стойки не снимало
//   ничто (4205 порт применял только к себе), и после боя он навсегда оставался в idle01 без фиджетов.
void AAsdaPlayerCharacter::TickCombatStanceExit()
{
	if (!bCombatStance || !GetWorld() || !UAsdaAttackRhythmSubsystem::IsEnabled()) { return; }
	if (Cast<AAsdaPlayerController>(GetController())) { return; }   // свой — у контроллера
	if (IsAttackAnimActive() || AttackLoopClipIdx >= 0) { return; }   // идёт замах (состояние 5) — тик покоя не идёт
	if (GetWorld()->GetTimeSeconds() - CombatStanceSinceS > kCombatStanceHoldS)
	{
		SetCombatStance(false);
		UE_LOG(LogAsdaPlayer, Verbose, TEXT("[стойка] сосед %s: 10 с боевого покоя — мирный покой (эталон подтип 20 → 15)"), *GetName());
		ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::State, TEXT("%s: %.0f с боевого покоя без замаха — выход из стойки в мирный покой (эталон подтип 20 → 15)"),
			*AsdaCtPcWho(this), kCombatStanceHoldS);
	}
}

// ★★ Ревизия СРЕДН-5: моменты кадров ударов замаха (клип-секунды) — тот же трек, что `GetComboTiming` (B, при 0 — A).
//   Первое проигрывание: петля 0 (B[шаг]) и петля 1 (перемотка + B[шаг]); проходы цикла — перемотка + B[шаг].
void AAsdaPlayerCharacter::ComputeAttackHitFrames(int32 W, float Len, float Rewind, TArray<float>& OutFirst, TArray<float>& OutLoop)
{
	OutFirst.Reset(); OutLoop.Reset();
	AttackHitFirstStep.Reset(); AttackHitLoopStep.Reset();
	constexpr float Eps = 1e-3f;
	const FAsdaComboTimeRow* R = FindComboTimeRow(this, AttackClipName(W));
	if (!R) { return; }
	const int32 N = FMath::Clamp(R->StepCount, 1, 9);
	const float B[9] = { R->FrameTimeB0, R->FrameTimeB1, R->FrameTimeB2, R->FrameTimeB3, R->FrameTimeB4,
	                     R->FrameTimeB5, R->FrameTimeB6, R->FrameTimeB7, R->FrameTimeB8 };
	const float A[9] = { R->FrameTimeA0, R->FrameTimeA1, R->FrameTimeA2, R->FrameTimeA3, R->FrameTimeA4,
	                     R->FrameTimeA5, R->FrameTimeA6, R->FrameTimeA7, R->FrameTimeA8 };
	// ★ Ф4: пары «кадр — шаг комбо» (шаг нужен PvP: выбор импакта `battk01..04` по шагу и правило пары крита на последнем шаге
	//   нечётного комбо, entity_char.c:2071); после сортировки по кадру шаги идут тем же порядком.
	TArray<TPair<float, int32>> F, Lp;   // кадр (клип-с), шаг
	for (int32 S = 0; S < N; ++S)
	{
		const float T = ((B[S] > 0.f) ? B[S] : A[S]) * kFrameToMs * 0.001f;
		if (T >= 0.f && T < Len - Eps) { F.Emplace(T, S); }
		if (Rewind > Eps && Rewind + T < Len - Eps) { F.Emplace(Rewind + T, S); }
		if (Rewind + T < Len - Eps) { Lp.Emplace(Rewind + T, S); }
	}
	auto ByTime = [](const TPair<float, int32>& X, const TPair<float, int32>& Y) { return X.Key < Y.Key; };
	F.StableSort(ByTime); Lp.StableSort(ByTime);
	for (const TPair<float, int32>& P : F)  { OutFirst.Add(P.Key); AttackHitFirstStep.Add(P.Value); }
	for (const TPair<float, int32>& P : Lp) { OutLoop.Add(P.Key);  AttackHitLoopStep.Add(P.Value); }
}

bool AAsdaPlayerCharacter::ClipTargetFromDelay(float DelayS, int32& OutSerial, float& OutPos) const
{
	if (AttackLoopClipIdx < 0 || AttackLoopLenS <= 0.f || !BaseSkel) { return false; }
	const float L = AttackLoopLenS, R = AttackLoopRewindS;
	const float PassLen = FMath::Max(0.05f, L - R);                            // клип-длина прохода цикла
	float Pos = FMath::Clamp(GetAttackClipPositionSec(), 0.f, L);
	float Dist = FMath::Max(0.f, DelayS) * FMath::Max(0.05f, AttackClipTempo);   // клип-расстояние до цели
	int32 Serial = AttackPassSerial;
	if (Pos + Dist < L) { OutSerial = Serial; OutPos = Pos + Dist; return true; }
	Dist -= (L - Pos); ++Serial;
	while (Dist >= PassLen) { Dist -= PassLen; ++Serial; }
	OutSerial = Serial; OutPos = R + Dist;
	return true;
}

float AAsdaPlayerCharacter::DelayToClipTarget(int32 Serial, float Pos) const
{
	if (AttackLoopClipIdx < 0 || AttackLoopLenS <= 0.f || !BaseSkel) { return -1.f; }
	const float L = AttackLoopLenS, R = AttackLoopRewindS;
	const float PassLen = FMath::Max(0.05f, L - R);
	const float P0 = FMath::Clamp(GetAttackClipPositionSec(), 0.f, L);
	float Dist = 0.f;
	if (Serial == AttackPassSerial) { Dist = Pos - P0; }
	else if (Serial > AttackPassSerial) { Dist = (L - P0) + (float)(Serial - AttackPassSerial - 1) * PassLen + (Pos - R); }
	else { Dist = 0.f; }   // проход уже позади
	return FMath::Max(0.f, Dist) / FMath::Max(0.05f, AttackClipTempo);
}

int32 AAsdaPlayerCharacter::ConsumeCrossedHitFrames(TArray<int32>& OutSteps)
{
	OutSteps.Reset();
	if (AttackLoopClipIdx < 0 || AttackLoopLenS <= 0.f || !BaseSkel) { HitScanPos = -1.f; return 0; }
	constexpr float Eps = 1e-3f;
	const float L = AttackLoopLenS, R = AttackLoopRewindS;
	const float Pos = FMath::Clamp(GetAttackClipPositionSec(), 0.f, L);
	const bool bFirst = bAttackFirstPass;
	// Проход цикла без кадра удара у дальнобойного (баллиста: кадр B0 = точке перемотки) — кадр в точке перемотки (как `PeerSegHits`).
	TArray<float> LoopHits = AttackHitLoop;
	TArray<int32> LoopSteps = AttackHitLoopStep;
	if (LoopHits.Num() == 0 && AttackHitFirst.Num() > 0) { LoopHits.Add(R); LoopSteps.Add(0); }
	// [ОРИГИНАЛ] Дальнобойное (ветки 8–11 `CCharacter_ResolveBasicAttack`, entity_char.c:27368–27560): выстрел — на пороге
	//   `петля·W + B0`, после него ожидание конца клипа (4203, кадр := W, петля 1) — ОДИН выстрел на проход, и в первом тоже
	//   (второго порога `W + B0` внутри первого проигрывания у дальнобойного нет). Списки кадров ближнего это допускают — режем.
	TArray<float> FirstHits = AttackHitFirst;
	TArray<int32> FirstSteps = AttackHitFirstStep;
	if (bHitScanRanged && FirstHits.Num() > 1) { FirstHits.SetNum(1); FirstSteps.SetNum(FMath::Min(1, FirstSteps.Num())); }
	if (bHitScanRanged && LoopHits.Num() > 1) { LoopHits.SetNum(1); LoopSteps.SetNum(FMath::Min(1, LoopSteps.Num())); }
	if (HitScanPos < 0.f)
	{
		// Первый опрос: с начала «взвода» кадр 0 входит; посреди цикла — отсчёт с текущей позиции.
		HitScanPos = (bFirst && Pos < 0.05f) ? -Eps : Pos;
		bHitScanFirst = bFirst;
		HitScanSerial = AttackPassSerial;
	}
	auto Take = [&OutSteps](const TArray<float>& Hs, const TArray<int32>& St, float From, float To, bool bInclFrom)
	{
		constexpr float E = 1e-3f;   // допуск сравнения кадров (клип-с)
		for (int32 i = 0; i < Hs.Num(); ++i)
		{
			const float H = Hs[i];
			const bool bAfter = bInclFrom ? (H >= From - E) : (H > From + E * 0.1f);
			if (bAfter && H <= To + E) { OutSteps.Add(St.IsValidIndex(i) ? St[i] : 0); }
		}
	};
	const TArray<float>& PrevHits = bHitScanFirst ? FirstHits : LoopHits;
	const TArray<int32>& PrevSteps = bHitScanFirst ? FirstSteps : LoopSteps;
	// Перемотка — ТОЛЬКО новый проход клипа (номер прохода растёт в `OnAttackClipEnd`). Откат позиции внутри прохода (рывок кадра при
	//   синхронной подгрузке, кадр со сброшенной позицией компонента) перемоткой не считается: иначе кадр удара прошёлся бы дважды
	//   (живой прогон 17.10: двойной выстрел лука в соседних кадрах).
	const bool bWrapped = (AttackPassSerial != HitScanSerial);
	if (!bWrapped)
	{
		if (Pos > HitScanPos) { Take(PrevHits, PrevSteps, HitScanPos, Pos, /*bInclFrom=*/false); }
		HitScanPos = FMath::Max(HitScanPos, Pos);
	}
	else
	{
		// Проход кончился и клип перемотан: хвост прошлого прохода и начало нового (кадр в точке перемотки — нового).
		Take(PrevHits, PrevSteps, HitScanPos, L - Eps, /*bInclFrom=*/false);
		Take(LoopHits, LoopSteps, R, Pos, /*bInclFrom=*/true);
		HitScanPos = Pos;
	}
	bHitScanFirst = bFirst;
	HitScanSerial = AttackPassSerial;
#if ASDA_COMBAT_TRACE
	if (OutSteps.Num() > 0 && (AsdaCombatTrace::IsOn(1) && (AsdaCombatTrace::IsOn(2) || AsdaCtPcOurs(this))))   // волна 2: сосед вне нашего боя — только уровень 2
	{
		FString CtSteps;   // журнал боя: шаги комбо пройденных кадров удара
		for (const int32 S : OutSteps) { CtSteps += FString::Printf(TEXT(" %d"), S); }
		ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::HitFrame, TEXT("%s: КАДР удара ×%d (шаги [%s ]) клип %.3f с, проход №%d (%s)%s, по %s"),
			*AsdaCtPcWho(this), OutSteps.Num(), *CtSteps, Pos, AttackPassSerial, bFirst ? TEXT("первый") : TEXT("цикловой"),
			bWrapped ? TEXT(", через перемотку") : TEXT(""), *AsdaCtPcTarget(AttackFaceTarget.Get()));
	}
#endif
	return OutSteps.Num();
}

float AAsdaPlayerCharacter::UpcomingHitDelaySec(int32 K) const
{
	if (K < 0 || AttackLoopClipIdx < 0 || AttackLoopLenS <= 0.f) { return -1.f; }
	const float L = AttackLoopLenS;
	const float P = FMath::Clamp(GetAttackClipPositionSec(), 0.f, L);
	const float Rate = FMath::Max(0.1f, AttackClipTempo);
	const TArray<float>& Cur = bAttackFirstPass ? AttackHitFirst : AttackHitLoop;
	int32 Idx = 0;
	for (const float X : Cur)
	{
		if (X > P + 1e-3f) { if (Idx == K) { return (X - P) / Rate; } ++Idx; }
	}
	if (AttackHitLoop.Num() == 0) { return -1.f; }
	float Base = L - P;
	for (int32 Pass = 0; Pass < 8; ++Pass)
	{
		for (const float X : AttackHitLoop)
		{
			if (Idx == K) { return (Base + (X - AttackLoopRewindS)) / Rate; }
			++Idx;
		}
		Base += L - AttackLoopRewindS;
	}
	return -1.f;
}

// ═══ ★★★ 17.10.2026 СИНХРОНИЗАЦИЯ БОЯ, Ф2 — ЗАМАХ СОСЕДА ОТ ПАКЕТОВ (разбор у объявления `PeerAcceptSwingPacket`) ══════════

// Ключ отката Ф2. Полный разбор того, что возвращает каждый ключ синхронизации боя, — у `CVarAsdaHitFrameAllChannels`
//   (`AsdaWorldSubsystem.cpp`).
static ASDA_DEV_CVAR(int32, CVarAsdaSwingPlanFromPackets,
	TEXT("asda.Combat.SwingPlanFromPackets"), 1,
	TEXT("1 (по умолчанию) = замах соседа ведут его пакеты 4029 (один принятый 4029 — один отрезок клипа «точка отправки → точка ")
	TEXT("отправки», темп по среднему интервалу 4029 в пределах 0.8…1.25, догон — ускорением нового отрезка), цифры и выстрелы ")
	TEXT("соседа и свои — на кадрах удара плана отрезка; 0 = замах соседа по времени (SwingLen + 1.5 с), цифры соседа и своего ")
	TEXT("дальнобойного — от начала клипа, но свой БЛИЖНИЙ бой под серверными часами (asda.Combat.ServerRhythm 1) остаётся на ")
	TEXT("плане кадров (как до Ф2, ревизия СРЕДН-5) — это не полное «состояние после Ф1»."),
	ECVF_Default);

static constexpr float kPeerTempoCoefMin = 0.8f;    // нижняя граница коэффициента темпа соседа (проект §4, §9 — джиттер пинга)
static constexpr float kPeerTempoCoefMax = 1.25f;   // верхняя граница
static constexpr int32 kPeerCoefSamplesMax = 4;     // выборок в среднем коэффициента (последние)
static constexpr float kPeerSkipFactor = 1.6f;      // интервал длиннее ожидаемого в столько раз — пропущенный 4029, выборки нет
static constexpr int32 kPeerMaxQueued = 2;          // отрезков в очереди сверх текущего; больше — догон (отставание от пакетов)
static constexpr float kPeerCatchUpMaxRate = 3.0f;  // ★ ревизия Ф2 (ВАЖНО 2): предел скорости отрезка догона (как у своего замаха)
static constexpr int32 kPeerQueueHardMax = 4;       // ★ 18.10.2026 (ревизия Ф4, мелочь 5): жёсткий потолок очереди отрезков соседа
static constexpr double kPeerBurstDtS = 0.05;       // ★ 18.10.2026 (мелочь 5): интервал прихода короче — пачка пакетов (рывок сети)
static constexpr float kPeerCatchUpDrain = 1.15f;   // ★ ревизия Ф2 (ВАЖНО 2): запас к темпу прихода пакетов — очередь тает
static constexpr float kPeerSilenceExtraS = 0.5f;   // запасной конец серии: тишина «отрезок + 0.5 с» (проект §4)

bool AAsdaPlayerCharacter::IsSwingPlanFromPackets()
{
	return CVarAsdaSwingPlanFromPackets.GetValueOnGameThread() != 0;
}

float AAsdaPlayerCharacter::PeerNominalTempo() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	return FMath::Max(0.1f, WS ? WS->GetAttackTempoMul(this) : 1.f);
}

float AAsdaPlayerCharacter::GetPeerTempoCoef() const
{
	if (PeerCoefSamples.Num() == 0) { return 1.f; }
	float Sum = 0.f;   // сумма выборок
	for (const float X : PeerCoefSamples) { Sum += X; }
	return FMath::Clamp(Sum / (float)PeerCoefSamples.Num(), kPeerTempoCoefMin, kPeerTempoCoefMax);
}

void AAsdaPlayerCharacter::ResetPeerPktState()
{
	bPeerPktDriven = false;
	PeerQueuedRates.Reset();
	bPeerSegClaimed = false;
	PeerSegRate = 0.f;
	PeerSegDistClip = 0.f;
	PeerLastAcceptS = -1.0;
	PeerLastAcceptNatS = 0.f;
	PeerCoefSamples.Reset();
	if (UWorld* W = GetWorld()) { W->GetTimerManager().ClearTimer(PeerSilenceTimer); }
}

void AAsdaPlayerCharacter::OnPeerSilence()
{
	if (!bPeerPktDriven) { return; }
	UE_LOG(LogAsdaPlayer, Log, TEXT("[бой-Ф2] сосед %s: 4029 нет дольше «отрезок + %.1f с» — запасной конец замаха (основной — отказ 4029)"),
		*GetName(), kPeerSilenceExtraS);
	ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Peer, TEXT("%s: 4029 нет дольше «отрезок + %.1f с» — запасной конец замаха соседа (основной — отказ 4029)"),
		*AsdaCtPcWho(this), kPeerSilenceExtraS);
	StopAttackAnim();
}

void AAsdaPlayerCharacter::PeerSegHits(float StartPos, bool bStartFirst, float Dist, bool bInclStart, TArray<float>& OutDist) const
{
	OutDist.Reset();
	constexpr float Eps = 1e-3f;
	const float L = AttackLoopLenS, R = AttackLoopRewindS;
	if (L <= 0.f || Dist <= 0.f) { return; }
	// Кадры удара прохода цикла. Дальнобойное без кадра внутри прохода цикла (баллиста: B0 = точке перемотки, `R + B0` за концом
	//   клипа) — кадр в самой точке перемотки: эталон после перемотки на B0 стреляет сразу (`entity_char.c` ветка 0xB, кадр := B0).
	TArray<float> LoopHits = AttackHitLoop;
	if (bPeerRanged && LoopHits.Num() == 0) { LoopHits.Add(R); }
	const TArray<float>& CurHits = bStartFirst ? AttackHitFirst : LoopHits;
	const float InPassEnd = FMath::Min(StartPos + Dist, L);   // конец части отрезка в текущем проходе
	for (const float H : CurHits)
	{
		const bool bAfterStart = bInclStart ? (H >= StartPos - Eps) : (H > StartPos + Eps);
		if (bAfterStart && H <= InPassEnd + Eps && H < L - Eps) { OutDist.Add(H - StartPos); }
	}
	if (StartPos + Dist > L + Eps)
	{
		// Отрезок переходит через конец прохода и перемотку: кадры прохода цикла от точки перемотки до конца отрезка.
		const float AfterRewind = Dist - (L - StartPos);   // клип-длина отрезка после перемотки
		for (const float H : LoopHits)
		{
			if (H >= R - Eps && H <= R + AfterRewind + Eps) { OutDist.Add((L - StartPos) + (H - R)); }
		}
	}
	OutDist.Sort();
	if (bPeerRanged && OutDist.Num() > 1) { OutDist.SetNum(1); }   // дальнобойное: один выстрел на отрезок (один 4028)
}

void AAsdaPlayerCharacter::BeginPeerSegment(const TCHAR* Why, bool bInclStart)
{
	if (!bPeerPktDriven || AttackLoopClipIdx < 0 || !BaseSkel || !GetWorld()) { return; }
	const float L = AttackLoopLenS;
	if (L <= 0.f) { return; }
	const float Pos = FMath::Clamp(GetAttackClipPositionSec(), 0.f, L);
	bool bAtEnd = false;
	const float Dist = NextSendDistClip(Pos, bAtEnd);
	float Rate = PeerNominalTempo() * GetPeerTempoCoef();   // отрезок без ответа — текущей оценкой темпа по интервалам 4029
	bool bClaimed = false;
	if (PeerQueuedRates.Num() > 0) { Rate = PeerQueuedRates[0]; PeerQueuedRates.RemoveAt(0); bClaimed = true; }
	Rate = FMath::Max(0.05f, Rate);
	const double Now = GetWorld()->GetTimeSeconds();
	// Начало отрезка — по ПЛАНУ (конец прошлого отрезка), а не по кадру, в котором сработал таймер границы: по плану уже стоят
	//   цифры/выстрелы отрезков в очереди (`PeerAcceptSwingPacket`), а клип шёл непрерывно (на конце прохода перемотка учитывает
	//   перелёт, `OnAttackClipEnd`). Опоздание таймера (≤ кадра) уходит в позицию клипа, а не в шкалу времени отрезков.
	double StartS = Now;     // мир-время начала отрезка
	float StartClip = Pos;   // позиция клипа в начале отрезка
	if (PeerSegRate > 0.f && PeerSegDistClip > 0.f)
	{
		const double PlannedEnd = PeerSegStartS + (double)PeerSegDistClip / (double)PeerSegRate;   // конец прошлого отрезка по плану
		const double Lag = Now - PlannedEnd;                                                          // опоздание таймера границы
		if (Lag >= 0.0 && Lag < 0.25) { StartS = PlannedEnd; StartClip = FMath::Max(0.f, Pos - (float)Lag * PeerSegRate); }
	}
	PeerSegRate = Rate; AttackClipTempo = Rate;
	BaseSkel->SetPlayRate(Rate);
	PeerSegStartS = StartS; PeerSegStartClip = StartClip; bPeerSegStartFirst = bAttackFirstPass; bPeerSegInclStart = bInclStart;
	PeerSegDistClip = Dist + (Pos - StartClip); bPeerSegAtEnd = bAtEnd; bPeerSegClaimed = bClaimed;
	FTimerManager& TM = GetWorld()->GetTimerManager();
	const float RestReal = FMath::Max(0.001f, (L - Pos) / Rate);
	TM.SetTimer(AttackLoopTimer, this, &AAsdaPlayerCharacter::OnAttackClipEnd, RestReal, false);
	AttackAnimUntilS = Now + (double)RestReal;
	if (bAtEnd) { TM.ClearTimer(AttackSendTimer); }
	else { TM.SetTimer(AttackSendTimer, this, &AAsdaPlayerCharacter::OnAttackSendPoint, FMath::Max(0.001f, Dist / Rate), false); }
#if !UE_BUILD_SHIPPING
	if (PeerSegLog.Num() >= 64) { PeerSegLog.RemoveAt(0); }   // ★ ревизия Ф2 (мелочь 4): кольцо — журнал не перестаёт писаться
	{
		FAsdaPeerSegSeen& E = PeerSegLog.AddDefaulted_GetRef();
		E.StartS = StartS; E.bClaimed = bClaimed; E.Rate = Rate; E.StartClip = StartClip; E.bFirstPass = bAttackFirstPass;
	}
#endif
	UE_LOG(LogAsdaPlayer, Log, TEXT("[бой-Ф2] отрезок соседа (%s): клип %.3f→+%.3f с%s, скорость %.3f, %s, в очереди %d"),
		Why ? Why : TEXT("?"), Pos, Dist, bAtEnd ? TEXT(" (конец прохода)") : TEXT(""), Rate,
		bClaimed ? TEXT("взят пакетом") : TEXT("без ответа"), PeerQueuedRates.Num());
	ASDA_CTRACE_V(EAsdaCT::Peer, TEXT("%s: отрезок замаха соседа (%s) клип %.3f→+%.3f с%s, скорость ×%.3f, %s, в очереди %d"),
		*AsdaCtPcWho(this), Why ? Why : TEXT("?"), Pos, Dist, bAtEnd ? TEXT(" (конец прохода)") : TEXT(""), Rate,
		bClaimed ? TEXT("взят пакетом") : TEXT("без ответа"), PeerQueuedRates.Num());
}

void AAsdaPlayerCharacter::PeerAcceptSwingPacket(int32 WeaponSubType, bool bRanged, FAsdaPeerSwingPlan& Out, bool bNewSeries)
{
	Out = FAsdaPeerSwingPlan();
	UWorld* W = GetWorld();
	if (!W || !BaseSkel) { return; }
	const double Now = W->GetTimeSeconds();
	const int32 Wr = (WeaponSubType == 10 && EquippedWeaponSubType != 99) ? EquippedWeaponSubType : WeaponSubType;   // класс набора клипов
	const int32 ClipIdx = AttackClipIdx(Wr);
	const float Nom = PeerNominalTempo();
	bPeerRanged = bRanged;
	const bool bFresh = !bPeerPktDriven || !IsAttackLoopAlive() || AttackLoopClipIdx != ClipIdx;
	bool bOnlyUpcoming = false;   // ★ ревизия Ф2 (ВАЖНО 1): новая серия при идущем цикле — в план только кадры впереди
	if (bFresh)
	{
		// [ОРИГИНАЛ] Не в замахе — вход в состояние 5 с кадра 0 («взвод», network.c:42070–42081 `CChar_SetState(…, 5, …)`); первый
		//   отрезок — номинальным темпом (эталон: темп клипа `(M+1)·dt`, выборок интервала ещё нет).
		ResetPeerPktState();
		bPeerPktDriven = true;
		bPeerRanged = bRanged;
		PeerSegRate = Nom;
		PeerQueuedRates.Add(Nom);
		AttackLoopClipIdx = -1;   // полный вход («взвод»), даже если шёл цикл по времени (ключ переключали)
		const float Len = StartAttackSwing(WeaponSubType, /*HoldSec=*/-1.f);   // держит до конца серии (отказ/тишина)
		if (Len <= 0.f) { ResetPeerPktState(); return; }
		BeginPeerSegment(TEXT("взвод"), /*bInclStart=*/true);
		Out.bStarted = true;
	}
	else
	{
		// Выборка темпа: естественная длительность отрезка ПРОШЛОГО 4029 / интервал между 4029. Пропуск 4029 (сервер молча
		//   отбросил ранний 4028) — интервал ≈ два отрезка: выборка не берётся, серия не рвётся.
		if (PeerLastAcceptS > 0.0 && PeerLastAcceptNatS > 0.f)
		{
			const double Dt = Now - PeerLastAcceptS;                                            // интервал прихода
			const double Expected = (double)PeerLastAcceptNatS / (double)GetPeerTempoCoef();   // ожидаемый интервал
			if (Dt > 0.05 && Dt < Expected * kPeerSkipFactor)
			{
				PeerCoefSamples.Add(FMath::Clamp((float)((double)PeerLastAcceptNatS / Dt), 0.5f, 2.f));
				if (PeerCoefSamples.Num() > kPeerCoefSamplesMax) { PeerCoefSamples.RemoveAt(0); }
			}
			else
			{
				UE_LOG(LogAsdaPlayer, Log, TEXT("[бой-Ф2] сосед %s: интервал 4029 %.3f с против ожидаемых %.3f — пропуск ответа, выборка темпа не берётся"),
					*GetName(), Dt, Expected);
			}
		}
		float Rate = Nom * GetPeerTempoCoef();         // скорость нового отрезка
		const double Elapsed = Now - PeerSegStartS;    // сколько идёт текущий отрезок
		const double CurDur = (PeerSegRate > 0.f) ? (double)PeerSegDistClip / (double)PeerSegRate : 0.0;
		if (bNewSeries)
		{
			// ★★★ Ревизия Ф2 (ВАЖНО 1) [ОРИГИНАЛ]: новая серия (4029 вне серии / смена цели) — очередь соседа сброшена и наполнена
			//   заново (network.c:42069–42081), значения снимаются на БЛИЖАЙШИХ кадрах удара. Отрезки прошлой серии из очереди
			//   снимаются, пакету отдаётся текущий отрезок (его кадры впереди); прежде значения ждали за 1–2 пустыми отрезками.
			if (PeerQueuedRates.Num() > 0)
			{
				UE_LOG(LogAsdaPlayer, Log, TEXT("[бой-Ф2] сосед %s: новая серия — очередь отрезков прошлой серии (%d) снята"),
					*GetName(), PeerQueuedRates.Num());
			}
			PeerQueuedRates.Reset();
			bPeerSegClaimed = true;
			bOnlyUpcoming = true;
		}
		else if (!bPeerSegClaimed && PeerQueuedRates.Num() == 0 && Elapsed <= 0.5 * CurDur)
		{
			// Отрезок «без ответа» только начался — ответ опоздал на долю отрезка: этот отрезок его (кадры, что уже позади, — сразу).
			bPeerSegClaimed = true;
		}
		else if (PeerQueuedRates.Num() >= kPeerQueueHardMax)
		{
			// ★★ 18.10.2026 (ревизия Ф4, мелочь 5) [ПОРТ]: ЖЁСТКИЙ ПОТОЛОК ОЧЕРЕДИ. Пачка пакетов быстрее любого предела темпа (рывок
			//   сети) растила очередь отрезков без границы — план кадров уходил за потолок записи ленты (4 с), цифры отставали от боя.
			//   [ОРИГИНАЛ] у эталона одна очередь значений (`addEntriesBounded4`), лишнее не копится. Здесь пакет сверх потолка берёт
			//   ПОСЛЕДНИЙ отрезок очереди (нового не добавляет): его значения выходят на тех же кадрах удара — урон не теряется.
			Out.QueuedAhead = PeerQueuedRates.Num();
			UE_LOG(LogAsdaPlayer, Log, TEXT("[бой-Ф2] сосед %s: очередь отрезков %d на потолке — пакет берёт последний отрезок очереди"),
				*GetName(), PeerQueuedRates.Num());
		}
		else
		{
			if (PeerQueuedRates.Num() >= kPeerMaxQueued)
			{
				// ★★★ Ревизия Ф2 (ВАЖНО 2): ДОГОН БЕЗ ОБРЫВА. Пакеты идут быстрее, чем позволяет предел темпа (неизвестный нам бафф
				//   скорости, рывок сети). Прежде текущий отрезок обрывался в своей точке отправки (`SetPosition`), а таймеры цифр и
				//   выстрелов, уже поставленные по плану, оставались на прежних моментах — цифры отставали от анимации на обрезанный
				//   хвост, поза прыгала. Теперь планы НЕ ломаются: уже стоящие отрезки играют своим темпом, а НОВЫЙ отрезок идёт
				//   темпом прихода пакетов (естественная длина прошлого отрезка / интервал) с запасом `kPeerCatchUpDrain` — очередь
				//   тает без скачков. Предел скорости — `kPeerCatchUpMaxRate`.
				const double Dt = (PeerLastAcceptS > 0.0) ? (Now - PeerLastAcceptS) : 0.0;   // интервал прихода пакетов
				if (Dt > 0.05 && PeerLastAcceptNatS > 0.f)
				{
					const float Need = (float)((double)PeerLastAcceptNatS * (double)Nom / Dt) * kPeerCatchUpDrain;   // темп «как приходят»
					Rate = FMath::Clamp(FMath::Max(Rate, Need), Rate, kPeerCatchUpMaxRate);
				}
				// ★ 18.10.2026 (мелочь 5): пачка (интервал ≤ 0.05 с) — темп прихода не измерить, а очередь уже за пределом: догон предельной
				//   скоростью (прежде ×1.15 — очередь росла).
				else if (Dt > 0.0 && Dt <= kPeerBurstDtS) { Rate = kPeerCatchUpMaxRate; }
				else { Rate = FMath::Min(Rate * kPeerCatchUpDrain, kPeerCatchUpMaxRate); }
				UE_LOG(LogAsdaPlayer, Log, TEXT("[бой-Ф2] сосед %s: очередь отрезков %d — догон ускорением нового отрезка: скорость %.3f (интервал %.3f с)"),
					*GetName(), PeerQueuedRates.Num(), Rate, Dt);
			}
			PeerQueuedRates.Add(Rate);
			Out.QueuedAhead = PeerQueuedRates.Num();
		}
	}
	// План: пройти от текущего отрезка до отрезка этого пакета (QueuedAhead) — моменты начала и кадры удара.
	float SegPos = PeerSegStartClip;          // начало рассматриваемого отрезка (клип-с)
	bool bSegFirst = bPeerSegStartFirst;      // вид прохода в его начале
	bool bIncl = bPeerSegInclStart;           // кадр в точке начала — его
	float SegDist = PeerSegDistClip;          // клип-длина
	bool bSegAtEnd = bPeerSegAtEnd;           // кончается концом прохода
	float SegRate = FMath::Max(0.05f, PeerSegRate);   // скорость
	double SegStartS = PeerSegStartS;         // мир-время начала
	for (int32 k = 0; k < Out.QueuedAhead; ++k)
	{
		SegStartS += (double)SegDist / (double)SegRate;
		// Следующий отрезок начинается там, где кончился этот: конец прохода — с точки перемотки (проход цикла), иначе в точке
		//   отправки того же прохода либо (через конец прохода) после перемотки.
		const float L = AttackLoopLenS, R = AttackLoopRewindS;
		if (bSegAtEnd) { SegPos = R; bSegFirst = false; bIncl = true; }
		else if (SegPos + SegDist < L - 1e-3f) { SegPos += SegDist; bIncl = false; }
		else { SegPos = R + (SegDist - (L - SegPos)); bSegFirst = false; bIncl = false; }
		SegDist = SendDistFromClip(SegPos, bSegFirst, bSegAtEnd);
		SegRate = FMath::Max(0.05f, PeerQueuedRates.IsValidIndex(k) ? PeerQueuedRates[k] : SegRate);
	}
	TArray<float> HitDist;   // кадры удара отрезка пакета (клип-расстояния от его начала)
	PeerSegHits(SegPos, bSegFirst, SegDist, bIncl, HitDist);
	for (const float D : HitDist)
	{
		const double AtRel = SegStartS + (double)D / (double)SegRate - Now;   // момент кадра от «сейчас» (отрицательный — позади)
		if (bOnlyUpcoming && AtRel < -0.02) { continue; }                     // ★ ВАЖНО 1: прошлые кадры текущего отрезка — не этой серии
		Out.HitDelaysS.Add(FMath::Max(0.f, (float)AtRel));
	}
	if (bOnlyUpcoming && Out.HitDelaysS.Num() == 0)
	{
		// ★ ВАЖНО 1: кадров впереди в текущем отрезке нет — пакету следующий отрезок (взят им, темп по оценке).
		const float Rate2 = FMath::Max(0.05f, Nom * GetPeerTempoCoef());
		bPeerSegClaimed = false;   // текущий отрезок пакетом не взят (его кадры позади)
		PeerQueuedRates.Add(Rate2);
		Out.QueuedAhead = 1;
		const double Start2 = PeerSegStartS + (double)PeerSegDistClip / (double)FMath::Max(0.05f, PeerSegRate);
		const float L = AttackLoopLenS, R = AttackLoopRewindS;
		float Pos2 = PeerSegStartClip; bool bFirst2 = bPeerSegStartFirst; bool bIncl2 = false; bool bEnd2 = false;
		if (bPeerSegAtEnd) { Pos2 = R; bFirst2 = false; bIncl2 = true; }   // отрезок кончается концом прохода — следующий с точки перемотки
		else if (Pos2 + PeerSegDistClip < L - 1e-3f) { Pos2 += PeerSegDistClip; }
		else { Pos2 = R + (PeerSegDistClip - (L - Pos2)); bFirst2 = false; }
		const float Dist2 = SendDistFromClip(Pos2, bFirst2, bEnd2);
		PeerSegHits(Pos2, bFirst2, Dist2, bIncl2, HitDist);
		for (const float D : HitDist) { Out.HitDelaysS.Add(FMath::Max(0.f, (float)(Start2 + (double)D / (double)Rate2 - Now))); }
		SegDist = Dist2; SegRate = Rate2; SegStartS = Start2;
	}
	Out.SegDurS = SegDist / SegRate;
	Out.StartInS = FMath::Max(0.f, (float)(SegStartS - Now));
	PeerLastAcceptS = Now;
	PeerLastAcceptNatS = SegDist / Nom;
	// Запасной конец серии: тишина дольше «конец отрезка этого пакета + ещё отрезок + 0.5 с».
	const float SilenceIn = Out.StartInS + 2.f * Out.SegDurS + kPeerSilenceExtraS;
	W->GetTimerManager().SetTimer(PeerSilenceTimer, this, &AAsdaPlayerCharacter::OnPeerSilence, FMath::Max(0.1f, SilenceIn), false);
	FString HitsStr; for (const float X : Out.HitDelaysS) { HitsStr += FString::Printf(TEXT(" %.3f"), X); }
	UE_LOG(LogAsdaPlayer, Log, TEXT("[бой-Ф2] сосед %s: принят 4029 — %s, отрезок через %.3f с, длина %.3f с, кадры удара через [%s ] с, коэффициент темпа %.3f (выборок %d)"),
		*GetName(), Out.bStarted ? TEXT("взвод") : (Out.QueuedAhead > 0 ? TEXT("в очередь") : TEXT("текущий отрезок")),
		Out.StartInS, Out.SegDurS, *HitsStr, GetPeerTempoCoef(), PeerCoefSamples.Num());
	ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Peer, TEXT("%s: замах по 4029 — %s%s, отрезок через %.3f с длиной %.3f с, кадры удара через [%s ] с, темп ×%.3f (выборок %d), цель %s"),
		*AsdaCtPcWho(this), Out.bStarted ? TEXT("взвод") : (Out.QueuedAhead > 0 ? *FString::Printf(TEXT("в очередь №%d"), Out.QueuedAhead) : TEXT("текущий отрезок")),
		bNewSeries ? TEXT(" (новая серия)") : TEXT(""), Out.StartInS, Out.SegDurS, *HitsStr, GetPeerTempoCoef(), PeerCoefSamples.Num(),
		*AsdaCtPcTarget(AttackFaceTarget.Get()));
}

#if !UE_BUILD_SHIPPING
void AAsdaPlayerCharacter::NaturalSegmentsS(int32 Count, TArray<float>& Out) const
{
	Out.Reset();
	if (AttackLoopLenS <= 0.f || Count <= 0) { return; }
	const float Nom = PeerNominalTempo();
	float Pos = 0.f; bool bFirst = true;   // замах с кадра 0, первый проход
	for (int32 k = 0; k < Count; ++k)
	{
		bool bAtEnd = false;
		const float D = SendDistFromClip(Pos, bFirst, bAtEnd);
		Out.Add(D / Nom);
		const float L = AttackLoopLenS, R = AttackLoopRewindS;
		if (bAtEnd) { Pos = R; bFirst = false; }
		else if (Pos + D < L - 1e-3f) { Pos += D; }
		else { Pos = R + (D - (L - Pos)); bFirst = false; }
	}
}
#endif

// ★ Эталон Player_ProcessAttackTick LABEL_155 (entity_char.c:1101-1107 и ветки лука/арбалета/баллисты):
//   клип атаки ДОИГРАЛ ДО КОНЦА; если автоатака ещё активна (эталон 52121==100 / наше удержание) —
//   кадр := WindowTime и проход повторяется («взвод» не переигрывается). Иначе — восстановление
//   клипа текущего состояния (стойка/бег), как делал прежний таймер PlayAttackClip.
void AAsdaPlayerCharacter::OnAttackClipEnd()
{
	if (!GetWorld()) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	const double PrevUntilS = AttackAnimUntilS;   // ★ Ф2: расчётный конец прохода (перелёт таймера — для соседа от пакетов)
	const bool bHold = (AttackLoopClipIdx >= 0) && AnimState != EAsdaPlayerState::Dead &&
		(AttackLoopHoldUntilS < 0.0 || Now < AttackLoopHoldUntilS);
	if (bHold)
	{
		// ★ 15.10.2026: у своего персонажа под серверными часами конец прохода — точка отправки ТОЛЬКО там, где её ставит
		//   эталон (дальнобойное оружие и нечётное число шагов); у ближнего с чётным числом шагов удары ушли внутри прохода.
		const bool bRhythmOwner = IsServerRhythmOwner();
		const TArray<float>& EndingPts = bAttackFirstPass ? AttackSendPtsFirst : AttackSendPtsLoop;
		const bool bEndIsSend = !bRhythmOwner || (EndingPts.Num() > 0 && EndingPts.Last() >= AttackLoopLenS - 1e-3f);
		const float L = PlayWorldClip(AttackLoopClipIdx, false);
		if (L > 0.f)
		{
			// ★★★ РИТМ ЗАДАЁТ АНИМАЦИЯ (правка 2026-08-25). Проход клипа кончился — здесь же
			//   уходит следующий удар, ровно как в оригинале: он шлёт заявку ИЗНУТРИ тика замаха
			//   (`skill_buff.c:762-793`), а не по таймеру ответа сервера. Раньше у нас работали два
			//   независимых таймера — анимация 0.85 с против пакетов 1.31 с, и примерно каждый
			//   третий взмах был холостым.
			if (bEndIsSend)
			{
				if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetController()))
				{
					PC->OnSwingCycleCompleted();
				}
			}
			if (AttackLoopRewindS > 0.f && BaseSkel) { BaseSkel->SetPosition(AttackLoopRewindS, false); }
			// ★ 17.10.2026 (Ф2): у соседа с замахом от пакетов перелёт за конец клипа (таймер конца прохода срабатывает в кадре,
			//   позже расчётного) не выбрасывается — клип продолжается с точки перемотки + перелёт, как непрерывный эталонный
			//   тик кадра; иначе шкала отрезков отставала бы на долю кадра каждый проход, а план кадров удара — нет.
			// ★ 17.10.2026 (Ф4, прогон самотеста наблюдателя): то же у СВОЕГО замаха — без этого каждый проход своего клипа отставал на
			//   перелёт таймера (лук: +0.668 → +0.698 с от начала отрезка за 8 проходов), и свои выстрелы расходились с соседскими.
			//   Эталонный тик кадра непрерывен для всех персонажей.
			// ★ 18.10.2026 (мелочь 1): ключ отката `asda.Combat.ClipCarryOverAll 0` — перелёт только у соседа от пакетов (как Ф2).
			const bool bCarryOver = BaseSkel && PrevUntilS > 0.0 && (bPeerPktDriven || CVarAsdaClipCarryOverAll.GetValueOnGameThread() != 0);
			if (bCarryOver)
			{
				const double Over = FMath::Clamp(Now - PrevUntilS, 0.0, 0.1);   // перелёт за конец прохода (реальные с)
				if (Over > 0.0) { BaseSkel->SetPosition(AttackLoopRewindS + (float)Over * FMath::Max(0.05f, AttackClipTempo), false); }
			}
			bAttackFirstPass = false;   // ★ 15.10.2026: дальше — проходы цикла (точки от перемотки)
			++AttackPassSerial;         // ★ Ф4: новый проход — перемотка для счёта кадров удара (`ConsumeCrossedHitFrames`)
			PlaySwingSound(EquippedWeaponSubType);   // ★ каждый проход цикла — свой звук замаха
			// ★★★ ТЕМП: остаток прохода считается в клип-времени (`L` и точка перемотки — позиции в
			//   клипе), а таймер и гейт замаха живут реальным временем — делим на множитель темпа.
			// ★ Ф2: у соседа от пакетов остаток прохода — от фактической позиции (точка перемотки + перелёт).
			const float RestClip = bCarryOver ? FMath::Max(0.f, L - GetAttackClipPositionSec()) : (L - AttackLoopRewindS);
			const float Rest = FMath::Max(0.1f, RestClip / FMath::Max(0.1f, AttackTempoMul()));
			AttackAnimUntilS = Now + (double)Rest;   // гейт замаха живёт весь цикл
			GetWorld()->GetTimerManager().SetTimer(AttackLoopTimer, this, &AAsdaPlayerCharacter::OnAttackClipEnd, Rest, false);
			UE_LOG(LogAsdaPlayer, Log, TEXT("[бой] цикл атаки: клип=%d перемотка на %.2fс, проход %.2fс (взвод НЕ переигрывается)"),
				AttackLoopClipIdx, AttackLoopRewindS, Rest);
			ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Swing, TEXT("%s: КОНЕЦ прохода клипа %d → перемотка на %.3f с%s, проход №%d длится %.3f с, конец прохода %s, удержание %s, цель %s"),
				*AsdaCtPcWho(this), AttackLoopClipIdx, AttackLoopRewindS,
				bCarryOver ? *FString::Printf(TEXT(" (+перелёт %.0f мс)"), FMath::Clamp(Now - PrevUntilS, 0.0, 0.1) * 1000.0) : TEXT(""),
				AttackPassSerial, Rest, bEndIsSend ? TEXT("= точка 4028") : TEXT("без 4028"),
				AttackLoopHoldUntilS < 0.0 ? TEXT("до конца серии") : *FString::Printf(TEXT("ещё %.2f с"), AttackLoopHoldUntilS - Now),
				*AsdaCtPcTarget(AttackFaceTarget.Get()));
			// ★ 15.10.2026: удар ушёл на конце прохода — следующий отрезок по серверным часам. Если точка была внутри
			//   прохода (ближнее оружие), её таймер `AttackSendTimer` уже взведён с той же скоростью — не трогаем.
			if (bRhythmOwner && bEndIsSend) { BeginSendSegment(TEXT("конец прохода")); }
			// ★ Ф2: сосед — конец прохода, совпадающий с точкой отправки (дальнобойное, нечётное число шагов), — граница отрезка;
			//   следующий отрезок начинается в точке перемотки (кадр в ней — его, у баллисты это выстрел).
			const bool bPeerEndIsSend = EndingPts.Num() > 0 && EndingPts.Last() >= AttackLoopLenS - 1e-3f;
			if (bPeerPktDriven && bPeerEndIsSend) { BeginPeerSegment(TEXT("конец прохода"), /*bInclStart=*/true); }
			return;
		}
	}
	// Цикл кончился (удержание истекло / цели нет / смерть): выход из атак-клипа.
	ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Swing, TEXT("%s: КОНЕЦ прохода клипа — цикл замаха окончен (%s)%s, далее %s"),
		*AsdaCtPcWho(this),
		AttackLoopClipIdx < 0 ? TEXT("цикла нет — одиночный замах") : (AnimState == EAsdaPlayerState::Dead ? TEXT("мёртв")
			: (bHold ? TEXT("клип не найден") : TEXT("удержание истекло"))),
		IsAttackAnimActive() ? TEXT(", уже начат новый замах") : TEXT(""),
		IsAttackAnimActive() ? TEXT("клип не трогаем") : (IsActionClipOwned() ? TEXT("клип действия/эмоции не перебиваем")
			: (AnimState == EAsdaPlayerState::Idle ? (bCombatStance ? TEXT("покой idle01 (стойка)") : TEXT("покой"))
			: (AnimState == EAsdaPlayerState::Move ? TEXT("клип бега") : TEXT("клип состояния не меняем")))));
	// ★ 01.11.2026 (ревизия 4024/400): своё удержание цикла после смены цели на живого моба (`SetTarget` → `HoldAttackLoop(2 с)`)
	//   истекло, а новой атаки игрок не начал — у эталона такого выхода нет (выбор другого моба атаку не прерывает, а новая атака —
	//   прямая запись 5 без прохода через 0, entity_char.c:24029-24075), у порта атака кончается здесь: это переход 5 → 0, и сервер с
	//   соседями узнают о нём тем же 4024/400 (`CCharacter_SetAction` case 0, entity_char.c:16545-16556).
	if (IsLocalPlayerCharacter() && AttackLoopClipIdx >= 0 && AttackLoopHoldUntilS > 0.0 && AnimState != EAsdaPlayerState::Dead)
	{
		const AAsdaPlayerController* PCx = Cast<AAsdaPlayerController>(GetController());   // свой контроллер
		if (PCx && !PCx->bAutoAttack && !PCx->bPvpAttack) { AsdaAttackExit::SendAttackExit400(GetGameInstance(), TEXT("удержание цикла истекло без новой атаки")); }
	}
	AttackLoopClipIdx = -1; AttackLoopHoldUntilS = 0.0;
	AttackSyncRate = 0.f; GetWorld()->GetTimerManager().ClearTimer(AttackSendTimer);   // ★ 15.10.2026
	ResetPeerPktState();   // ★ Ф2
	if (IsAttackAnimActive()) { return; }   // уже начат новый замах
	// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Bow.NockLifecycle`): цикл замаха кончился сам (удержание истекло / цели нет) — переход 5 → 0,
	//   эталон `CCharacter_SetAction` case 0 → `clearActiveEffectSlots` (entity_char.c:16589): стрела на тетиве снимается.
	if (AsdaNockedArrow::LifecycleOn()) { AsdaNockedArrow::Hide(this, TEXT("цикл замаха окончен")); }
	// ★★★ Клип эмоции не перебиваем (владелец 2026-08-25: «длинные эмоции не доигрывают»).
	//   Сторож поймал подмену на 7.82 с из 13.33 — клип танца заменялся клипом текущего состояния.
	//   Законно прервать эмоцию может только смена состояния (движение/смерть) — она идёт через
	//   `SetAnimState`, который зовёт `StopEmotionClip`. Все ОСТАЛЬНЫЕ перезапуски клипа состояния
	//   (смена оружия, конец боевого цикла, крылья) обязаны эмоцию пропускать.
	// ★ В5: клип действия рыбалки, ★ 10.10.2026: клип копания 40/43 — тоже не перебиваем. ★ В3 S7: один предикат.
	if (IsActionClipOwned()) { return; }
	if (AnimState == EAsdaPlayerState::Idle)      { PlayWorldClip(IdleClipIdx(), true); }
	else if (AnimState == EAsdaPlayerState::Move) { PlayWorldClip(MoveClip(), true); }
}
