// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_HitShow.cpp — часть `AsdaWorldSubsystem_Hit.cpp`: попадание: вспышки, стрелы/снаряды, нотисы боя, ShowHitResult/ShowPlayerHit.
//   ★ R2 рефакторинга (10.11.2026): определения перенесены из `AsdaWorldSubsystem_Hit.cpp` ДОСЛОВНО (сверка `Tools/asda_split_check.py`);
//   общие помощники и объявления — `AsdaWorldSubsystemHitInternal.h`. Шапка системы и ссылки на эталон — в `AsdaWorldSubsystem_Hit.cpp` и заголовке класса.

#include "World/AsdaWorldSubsystem.h"
#include "UI/AsdaEntryWindows.h"   // ★ 22.10.2026: окна при своём 4055/4056 — список эталона вместо CloseAll
#include "UI/AsdaEscChain.h"      // ★ 25.10.2026: «кто ударил своего» (эталонное netWord54) для веток состояния ESC
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaNotice.h"              // ★ 27.09.2026: плашка 221 для отказов 4007 (канал обработчика `Recv_EntityMove`)
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» — единая точка своего номера (E2, 14.09.2026)
#include "World/AsdaMapRegistry.h"   // ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (путь DA_WalkGrid)
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: ключ отката ритма (темп предметов), кадры ударов своего замаха
#include "World/AsdaWalkGrid.h"
#include "Characters/AsdaCharAssembly.h"      // кости/attach-матрицы оружия «в руке ↔ за спиной»
#include "Camera/CameraComponent.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaHudSubsystem.h"             // HERO_NOTICE_WIN — только чужой редкий дроп (6131)
#include "UI/AsdaChatSubsystem.h"            // ★ системный чат CHAT_NEW_SYSTEM + оверлеи A/B — все лут-строки
// ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaFadeComponent.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "World/AsdaProjectileActor.h"       // ★ снаряды дальнобойного оружия (projectile_re.md)
#include "World/AsdaNockedArrow.h"           // ★ 05.11.2026: жизненный цикл стрелы на тетиве (снятие по событиям эталона)
#include "Materials/MaterialInterface.h"      // материал меша стрелы (у ..._1 ссылка битая)
#include "World/AsdaFloatingTextSubsystem.h" // цифры урона (порт CFloatingText)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "Skill/AsdaSkillFxSubsystem.h"      // ★★★ стык «урон ↔ визуал»: момент удара навыка
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
// тип оружия атакующего (импакт battk01..04)   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
// надетое (контейнер 3): проверка сломанных вещей   ★ R3: комментарий снятого включения "UI/AsdaInventorySubsystem.h" (файл им не пользовался) — сохранён
#include "Items/AsdaItemDbRows.h"            // ★ сырая запись skill_info (.sif +0x48 = buff0 состояния отражения)
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "AsdaBytes.h"                       // ★ единые читалки сырой записи (DataHex)
#include "Engine/DataTable.h"
#include "Resources/AsdaSoundSubsystem.h"    // звуки ударов (S_c_*normal1, фазовые звуки мобов)
#include "Components/MeshComponent.h"          // подошва цели: низ ВИДИМОЙ геометрии (TargetSoleZ)
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"          // ★ плашка имени — тоже UMeshComponent, но не тело
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "World/AsdaEntityText.h"           // ★ волна 3 п.14: имена/модели/звук-базы мобов и NPC — одна точка
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "World/AsdaWorldKind.h"            // ★ F7 (дедуп №2, В3): общие признаки «логин» / «загрузка»
#include "AsdaHeldTables.h"                  // FAsdaHeldTables — удержание таблиц спауна (15.09.2026, dt_gc_hold)
#include "World/AsdaLootActor.h"             // наземный лут (порт DropItemMgr, loot_system_re.md)
#include "World/AsdaPetActor.h"              // питомцы (6120, pet_system_re.md)
#include "World/AsdaSoulGuardianActor.h"     // ★ компаньон-хранитель «ангел» (6156, soul_guardian_companion_re.md)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "AsdaFxNames.h"                     // ★ дедуп №2 В1-C (F5/F6): теги эффектов персонажа, имена одиночных эффект-BP
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
#include "Text/AsdaTextSubsystem.h"          // loc-нотисы подбора (1011/1012/1030)
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Engine/TextureRenderTarget2D.h"   // RT эмблемы клана — это UTexture-наследник
#include "World/AsdaClanFlag.h"              // FAsdaClanFlagData::FromWire (флаг клана из 6542)
#include "Login/AsdaLoginData.h"             // DecodeCp1251 (имена монстров в cp1251)
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "World/AsdaBlendAnimInstance.h"     // ★ 29.10.2026: вздрагивание персонажа от удара моба (AsdaAnimSmooth::StartHitFlinch)
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "AsdaWorldSubsystemHitInternal.h"   // ★ R2: общие помощники и объявления частей AsdaWorldSubsystem_Hit*.cpp (после включений пролога)

// ── ★ Импакт-эффекты попадания (BP из JEB, asda_build_combat_impacts.py) ──
void UAsdaWorldSubsystem::SpawnImpact(const FString& BPName, const FVector& Loc, float Scale)
{
	UWorld* W = GetGameInstance()->GetWorld();
	if (!W) { return; }
	// ★ Аудит №3 (A10): спавн через ЕДИНУЮ точку. Контракт остаётся «готовое имя BP», а не jeb-база:
	//   импакты собираются `Tools/asda_build_combat_impacts.py` под РУЧНЫМИ именами (`BP_ImpactMob`,
	//   `BP_ImpactBattk01..04`), схема `BP_Fx_<jeb>` к ним неприменима без переименования ассетов.
	if (AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnByBPName(W, BPName, Loc, FRotator::ZeroRotator, nullptr))
	{
		// ★★★ Эталонный `CEffectMgr_scaleAll` импакта (перенос 2026-08-14, `skill_fx_scale.md` §12):
		//   раньше масштаб не выставлялся ВООБЩЕ, т.е. был 1.0 при любой цели.
		if (!FMath::IsNearlyEqual(Scale, 1.f)) { Fx->SetActorScale3D(FVector(Scale)); }
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F2): срок — `NaturalLifeSpan` импакта (эталон: эффект удара отдаётся
		//   менеджеру эффектов без хэндла гашения и живёт до конца своих эмиттеров). Было жёсткое 1,2 с («JEB ~0.5-1с» —
		//   оценка, не данные); по снимку `asda.Dedup.W2C.Life` у всех пяти импактов natural 1,0 с (контейнер 0,35–0,5 с endMode 0 — после него эффект и так не рисуется).
		Fx->SetLifeSpan(Fx->NaturalLifeSpan());
	}
}
// Материал стрелы (у меша ..._1 ссылка на материал битая — своей папки Materials у него нет;
//   текстура в оригинале ОДНА на обе модели: ch1_n_00_arrs001_00_00_0.TEX, сверено по .jbm).
static const TCHAR* kArrowMat      = TEXT("/Game/AsdaAssets/Models/Char/ch1_n_00_arrs001_00_00_0/Materials/ch1_n_00_arrs001_00_00_0.ch1_n_00_arrs001_00_00_0");
static const FName  kArrowBone(TEXT("8_b1"));          // эталон: pChar[52082] = List_FindIndexByName(model,"8_b1")
static const FName  kNockCompName(TEXT("AsdaNockedArrow"));
static const FName  kProjTrailTag(TEXT("ProjectileTrailFx"));

// Звук ВЫСТРЕЛА по типу оружия (эталон, ветки switch в Player_ProcessAttackTick).
static const TCHAR* ProjectileFireWav(int32 W)
{
	switch (W)
	{
	case 8: case 10: return TEXT("S_c_bnormal1");
	case 9:          return TEXT("S_c_cbnormal1");
	case 11:         return TEXT("S_c_balnormal1");
	default:         return nullptr;
	}
}

// Звук ПОПАДАНИЯ снаряда (эталон Combat_ProcessHitResultAndShowDamage: switch(weaponType-8) →
//   case 0 → bsnormal2, case 1/2 → cbnormal2, case 3 → balnormal3).
static const TCHAR* AsdaProjectileHitWav(int32 W)
{
	switch (W)
	{
	case 8:           return TEXT("S_c_bsnormal2");
	case 9: case 10:  return TEXT("S_c_cbnormal2");
	case 11:          return TEXT("S_c_balnormal3");
	default:          return nullptr;
	}
}

FVector UAsdaWorldSubsystem::ProjectileMuzzle(AActor* Attacker, bool& bFound) const
{
	bFound = false;
	if (!Attacker) { return FVector::ZeroVector; }
	const AAsdaPlayerCharacter* PCh = Cast<AAsdaPlayerCharacter>(Attacker);
	const USkeletalMeshComponent* Sk = (PCh && PCh->BaseSkel)
		? ToRawPtr(PCh->BaseSkel) : Attacker->FindComponentByClass<USkeletalMeshComponent>();
	if (Sk && Sk->GetBoneIndex(kArrowBone) != INDEX_NONE)
	{
		bFound = true;
		return Sk->GetBoneLocation(kArrowBone, EBoneSpaces::WorldSpace);
	}
	// Кости нет (превью/чужой скелет) — запасная точка на уровне груди, чтобы стрела не летела из ступней.
	FVector O, E; Attacker->GetActorBounds(false, O, E);
	return O;
}

// ★★★ 05.11.2026 [ИСПРАВЛЕНИЕ] Жизненный цикл стрелы на тетиве по событиям эталона (разбор — `World/AsdaNockedArrow.h`).
//   1 = стрела снимается сменой/уборкой оружия, выходом из атаки и смертью, а кладётся только при луке в руке; 0 = прежний порт
//   (снятие лишь таймером после серии по мобу — смена оружия посреди боя и PvP-серия оставляли стрелу в кисти навсегда).
static ASDA_DEV_CVAR(int32, CVarAsdaNockLifecycle,
	TEXT("asda.Bow.NockLifecycle"), 1,
	TEXT("1 (по умолчанию) = стрела на тетиве снимается сменой/уборкой оружия, выходом из атаки и смертью (эталон charEffectSlots_ClearSix / ")
	TEXT("clearActiveEffectSlots) и кладётся только при луке в руке; 0 = прежнее поведение порта (снятие только таймером после серии по мобу)."),
	ECVF_Default);

// Найти компонент стрелы на тетиве у персонажа (создаётся лениво `SetNockedArrow`); нет — nullptr.
static UStaticMeshComponent* AsdaFindNockComp(const AActor* Character)
{
	if (!Character) { return nullptr; }
	for (UActorComponent* C : Character->GetComponents())
	{
		if (C && C->GetFName() == kNockCompName) { return Cast<UStaticMeshComponent>(C); }
	}
	return nullptr;
}

bool AsdaNockedArrow::LifecycleOn()
{
	return CVarAsdaNockLifecycle.GetValueOnGameThread() != 0;
}

void AsdaNockedArrow::Hide(AActor* Character, const TCHAR* Why)
{
	UStaticMeshComponent* Nock = AsdaFindNockComp(Character);
	if (!Nock || !Nock->IsVisible()) { return; }
	Nock->SetVisibility(false);
	UE_LOG(LogAsdaWorld, Log, TEXT("[снаряд] стрела на тетиве снята у %s: %s"), *GetNameSafe(Character), Why ? Why : TEXT("-"));
}

bool AsdaNockedArrow::IsVisible(const AActor* Character)
{
	const UStaticMeshComponent* Nock = AsdaFindNockComp(Character);
	return Nock && Nock->IsVisible() && Nock->IsRegistered();
}

void UAsdaWorldSubsystem::SetNockedArrow(AActor* Attacker, bool bOn)
{
	AAsdaPlayerCharacter* PCh = Cast<AAsdaPlayerCharacter>(Attacker);
	if (!PCh || !PCh->BaseSkel) { return; }
	UStaticMeshComponent* Nock = AsdaFindNockComp(Attacker);
	if (!bOn)
	{
		if (Nock) { Nock->SetVisibility(false); }
		return;
	}
	// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Bow.NockLifecycle`): стрела кладётся только тому, у кого В РУКЕ лук (эталон кладёт её из ветки
	//   switch(equippedWeaponType) 8/0xA тика атаки — по ТЕКУЩЕМУ оружию) и кто жив. Выстрел/ответ, запланированный до смены оружия,
	//   приходит с подтипом прежнего лука — без этой проверки он клал стрелу персонажу уже с другим оружием (кость «8_b1» = кисть).
	if (AsdaNockedArrow::LifecycleOn())
	{
		const int32 Held = PCh->EquippedWeaponSubType;
		const bool bBowInHand = (Held == 8 || Held == 10) && !PCh->bWeaponMotionHidden;
		if (!bBowInHand || PCh->AnimState == EAsdaPlayerState::Dead)
		{
			if (Nock) { Nock->SetVisibility(false); }
			UE_LOG(LogAsdaWorld, Verbose, TEXT("[снаряд] стрела на тетиве НЕ положена %s: в руке подтип %d%s%s"), *GetNameSafe(Attacker), Held,
				PCh->bWeaponMotionHidden ? TEXT(", оружие убрано") : TEXT(""), PCh->AnimState == EAsdaPlayerState::Dead ? TEXT(", мёртв") : TEXT(""));
			return;
		}
	}
	if (!Nock)
	{
		if (PCh->BaseSkel->GetBoneIndex(kArrowBone) == INDEX_NONE)
		{
			UE_LOG(LogAsdaWorld, Warning, TEXT("[снаряд] кости '%s' нет в скелете — стрела на тетиве не показана"),
				*kArrowBone.ToString());
			return;
		}
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, kArrowMeshNock);
		if (!M) { return; }
		Nock = NewObject<UStaticMeshComponent>(Attacker, kNockCompName);
		Nock->SetStaticMesh(M);
		Nock->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Nock->SetCastShadow(false);
		// Эталон рендерит стрелу на тетиве ПРЯМО мировой матрицей кости «8_b1» ⇒ relative = identity.
		Nock->SetupAttachment(PCh->BaseSkel, kArrowBone);
		Nock->RegisterComponent();
	}
	Nock->SetVisibility(true);
}

float UAsdaWorldSubsystem::FireProjectile(AActor* Attacker, AActor* Target, int32 WeaponSubType,
	int32 AttackerUserIdx, int32 TargetRid, TFunction<void()> OnHit)
{
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W || !Attacker || !Target) { return 0.f; }

	bool bBone = false;
	const FVector From = ProjectileMuzzle(Attacker, bBone);
	// Цель ФИКСИРУЕТСЯ на момент выстрела (эталон: collisionTrace_Init получает готовые src/dst,
	//   самонаведения нет — сместившаяся цель всё равно получает урон, его решает сервер).
	FVector O, E; Target->GetActorBounds(false, O, E);
	const FVector To = O;

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.Owner = Attacker;
	AAsdaProjectileActor* P = W->SpawnActor<AAsdaProjectileActor>(AAsdaProjectileActor::StaticClass(), From, FRotator::ZeroRotator, SP);
	if (!P) { return 0.f; }
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, kArrowMeshFly))
	{
		P->ArrowMesh->SetStaticMesh(M);
		if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, kArrowMat))
		{
			P->ArrowMesh->SetMaterial(0, Mat);
		}
	}
	else
	{
		UE_LOG(LogAsdaWorld, Warning, TEXT("[снаряд] меш стрелы не найден: %s"), kArrowMeshFly);
	}
	// Шлейф — тот же .jeb, что и в оригинале; ассет собирается генератором эффектов персонажей.
	P->TrailFx = SpawnCharFx(kArrowTrailJeb, P, FVector::ZeroVector, kProjTrailTag);
	P->Launch(From, To, 2.5f);   // ЭТАЛОННАЯ скорость 2.5 юнита/мс

	if (OnHit)
	{
		const int32 WSub = WeaponSubType;
		TWeakObjectPtr<UAsdaWorldSubsystem> WeakThis = this;
		const FVector HitAt = To;
		P->OnArrive.BindLambda([OnHit, WeakThis, WSub, HitAt]()
		{
			OnHit();   // цифры + импакт (общий путь ShowHitResult)
			// Звук ПОПАДАНИЯ снаряда — только у дальнобойного оружия (эталон, отдельный switch).
			if (UAsdaWorldSubsystem* WS = WeakThis.Get())
			{
				if (const TCHAR* Wav = AsdaProjectileHitWav(WSub))
				{
					if (UAsdaSoundSubsystem* S = WS->GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>())
					{
						S->PlaySound3DAs(FName(Wav), HitAt, EAsdaSoundClass::Hit);   // ★ 30.10: класс «удар»
					}
				}
			}
		});
	}

	// Звук выстрела на позиции стрелка (эталон Audio_Play3DSound с soundPos персонажа).
	if (const TCHAR* FireWav = ProjectileFireWav(WeaponSubType))
	{
		if (UAsdaSoundSubsystem* S = GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>())
		{
			S->PlaySound3DAs(FName(FireWav), From, EAsdaSoundClass::Action);   // ★ 30.10: класс «действие» (выстрел)
		}
	}
	// Лук: стрела с тетивы ушла (эталон skillFireArrowProjectile: slot[124]=0, slot[255]=1).
	if (WeaponSubType == 8 || WeaponSubType == 10) { SetNockedArrow(Attacker, false); }

	UE_LOG(LogAsdaWorld, Log,
		TEXT("[снаряд] от userIdx=%d к цели=%d эффект=%s скорость=%.0f uu/с (тип оружия %d, дистанция %.0f, полёт %.2fс, старт %s %s)"),
		AttackerUserIdx, TargetRid, kArrowTrailJeb, P->SpeedUnitsPerMs * 1000.f,
		WeaponSubType, (float)(To - From).Size(), P->FlightTimeS,
		bBone ? TEXT("кость 8_b1") : TEXT("ЗАПАСНАЯ точка (кости 8_b1 нет)"), *From.ToCompactString());
	ASDA_CTRACE_IF(AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HitFrame, TEXT("ВЫСТРЕЛ снаряда: атакующий uidx=%d → цель %d (оружие %d), дистанция %.0f юн, полёт %.2f с — результат на прилёте"),
		AttackerUserIdx, TargetRid, WeaponSubType, (float)(To - From).Size(), P->FlightTimeS);
	return P->FlightTimeS;
}

// ── ★ Цифры урона (стили эталона; combat_floating_text_re.md) ──
// ★★★ БОЕВОЙ ЖУРНАЛ — ОДНА ТОЧКА НА ВЕСЬ БОЙ.
//   Эталон дублирует каждый результат удара строкой в чат, и делает это ТОЛЬКО когда действующее
//   лицо — локальный игрок (гейт `pAttackerChar->effectSlots[112].field_40`, у соседей строки нет,
//   есть только цифра). Набор строк одинаков в трёх местах эталона — `CCharacter_ResolveBasicAttack`
//   (entity_char.c:26900-27215), `Combat_ProcessHitResultAndShowDamage` (combat_pvp.c:1955-2260,
//   прилёт снаряда) и `Combat_ApplyDamage` (combat_pvp.c:7753-7975, удар моба по нам):
//       20011 уклонение цели      20012 нанесён урон (имя цели + число)
//       20013 расщеплённый урон    1092 неуязвимость          1159 промах
//       20014 уклонился МЫ         20015 промахнулся моб
//   Параметры вызова везде одни: цвет −1 (белый), TTL 10 000 мс, дорожка A — те же, что у 4102.
//   ⚠ До 24.08.2026 в порту эта строка была реализована ТОЛЬКО для отражения (20013). Обычная
//   атака не писала в журнал ничего — журнал боя оставался пустым.
void UAsdaWorldSubsystem::LogCombatNotice(int32 NoticeId, const FString& NameArg, int32 NumArg, bool bHasNum)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	UAsdaTextSubsystem* Txt = GI->GetSubsystem<UAsdaTextSubsystem>();
	UAsdaChatSubsystem* Chat = GI->GetSubsystem<UAsdaChatSubsystem>();
#if !UE_BUILD_SHIPPING
	++CombatNoticeCalls;   // ★ 17.10.2026 (Ф0): наблюдение самотеста — бой попросил строку в журнал
#endif
	if (!Txt || !Chat) { return; }
	TArray<FString> Names;  if (!NameArg.IsEmpty()) { Names.Add(NameArg); }
	TArray<int32>   Nums;   if (bHasNum)            { Nums.Add(NumArg); }
	const FString Line = Txt->FormatById(NoticeId, Names, Nums);
	if (Line.IsEmpty())
	{
		// Не молчим: пустая строка = нет записи в таблице текста, а не «эталон так делает».
		UE_LOG(LogAsdaWorld, Warning, TEXT("[журнал боя] нотис %d пуст в таблице текста — строка не пошла"), NoticeId);
		return;
	}
	Chat->AddSystemMessage(Line, UAsdaChatSubsystem::ColWhite(), EAsdaSysTrack::A, 10000.f);
}

void UAsdaWorldSubsystem::ShowHitResult(AAsdaMobActor* Target, int32 RawDmg, bool bOurAttack, int32 WeaponSubType, int32 HitIdx,
	int32 AttackerUserIdx, uint32 PlannedSeq)
{
	// ★★★ 17.10.2026 (синхронизация боя, Ф0) [ОРИГИНАЛ]: СТИЛИ ЦИФР СОСЕДА ПО МОБУ — 1/2/5/6, как у своих.
	//   Цель здесь всегда МОБ. Эталон показывает удар соседа по мобу в `Player_ProcessAttackTick` (entity_char.c:848–1056) теми же
	//   стилями, что и свой: урон 1, крит 2, MISS/IMMUNE 5, DODGE 6 — ветки «свой/чужой» там нет. Красные 9/0xA/0xD/0xE эталон
	//   рисует только по цели-ПЕРСОНАЖУ (`CCharacter_AttackAnimTick`, entity_char.c:1775, стр. 2103–2326 — PvP; в порту это путь
	//   `ShowPlayerHit`, он не меняется). Порт до 17.10 красил соседа по мобу красным — расхождение с оригиналом.
	//   Строка в журнал чата — по-прежнему только свои удары (`bOurAttack`). Ключ отката `asda.Combat.ObserverSync 0` — красные.
	const bool bObserverSync = CVarAsdaObserverSync.GetValueOnGameThread() != 0;   // ★ Ф0: наблюдатель по эталону
	const bool bMobStyles = bOurAttack || bObserverSync;   // стили «по мобу» 1/2/5/6 (иначе — прежние красные соседа)
#if !UE_BUILD_SHIPPING
	FAsdaHitResultSeen* Seen = nullptr;                    // ★ Ф0: запись наблюдения самотеста (только при `bRecordHitResults`)
	if (bRecordHitResults && Target)
	{
		FAsdaHitResultSeen& E = HitResultsSeen.AddDefaulted_GetRef();
		E.Rid = Target->NetRenderId; E.Raw = RawDmg; E.bOurs = bOurAttack; E.AttackerUserIdx = AttackerUserIdx;
		const UWorld* Ws = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;   // мир показа
		E.AtS = Ws ? Ws->GetTimeSeconds() : -1.0;
		Seen = &E;
	}
#endif
	// Вывод надписи с записью фактического стиля в наблюдение (один выход на все ветки ниже).
	// ★ 17.10.2026 (Ф5) [ОРИГИНАЛ]: удар соседа по мобу — цифра только при режиме показа 1 (entity_char.c:848–1056
	//   `CClass_GetField10112 == 1`); свой — всегда (режим 2 глушит сам `SpawnStyled`).
	const bool bShowText = ShouldShowDamageText(bOurAttack);
	auto Spawn = [&](int32 Style, const FString& Text, UAsdaFloatingTextSubsystem* F)
	{
		ASDA_CTRACE_IF(bOurAttack || AsdaCtWorld::Ours(Target ? Target->NetRenderId : -1, AttackerUserIdx), EAsdaCT::DmgText, TEXT("цифра «%s» стиль %d над %s (%s uidx=%d) — %s"), *Text, Style,
			*AsdaCtWorld::Mob(Target ? Target->NetRenderId : -1, Target), bOurAttack ? TEXT("свой удар") : TEXT("удар соседа"), AttackerUserIdx,
			bShowText ? TEXT("показана") : TEXT("СКРЫТА режимом показа цифр (DamageTextMode)"));
		if (!bShowText) { return; }   // надписи нет (−1 в наблюдении самотеста)
#if !UE_BUILD_SHIPPING
		if (Seen) { Seen->Style = Style; }
#endif
		F->SpawnOverActor(Style, Text, Target);
	};
	// ★★★ 18.10.2026 (ключ `asda.Combat.DeathAtLastPendingHit`) [ОРИГИНАЛ entity_char.c:848/861/904 — цифры гейтятся `target+480 != 10`]:
	//   удар по ТРУПУ позже смертельного удара этой смерти (метка `LethalMarks`) не показывается — ни цифры, ни вычета, ни флинча: моб
	//   лёг на ударе, который довёл полосу до 0, остаток очереди сервер не применял. ★ Ревизия 18.10 (п.1): у атакующего смертельного
	//   ответа — по ПОРЯДКУ ударов (номер после смертельного), а не по часам: перестановка таймеров своего замаха не гасит сам смертельный
	//   удар. Прочие удары по трупу — позже смерти больше чем на `kCorpseHitGraceS`. Смертельный удар показывается всегда (правило
	//   2026-08-25); метку снимают реген/респаун/снятие ленты — другой жизни гейт не касается.
	if (Target && Target->IsDead() && IsDeathAtLastPendingHit() && IsHitAfterLethal(Target, AttackerUserIdx, PlannedSeq))
	{
		const FAsdaLethalMark* Km = LethalMarks.Find(Target->NetRenderId);   // метка смертельного удара (журнал)
		ASDA_CTRACE_IF(bOurAttack || AsdaCtWorld::Ours(Target->NetRenderId, AttackerUserIdx), EAsdaCT::DmgText, TEXT("%s: удар %s (h=%d, uidx=%d, №%u) по ТРУПУ позже смертельного №%u — НЕ показан (эталон: цифр по состоянию 10 нет)"),
			*AsdaCtWorld::Mob(Target->NetRenderId, Target), *AsdaCtWorld::Hit(RawDmg), HitIdx, AttackerUserIdx, PlannedSeq, Km ? Km->Seq : 0u);
#if !UE_BUILD_SHIPPING
		if (Seen) { Seen->bOnCorpse = true; }
#endif
		return;
	}
	// ★ 17.10.2026 (ревизия правки боя, п.4): фактический момент показа результата — опора самотеста кадра удара.
	if (Target)
	{
		const UWorld* Wh = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;   // мир показа
		LastHitResultShownS = Wh ? Wh->GetTimeSeconds() : -1.0;
		LastHitResultShownRid = Target->NetRenderId;
		++HitResultsShown;
	}
	UAsdaFloatingTextSubsystem* FT = GetGameInstance()->GetSubsystem<UAsdaFloatingTextSubsystem>();
	if (!FT || !Target)
	{
		ASDA_CTRACE_IF(bOurAttack || AsdaCtWorld::Ours(Target ? Target->NetRenderId : -1, AttackerUserIdx), EAsdaCT::Damage, TEXT("результат удара %s (h=%d, uidx=%d) НЕ показан: %s"), *AsdaCtWorld::Hit(RawDmg), HitIdx, AttackerUserIdx,
			Target ? TEXT("нет подсистемы цифр") : TEXT("цели нет"));
		return;
	}
	ASDA_CTRACE_IF(bOurAttack || AsdaCtWorld::Ours(Target ? Target->NetRenderId : -1, AttackerUserIdx), EAsdaCT::Damage, TEXT("УДАР h=%d по %s от %s uidx=%d (оружие %d): %s, HP полосы до %d/%d%s"),
		HitIdx, *AsdaCtWorld::Mob(Target->NetRenderId, Target), bOurAttack ? TEXT("своего") : TEXT("соседа"), AttackerUserIdx, WeaponSubType,
		*AsdaCtWorld::Hit(RawDmg), Target->CurHp, Target->MaxHp, Target->IsDead() ? TEXT(" (цель уже труп)") : TEXT(""));
	// ★ Эталон entity_char.c:26912-26965 (switch по значению из очереди), стили FloatingText_Spawn:
	//     -1 → DODGE(6);  -2 и 0 → НИЧЕГО (`dmgFromQueue != -2 && dmgFromQueue`);
	//     -3/-4/-5 → IMMUNE(5);  -6 → MISS(5);  урон(1), крит бит31(2).
	//   Чужой бой (CCharacter_AttackAnimTick): DODGE 0xE, MISS/IMMUNE 0xD, урон 9, крит 0xA.
	//   Раньше сюда попадало «всё <= 0 = MISS», из-за чего -2 (пусто) рисовал ложный MISS.
	// ★★★ КОДЫ −2 И 0 — ЭТО «MISS», И ЭТАЛОН ЕГО РИСУЕТ.
	//   ⚠ ГРАБЛИ, НАСТУПАЛ 24.08.2026: я прочитал у эталона только условие
	//   `else if ( dmgFromQueue != -2 && dmgFromQueue )` и заключил, что −2 не показывается ничем.
	//   Это ошибка чтения — сразу ЗА этой веткой стоит замыкающий `else`, который и рисует «MISS»:
	//       entity_char.c:27207  else if ( target->…field_8 != 6 ) { …"MISS"… нотис 1159 }
	//   Тот же замыкающий `else` есть у удара моба по игроку — combat_pvp.c:7953, стиль 0xD,
	//   нотис 20015. Значит в него проваливаются ОБА исключённых кода: и −2, и 0.
	//   Третье подтверждение: в обработчике прилёта снаряда −2 вынесен в ЯВНЫЙ `case 0xFFFFFFFE`
	//   с тем же «MISS» (combat_pvp.c:1988).
	//   ★ Смысл −2 со стороны сервера: NpcServer `sub_45CEC0@0x45CEC0` ставит его, когда у
	//   атакующего не прошла проверка меткости (`if ((char)v75 >= rand()) … else a5[i] = -2;`),
	//   то есть −2 = ПРОМАХ. «MISS» — ровно то, что нужно показать.
	if (RawDmg == -2 || RawDmg == 0)
	{
		if (CVarShowMissOnCode2.GetValueOnGameThread() != 0)
		{
			Spawn(bMobStyles ? 0x5 : 0xD, TEXT("MISS"), FT);
			if (bOurAttack) { LogCombatNotice(1159); }
		}
		else
		{
			ASDA_CTRACE_IF(bOurAttack || AsdaCtWorld::Ours(Target ? Target->NetRenderId : -1, AttackerUserIdx), EAsdaCT::DmgText, TEXT("промах %d по %s — надпись MISS выключена ключом asda.Combat.ShowMissOnCode2 0"), RawDmg,
				*AsdaCtWorld::Mob(Target->NetRenderId, Target));
		}
		return;
	}
	if (RawDmg == -1)
	{
		Spawn(bMobStyles ? 0x6 : 0xE, TEXT("DODGE"), FT);
		if (bOurAttack) { LogCombatNotice(20011, Target->MobName); }
	}
	else if (RawDmg == -6)
	{
		Spawn(bMobStyles ? 0x5 : 0xD, TEXT("MISS"), FT);
		if (bOurAttack) { LogCombatNotice(1159); }
	}
	else if (RawDmg >= -5 && RawDmg <= -3)
	{
		Spawn(bMobStyles ? 0x5 : 0xD, TEXT("IMMUNE"), FT);
		if (bOurAttack) { LogCombatNotice(1092); }
	}
	// ★★★ КОД −7. Эталон обрабатывает его в `Skill_SpawnHitFeedback` (skill_buff.c:445-456):
	//       case -7: if ( targetActionState != 10 )            // не по трупу
	//                { FloatingText(стиль 5, "MISS"); нотис 1032 в чат; }
	//   ⚠ А в ветке ОБЫЧНОЙ атаки (`CCharacter_ResolveBasicAttack`) case −7 НЕТ ВОВСЕ: значение
	//   проваливается в `default: if ( dmgFromQueue >= -1 )`, условие ложно — и не рисуется ничего.
	//   Это дефект оригинала (молчаливая потеря), чиним по более полной ветке.
	//   ⚠⚠ У НАС было хуже: −7 уходил в ветку урона, где бит 31 отрицательного числа читается как
	//   крит, и над целью печаталось `0x7FFFFFF9` = 2 147 483 641.
	else if (RawDmg == -7)
	{
		if (!Target->IsDead())   // эталонный гейт «цель не в состоянии 10»
		{
			Spawn(bMobStyles ? 0x5 : 0xD, TEXT("MISS"), FT);
			if (bOurAttack) { LogCombatNotice(1032); }
		}
		else
		{
			ASDA_CTRACE_IF(bOurAttack || AsdaCtWorld::Ours(Target ? Target->NetRenderId : -1, AttackerUserIdx), EAsdaCT::DmgText, TEXT("код −7 по трупу %s — надписи нет (эталонный гейт состояния 10)"), *AsdaCtWorld::Mob(Target->NetRenderId, Target));
		}
	}
	else
	{
		const bool bCrit = (RawDmg & 0x80000000u) != 0;
		const int32 Dmg  = RawDmg & 0x7FFFFFFF;
		Spawn(bCrit ? (bMobStyles ? 0x2 : 0xA) : (bMobStyles ? 0x1 : 0x9), FString::FromInt(Dmg), FT);
		// Эталон пишет строку урона и на обычном, и на критическом ударе — нотис один (20012).
		if (bOurAttack) { LogCombatNotice(20012, Target->MobName, Dmg, /*bHasNum=*/true); }
		// ★ HP цели уменьшается ЛОКАЛЬНО НА КАЖДОМ УДАРЕ (эталон CCharacter_TakeDamage: HP -= dmg сразу).
		//   Пакетный HP (4029 @+4) остаётся коррекцией — без этого полоса «неотзывчива» (юзер 2026-07-29).
		ASDA_CTRACE_IF(bOurAttack || AsdaCtWorld::Ours(Target ? Target->NetRenderId : -1, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: HP полосы %d → %d / %d — урон %d%s на кадре удара (%s uidx=%d)"),
			*AsdaCtWorld::Mob(Target->NetRenderId, Target), Target->CurHp, FMath::Max(0, Target->CurHp - Dmg), Target->MaxHp, Dmg,
			bCrit ? TEXT(" КРИТ") : TEXT(""), bOurAttack ? TEXT("свой") : TEXT("сосед"), AttackerUserIdx);
		// ★★★ 18.10.2026 (ключ `asda.Combat.HpFloorFromServer`, разбор у объявления): полоса не ниже пола (последнее HP 4029 —
		//   сервер списывает очередь постепенно, цифры свои показывают её быстрее); упёрлась — цифра та же, полоса стоит на поле.
		{
			const int32 FloorHp = IsHpFloorOn() ? MobHpFloorValue(Target->NetRenderId) : 0;   // пол полосы (0 — неизвестен)
			const int32 Free = FMath::Max(0, Target->CurHp - Dmg);                             // без пола
			const int32 NewBar = FMath::Min(Target->CurHp, FMath::Max(Free, FloorHp));         // пол не поднимает полосу
			if (NewBar != Free)
			{
				ASDA_CTRACE_IF(bOurAttack || AsdaCtWorld::Ours(Target->NetRenderId, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: удар: полоса %d→%d (упёрлась в пол %d; без пола было бы %d)"),
					*AsdaCtWorld::Mob(Target->NetRenderId, Target), Target->CurHp, NewBar, FloorHp, Free);
			}
			Target->CurHp = NewBar;
		}
		NoteMobHpShown(Target->NetRenderId, Dmg);   // ★ 05.11.2026 (asda.Combat.LiftLargeGap): учёт показанного урона
		NoteMobCombat(Target->NetRenderId);
		UpdateMobHpBar(Target);
		// ★ 17.10.2026 (Ф1): попадание показано — его урон вычитается из записи ленты ЭТОГО атакующего (проект §3.3).
		NoteImpactShown(Target->NetRenderId, AttackerUserIdx, Dmg);
		// ★ Реакция цели на удар: разворот к бьющему + хит-флинч + звук (эталон SetActionState(13)).
		FVector2D AtkGame = FVector2D::ZeroVector;
		if (bOurAttack)
		{
			if (const AActor* Me = LocalPlayerActor) { AtkGame = AsdaCoord::UeToGame2D(Me->GetActorLocation()); }   // ★ Аудит №3 (B1)
		}
		// ★ 17.10.2026 (Ф0, Р8) [ПОРТ]: удар СОСЕДА разворачивает моба к соседу (раньше `AtkGame = 0` — флинч без поворота, моб
		//   «не замечал» чужого бьющего). Атакующий — из реестра соседей по userIdx пакета 4029/4027. Ключ отката — ObserverSync 0.
		else if (bObserverSync && AttackerUserIdx >= 0)
		{
			if (const AActor* Atk = FindPlayerActor(AttackerUserIdx)) { AtkGame = AsdaCoord::UeToGame2D(Atk->GetActorLocation()); }
		}
#if !UE_BUILD_SHIPPING
		if (Seen) { Seen->bFlinchFaced = !AtkGame.IsNearlyZero() && !Target->IsDead() && !Target->IsAttackAnimActive(); }
#endif
		Target->NetHitFlinch(AtkGame);
		// Импакт удара на цели (эталон: спавн на позиции цели при показе урона).
		// ★★★ МАСШТАБ ИМПАКТА (перенос 2026-08-14, `skill_fx_scale.md` §12). Эталон в ЭТОЙ ветке —
		//   `entity_char.c:936` (`CCharacter_AttackAnimTick@0x9A2200`, чужая атака) и
		//   `combat_pvp.c:2149/2253/2267/…` (`Combat_ShowDamageResultText@0xA56930`, наша) — зовёт
		//   `CEffectMgr_scaleAll(sub_936A29(цель)/100)`, потому что ЦЕЛЬ ЗДЕСЬ ВСЕГДА МОБ
		//   (сигнатура метода: `AAsdaMobActor* Target`). Для цели-ПЕРСОНАЖА тот же эталон ставит
		//   константу 1.5 — она уже реализована в `GetBasicAttackHitFxScale` и включится сама,
		//   когда в порту появится обычная атака ПЕРСОНАЖ→ПЕРСОНАЖ (сейчас такого пути нет).
		FVector O, E; Target->GetActorBounds(false, O, E);
		SpawnImpact(BasicAttackImpactBP(WeaponSubType, HitIdx), O,
			UAsdaSkillFxSubsystem::GetBasicAttackHitFxScale(Target));
		ASDA_CTRACE_IF(bOurAttack || AsdaCtWorld::Ours(Target ? Target->NetRenderId : -1, AttackerUserIdx), EAsdaCT::Fx, TEXT("%s: флинч%s, импакт %s (масштаб %.2f)%s"), *AsdaCtWorld::Mob(Target->NetRenderId, Target),
			AtkGame.IsNearlyZero() ? TEXT(" без разворота") : TEXT(" с разворотом к бьющему"), BasicAttackImpactBP(WeaponSubType, HitIdx),
			UAsdaSkillFxSubsystem::GetBasicAttackHitFxScale(Target), IsRangedWeapon(WeaponSubType) ? TEXT(", звук — по прилёту снаряда") : TEXT(""));
		// ★★★ ЗВУК НА КАДРЕ УДАРА — ЭТО ЗВУК ПОПАДАНИЯ, А НЕ ЗАМАХА.
		//   ⚠ ИСПРАВЛЕНО 24.08.2026. Здесь играл `S_c_*normal1` — но это звук ЗАМАХА, и в эталоне
		//   он звучит в другом месте и в другой момент: `Player_ProcessAttackTick`
		//   (entity_char.c:788-812), 3D у АТАКУЮЩЕГО, под гейтом «на этом шаге ещё не играли»
		//   (`field_398`). Его перенос — в `AAsdaPlayerCharacter` (см. PlaySwingSound).
		//   На кадре удара эталон играет ДВЕ РАЗНЫЕ таблицы 2D-звуков (skill_buff.c:566-583
		//   обычный и :706-724 критический):
		//       оружие 0    → S_c_sdamage   / S_c_scri
		//       оружие 1,4  → S_c_lsdamage  / S_c_lscri
		//       оружие 2    → S_c_spnormal2 / S_c_spcri
		//       оружие 3    → S_c_gsdamage  / S_c_gscri
		//       оружие 5    → S_c_stnormal2 / S_c_stcri
		//       прочее      → НИЧЕГО (эталон уходит на LABEL_133)
		//   ★ Приставка у оружия 0 РАЗНАЯ: замах `ss`, попадание `s` — одной таблицей не обойтись.
		//   ★ На КРИТЕ эталон дополнительно играет 3D-звук попадания по мобу
		//     (`Sound_BuildEntryWavName(mobTypeId, 8)`), у нас это `PlayMobSound(8)`.
		//   Дальнобойное оружие сюда не попадает: у него звук по прилёте снаряда.
		if (!IsRangedWeapon(WeaponSubType))
		{
			const TCHAR* HitSnd = nullptr;
			const TCHAR* CriSnd = nullptr;
			switch (WeaponSubType)
			{
			case 0:          HitSnd = TEXT("S_c_sdamage");   CriSnd = TEXT("S_c_scri");  break;
			case 1: case 4:  HitSnd = TEXT("S_c_lsdamage");  CriSnd = TEXT("S_c_lscri"); break;
			case 2:          HitSnd = TEXT("S_c_spnormal2"); CriSnd = TEXT("S_c_spcri"); break;
			case 3:          HitSnd = TEXT("S_c_gsdamage");  CriSnd = TEXT("S_c_gscri"); break;
			case 5:          HitSnd = TEXT("S_c_stnormal2"); CriSnd = TEXT("S_c_stcri"); break;
			default: break;   // эталон: у прочих типов звука на кадре удара нет
			}
			// ★★★ [ОРИГИНАЛ] 30.10.2026: таблица выше и 2D — ТОЛЬКО свой удар. Чужой удар эталон играет 3D В ПОЗИЦИИ ЦЕЛИ своей
			//   таблицей (`Player_ProcessAttackTick` entity_char.c:944-956, крит :1030-1042; `CCharacter_AttackAnimTick`
			//   :2221-2236 / :2303-2316): оружие 0/1/3/4 → S_c_sdamage / S_c_scri, 2 → S_c_spnormal2 / S_c_spcri,
			//   5 → S_c_stnormal2 / S_c_stcri. Прежде порт играл 2D-таблицу своего удара на любой удар — бой далёких игроков
			//   звучал в ухо на полной громкости (ключ `asda.Sound.ForeignHit3D`, разбор — AsdaSoundSubsystem.cpp).
			const bool bForeign3D = !bOurAttack && UAsdaSoundSubsystem::ForeignHit3DOn();
			if (bForeign3D)
			{
				switch (WeaponSubType)
				{
				case 0: case 1: case 3: case 4: HitSnd = TEXT("S_c_sdamage");   CriSnd = TEXT("S_c_scri");  break;
				case 2:                         HitSnd = TEXT("S_c_spnormal2"); CriSnd = TEXT("S_c_spcri"); break;
				case 5:                         HitSnd = TEXT("S_c_stnormal2"); CriSnd = TEXT("S_c_stcri"); break;
				default:                        HitSnd = nullptr;               CriSnd = nullptr;           break;
				}
			}
			const TCHAR* Pick = bCrit ? CriSnd : HitSnd;
			if (Pick)
			{
				if (UAsdaSoundSubsystem* Snd = GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>())
				{
					if (bForeign3D) { Snd->PlaySound3DAs(FName(Pick), O, EAsdaSoundClass::Hit, Target); }
					else            { Snd->PlaySound2D(FName(Pick)); }
				}
			}
			if (bCrit) { Target->PlayMobSound(8); }   // эталон: 3D-звук попадания по мобу на крите
		}
	}
}

// ── ★ Удар моба по игроку — «кадр удара» (эталон Combat_ApplyDamage@0xF7C470): цифры над жертвой
//   (стили CombatText_DamageLocal) + импакт eff_0_mon_99_battk01 на позиции жертвы. ──
void UAsdaWorldSubsystem::ShowPlayerHit(AActor* Victim, int32 Damage, uint8 HitType, int32 VictimUserIdx,
	const FString& AttackerName)
{
	UAsdaFloatingTextSubsystem* FT = GetGameInstance()->GetSubsystem<UAsdaFloatingTextSubsystem>();
	if (!FT || !Victim) { return; }
	// ★★★ КОДЫ РЕЗУЛЬТАТА УДАРА — ОДНА ТАБЛИЦА НА ВЕСЬ ПОРТ (сверено по ДВУМ независимым функциям
	//   эталона: `Player_ProcessAttackTick` entity_char.c:26912-26980 — наш удар по мобу, и
	//   `Combat_ApplyDamage` combat_pvp.c:7753-7772 — удар моба по нам; таблица в обеих одинакова):
	//       >0        урон (бит 31 = крит)
	//       -1        УКЛОНЕНИЕ → «DODGE» + запись в чат (нотис 20011 / 20014)
	//       -2        ПРОМАХ атакующего (не прошла проверка меткости в NpcServer `sub_45CEC0`)
	//                 → «MISS» замыкающим `else` (НЕ «ничего»: см. разбор ниже)
	//        0        тоже «MISS» — проваливается в тот же замыкающий `else`
	//       -3        НЕУЯЗВИМОСТЬ → «IMMUNE» + нотис 1092
	//       -4, -5    НЕУЯЗВИМОСТЬ → «IMMUNE» без записи в чат
	//       -6        ПРОМАХ → «MISS» + нотис 1159
	//        0        показывать нечего
	//   ⚠ ИСПРАВЛЕНО 24.08.2026: здесь `-2` и `0` рисовали «MISS». Это отсебятина — у промаха свой
	//   код `-6`, у уклонения свой `-1`, и оба мы рисуем. Ложный «MISS» на пустом коде вводил в
	//   заблуждение при разборе боя.
	// Имя АТАКУЮЩЕГО (моба) — эталон кладёт в слот именно его: `CClass_GetResourceFromField(pCaster)`.
	// Строку в журнал пишем ТОЛЬКО когда бьют НАС (эталонный гейт `field_40` = локальный игрок).
	UAsdaNetSubsystem* NetJ = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	const bool bVictimIsMe = NetJ && NetJ->IsSelfUserIdx(VictimUserIdx);
	const FString& AtkName = AttackerName;
	// ★ 17.10.2026 (Ф5) [ОРИГИНАЛ]: удар моба по ЧУЖОМУ — надпись только при режиме показа 1 (`Combat_ApplyDamage`,
	//   combat_pvp.c:7998/8010 `CClass_GetField10112 == 1`); по мне — всегда (режим 2 глушит сам `SpawnStyled`).
	const bool bShowTxt = ShouldShowDamageText(bVictimIsMe);
	ASDA_CTRACE_UID(VictimUserIdx, EAsdaCT::MobAttack, TEXT("удар моба '%s' по игроку uidx=%d%s: %s, тип удара %d — надпись %s"),
		*AtkName, VictimUserIdx, bVictimIsMe ? TEXT(" (я)") : TEXT(""), *AsdaCtWorld::Hit(Damage), (int32)HitType,
		bShowTxt ? TEXT("показывается") : TEXT("СКРЫТА режимом показа цифр"));
	if (Damage == -1)
	{
		if (bShowTxt) { FT->SpawnOverActor(0xE, TEXT("DODGE"), Victim); }
		if (bVictimIsMe) { LogCombatNotice(20014, AtkName); }   // мы уклонились
		return;
	}
	//   ★ −2 и 0 → «MISS» (замыкающий `else` в `Combat_ApplyDamage`, combat_pvp.c:7953, нотис 20015).
	if (Damage == -2 || Damage == 0)
	{
		if (CVarShowMissOnCode2.GetValueOnGameThread() != 0)
		{
			if (bShowTxt) { FT->SpawnOverActor(0xD, TEXT("MISS"), Victim); }
			if (bVictimIsMe) { LogCombatNotice(20015, AtkName); }   // моб промахнулся
		}
		return;
	}
	if (Damage >= -5 && Damage <= -3) { if (bShowTxt) { FT->SpawnOverActor(0xD, TEXT("IMMUNE"), Victim); } return; }
	// ★★★ КРИТ МОБА ОПОЗНАЁТСЯ ПО БИТУ 31 ЗНАЧЕНИЯ, а не по типу удара.
	//   Эталон `Combat_ApplyDamage` (ветка B, combat_pvp.c:7772 против :7856-7866):
	//       if ( value >= -1 ) { dmg = value & 0x7FFFFFFF;
	//                            CombatText_DamageLocal(…, ТИП 0, dmg, …); звук фазы 5+вариант }
	//       else              { dmg = value & 0x7FFFFFFF;
	//                            CombatText_DamageLocal(…, ТИП 1, dmg, …); звук фазы 7 }
	//   Сравнение ЗНАКОВОЕ — отрицательное значение (бит 31) и есть крит, ровно как у наших ударов.
	//   ⚠ ДЕФЕКТ ОРИГИНАЛА (D6): в «мгновенной» ветке A (`Recv_CombatHit` case 3, network.c:37680)
	//   тип жёстко 0 — крит моба, пришедший, пока моб во флинче, выглядел обычным ударом. Мы
	//   показываем крит одинаково в обеих ветках: одна и та же ситуация не должна выглядеть
	//   по-разному в зависимости от того, был моб в бою или нет.
	const bool bMobCrit = (Damage & 0x80000000u) != 0;
	const int32 Dmg = Damage & 0x7FFFFFFF;
	int32 Style = 0x9;
	switch (HitType)
	{
	case 0: Style = 0x9; break;
	case 1: Style = 0xA; break;
	case 4: case 5: Style = 0x11; break;
	case 16: if (bShowTxt) { FT->SpawnOverActor(0x10, TEXT("IMMUNE"), Victim); } return;
	default: Style = 0x10; break;   // 2/3/6/7/8
	}
	if (bMobCrit) { Style = 0xA; }   // крит перекрывает стиль по типу удара
	if (bShowTxt) { FT->SpawnOverActor(Style, FString::FromInt(Dmg), Victim); }   // ★ Ф5
	ASDA_CTRACE_UID(VictimUserIdx, EAsdaCT::DmgText, TEXT("цифра «%d» стиль 0x%X над игроком uidx=%d%s (удар моба '%s'%s) — %s; урон уходит в HP (ApplyVitalsDamage, канал «урон 4015 kind=3»)"),
		Dmg, Style, VictimUserIdx, bVictimIsMe ? TEXT(" (я)") : TEXT(""), *AtkName, bMobCrit ? TEXT(", КРИТ") : TEXT(""),
		bShowTxt ? TEXT("показана") : TEXT("скрыта режимом"));
	// ★★★ Урон идёт в ЕДИНЫЙ ИСТОЧНИК ИСТИНЫ, а не прямо в плашку. Эталон CCharacter_TakeDamage
	//   вычитает HP локально на кадре удара, а серверный снапшот 4058 — коррекция. Прежний код писал
	//   прямо в неймплейт, минуя сеть: значение упиралось в 0 и жило своей жизнью, пока сервер
	//   продолжал наносить урон (жалоба юзера 2026-08-04, доказательство в логе 05:58:44 → 05:59:05).
	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		int32 Uidx = VictimUserIdx;
		// `FindUserIdxByActor` сам отдаёт SelfUserNum для своего павна — отдельной ветки не нужно.
		if (Uidx < 0) { Uidx = FindUserIdxByActor(Victim); }
		Net->ApplyVitalsDamage(Uidx, Dmg, TEXT("урон 4015 kind=3"));
	}
	// ★ МАСШТАБ ЗДЕСЬ 1.0 — И ЭТО ЭТАЛОН, а не пропуск: `Combat_ApplyDamage@0xF7C470` спавнит
	//   `eff_0_mon_99_battk01-a_00.jeb` через `CEffectMgr_CreateEffect_F561F0` и `CEffectMgr_scaleAll`
	//   после него НЕ зовёт (проверено сплошной описью всех 29 вызовов `scaleAll` в срезе — этого
	//   спавна среди них нет). Поэтому параметр не передаём.
	FVector O, E; Victim->GetActorBounds(false, O, E);
	// ★ 29.10.2026 [ОРИГИНАЛ]: крит моба (бит 31) — `eff_0_mon_99_cattk01-a_00.jeb` вместо battk01 (combat_pvp.c:7883); ключ asda.Char.HitVictimFx.
	SpawnImpact((bMobCrit && AsdaHitVictimFxOn()) ? TEXT("BP_ImpactMobCrit") : TEXT("BP_ImpactMob"), O);
}
