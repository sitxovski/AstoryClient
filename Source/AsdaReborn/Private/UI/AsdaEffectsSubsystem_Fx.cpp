// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectsSubsystem_Fx.cpp — ★ R3-c 12.11.2026 (разрез AsdaEffectsSubsystem.cpp, дословный перенос): эффекты: предмет, титул, эмоции, ускорение, PM; эффекты бафов (таблица, программа, показ).
//   Общее для частей — в AsdaEffectsSubsystem_Private.h; шапка и реверс — в AsdaEffectsSubsystem.cpp.
#include "UI/AsdaEffectsSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Data/AsdaFxLoopRows.h"   // ★ дедуп №2 В2-C (F3): AsdaFxLoop::kTablePath
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт

#include "AsdaEffectActor.h"
#include "AsdaFxDeadHost.h"   // ★ R5-fix5 (13.11.2026): метка «гаснуть вместе с умершим мобом-носителем»
#include "Resources/AsdaSoundSubsystem.h"
#include "Skill/AsdaSkillFxSubsystem.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Npc/AsdaMobActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "World/AsdaCombatTrace.h"          // журнал боя: эффекты состояний (наложены/сняты/истекли)
#include "AsdaEffectsSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaEffectsSubsystem*.cpp

// ═════════════════════════════ мелкие помощники ════════════════════════════════════════════════
// ★ Аудит №3 (A5): своя пара `AsdaHexByte` + свой `Nib` УБРАНЫ — байт сырой записи из колонки
//   `DataHex` читает общая точка `AsdaBytes::HexByteAt` (Public/AsdaBytes.h).
//   ⚠ РАЗНИЦА В КОНТРАКТЕ, проверена по каждому вызывающему: прежняя копия на не-hex символе и на
//     обрыве строки молча отдавала 0, общая точка отдаёт −1 («поля нет»). Ни один здешний потребитель
//     от этого не меняет ветку — сверка в комментариях у самих чтений.
// Загрузка DataTable по пути (для таблиц ПОРТА, которых нет в item-БД: DT_BuffFx).
//   ★ Аудит №3 (A3): все таблицы item-БД грузит `UAsdaItemTableSubsystem`, второй загрузки больше нет.
//   ★ Аудит №4 (B2): загрузка идёт через общую точку `AsdaDt::Cached` — здесь остаётся тонкий
//   форвард (имя `AsdaLoadDT` читается в местах загрузки ниже). Раньше эта функция грузила
//   таблицу заново на КАЖДЫЙ вызов — кэша не было.
static UDataTable* AsdaLoadDT(const TCHAR* Path)
{
	return AsdaDt::Cached(Path);
}

// ★★★ Разовый эффект применения предмета — см. развёрнутый комментарий в заголовке.
AActor* UAsdaEffectsSubsystem::SpawnItemUseFx(int32 UserIdx, const FString& JebBase, int32 SoundId,
                                              const FString& SoundName)
{
	if (JebBase.IsEmpty() || UserIdx < 0) { return nullptr; }
	USkeletalMeshComponent* Skel = nullptr;
	// Персонаж ищется по userNum — `CharManager_FindByUserIdx@0x9D1240` ([[charid-vs-usernum]]:
	//   в 5029 по смещению +1 лежит именно userNum (i16), а charId (i32) на +3 клиент не читает).
	AActor* Host = ResolveHost(UserIdx, /*bMob=*/false, Skel);
	if (!Host)
	{
		UE_LOG(LogAsdaBuff, Log,
			TEXT("[предмет-визуал] персонажа userIdx=%d нет в мире — эффект %s не показан ")
			TEXT("(эталон в `OnRecv_UseItemResult` при `!targetChar` тоже выходит)"), UserIdx, *JebBase);
		return nullptr;
	}
	UWorld* W = Host->GetWorld();
	if (!W) { return nullptr; }

	// ★ ЭТАЛОННАЯ ПОПРАВКА ВЫСОТЫ attach-5 (`effectNodeTrackTargetChar@0xF58730`): хендлер каждый кадр
	//   кладёт `pos = charPos; pos.y += 15.0` — то есть **+15 по игровой Y = +15 по UE Z**. Канал
	//   эффектов СОСТОЯНИЙ её уже применял (`BuffFxTrackRiseZ`), а канал ПРЕДМЕТОВ — нет: это
	//   классический дефект-в-копии ([[port-shared-points]]). Зелья и Камень души идут именно
	//   attach-5 (`item_inventory.c:39105/39125/39147` — последний-но-один аргумент `Effect_SpawnByName` = 5).
	//   ★ Дедуп №2 В1-C (F12): режим и подъём — из ОБЩЕЙ таблицы кодов эталона (код 5 = позиция носителя + 15).
	EAsdaFxAttachMode Mode = EAsdaFxAttachMode::CharPos;   // режим привязки кода 5
	float RiseZ = 0.f;                                     // подъём кода 5
	AAsdaEffectActor::EtalonAttach(5, 0, BuffFxTrackRiseZ, Mode, RiseZ);
	const FVector At = Host->GetActorLocation() + FVector(0.f, 0.f, RiseZ);
	// Привязка эталона — `owner = targetChar + 2` (posVec): эффект держится за позицию персонажа. Спавн, привязка и
	//   срок (хэндл не сохраняется ⇒ самый долгий эмиттер) — общей точкой одноразового эффекта на носителе.
	AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnOnCarrier(W, JebBase, At, Host, Skel, NAME_None, Mode, FVector(0.f, 0.f, RiseZ));
	if (!Fx)
	{
		UE_LOG(LogAsdaBuff, Warning,
			TEXT("[предмет-визуал] BP_Fx_%s не собран — эффект применения предмета не показан ")
			TEXT("(собери Tools\\asda_build_buff_fx.py)"), *JebBase.Replace(TEXT("-"), TEXT("_")));
	}
	else
	{
		// ⚠ СТРОГО ПОСЛЕ `ConfigureBoneAttach` (внутри SpawnOnCarrier): он сам обнуляет `bStrictQuadAttach` по режиму.
		Fx->bStrictQuadAttach = GAsdaItemFxStrictQuad;   // см. блок у GAsdaItemFxStrictQuad
	}

	if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		if (!SoundName.IsEmpty())
		{
			// Ветка Камня души: эталон играет звук по ИМЕНИ ФАЙЛА (`Audio_Play3DSound`).
			Snd->PlaySound3DAs(FName(*SoundName), At, EAsdaSoundClass::Magic, Host);   // ★ 30.10: «магия»
		}
		else if (SoundId >= 0)
		{
			// Эталон: `CSoundMgr_PlaySound3D(g_pSoundMgr, 95|96, x, y, z, 0)` — звук по ЧИСЛОВОМУ id
			//   в той же точке, что и эффект.
			Snd->PlaySound3DByIdAs(SoundId, At, EAsdaSoundClass::Magic, Host);   // ★ 30.10: «магия»
		}
	}
	UE_LOG(LogAsdaBuff, Log, TEXT("[предмет-визуал] персонаж userIdx=%d: эффект %s%s"),
		UserIdx, *JebBase,
		!SoundName.IsEmpty() ? *FString::Printf(TEXT(" + звук %s"), *SoundName)
		: SoundId >= 0 ? *FString::Printf(TEXT(" + звук id %d"), SoundId) : TEXT(" (звука в эталоне нет)"));
	return Fx;
}

// ★★★ ЭФФЕКТ ЗВАНИЯ НА ПЕРСОНАЖЕ (2026-08-30). Развёрнутое обоснование — в заголовке.
//   Ветка-близнец `SpawnItemUseFx`, отличий ровно три и все они эталонные:
//     (1) attach-6 `CharWorld` вместо attach-5 `CharPos` — эффект живёт в ПОЛНОЙ мировой матрице
//         персонажа (`effectNodeFollowPlayerBone@0xF58850`), а не только в его позиции;
//     (2) поправки высоты +15 НЕТ — её делает только attach-5 (`effectNodeTrackTargetChar`);
//     (3) звука нет вовсе (см. доказательство в заголовке) — звуковой подсистемы тут не касаемся.
AActor* UAsdaEffectsSubsystem::SpawnTitleFx(int32 UserIdx, const FString& JebBase)
{
	if (JebBase.IsEmpty() || UserIdx < 0) { return nullptr; }
	USkeletalMeshComponent* Skel = nullptr;                       // скелет носителя (для CharWorld)
	// Персонаж ищется по userNum — `CharManager_FindByUserIdx@0x9D1240` ([[charid-vs-usernum]]).
	AActor* Host = ResolveHost(UserIdx, /*bMob=*/false, Skel);
	if (!Host)
	{
		// Эталон при `!pTargetChar` тоже молча выходит — эффекта нет, окно званий не трогается.
		UE_LOG(LogAsdaBuff, Log,
			TEXT("[звание-визуал] персонажа userIdx=%d нет в мире — эффект %s не показан ")
			TEXT("(эталон при `!pTargetChar` тоже выходит)"), UserIdx, *JebBase);
		return nullptr;
	}
	UWorld* W = Host->GetWorld();                                  // мир носителя
	if (!W) { return nullptr; }

	// Точка спавна эталона — `CObj_GetPosition(char->gap_1B0)`, то есть мировая позиция персонажа.
	//   При attach-6 она всё равно переписывается матрицей каждый кадр, но стартовый кадр важен.
	const FVector At = Host->GetActorLocation();                   // старт: позиция персонажа
	// attach-6: полная мировая матрица персонажа, без кости и без подъёма. Хэндл эталон не сохраняет ⇒ гасить нечем и
	//   не надо: живём по самому долгому эмиттеру. ★ Дедуп №2 В1-C (F12): общей точкой одноразового эффекта на носителе.
	AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnOnCarrier(W, JebBase, At, Host, Skel, NAME_None, EAsdaFxAttachMode::CharWorld, FVector::ZeroVector);
	if (!Fx)
	{
		UE_LOG(LogAsdaBuff, Warning,
			TEXT("[звание-визуал] BP_Fx_%s не собран — эффект звания не показан ")
			TEXT("(собери Tools\\run_headless_title_fx.bat)"), *JebBase.Replace(TEXT("-"), TEXT("_")));
		return nullptr;
	}
	UE_LOG(LogAsdaBuff, Log,
		TEXT("[звание-визуал] персонаж userIdx=%d: эффект %s, attach-6 (мировая матрица), ")
		TEXT("жизнь %.2f с (звука в эталоне нет)"), UserIdx, *JebBase, Fx->NaturalLifeSpan());
	return Fx;
}

// ★★★ 29.10.2026: ключ отката реквизита эмоций. Разбор — у объявления `SpawnEmoteProps`.
static ASDA_DEV_CVAR(int32, CVarAsdaEmoteProps,
	TEXT("asda.Emote.Props"), 1,
	TEXT("Реквизит эмоций: 1 (по умолчанию) = как оригинал — мячи жонглирования (M_Ball, мировая матрица персонажа) и ")
	TEXT("два цветка ухаживания (кость _8) у СВОЕГО игрока и у соседей, гаснут при выходе из эмоции; ")
	TEXT("0 = прежний порт (до 29.10.2026) — реквизит только у соседей, мяч на M_DUMMY, четыре цветка, без снятия."),
	ECVF_Default);

bool UAsdaEffectsSubsystem::IsEmotePropsEtalon()
{
	return CVarAsdaEmoteProps.GetValueOnGameThread() != 0;
}

int32 UAsdaEffectsSubsystem::SpawnEmoteProps(int32 UserIdx, int32 EmoteState)
{
	if (EmoteState != 44 && EmoteState != 45) { return 0; }   // реквизит у эталона есть только у этих двух
	USkeletalMeshComponent* Skel = nullptr;                    // скелет носителя (кость `_8` у цветов)
	AActor* Host = ResolveHost(UserIdx, /*bMob=*/false, Skel);
	const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(Host);
	if (!Ch)
	{
		// Эталон: автор — `CharManager_GetLocalPlayer`, сосед — `if (targetChar)`; без персонажа ничего.
		UE_LOG(LogAsdaBuff, Log, TEXT("[эмоция-реквизит] персонажа userIdx=%d нет в мире — реквизит состояния %d не показан"),
			UserIdx, EmoteState);
		return 0;
	}
	FAsdaEmotePropSet Set;
	Set.Host = Host;
	Set.State = EmoteState;
	if (EmoteState == 45)
	{
		// Жонглирование: `Effect_SpawnByName(…, "M_Ball.jeb", 1, pos, colorArg, char->posVec, 0, 6, 0, 1.0f)` у ОБОИХ полов.
		//   attach-6 = `EAsdaFxAttachMode::CharWorld` (мировая матрица, без кости и подъёма; старт — позиция персонажа,
		//   `CObj_GetPosition(char->gap_1B0)`) — общей точкой одноразового эффекта на носителе, как у званий (`SpawnTitleFx`).
		if (UWorld* W = Host->GetWorld())
		{
			if (AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnOnCarrier(W, TEXT("m_ball"), Host->GetActorLocation(), Host, Skel,
				NAME_None, EAsdaFxAttachMode::CharWorld, FVector::ZeroVector))
			{
				Set.Fx.Add(Fx);
			}
			else
			{
				UE_LOG(LogAsdaBuff, Warning, TEXT("[эмоция-реквизит] BP_Fx_m_ball не собран — мячей нет (Tools\\asda_build_char_fx.py, ASDA_CHARFX_ONLY)"));
			}
		}
	}
	else
	{
		// Ухаживание: два эффекта на кость `_8`, attach-10 (полный трансформ кости); набор по полу модели (`gender == 1` — муж).
		//   ★ Имя кости в UE: импорт скелета переименовал кости вида «_N» (подчёркивание + только цифры — FName с пустой базой)
		//   в «Null_N» (дамп `ch1_m/f_cremmy_preview`: `_8` → `Null_8`; `_8_a1` и прочие — как в оригинале). Ищем эталонное имя,
		//   при его отсутствии — имя импорта; без обоих `SpawnDummyBoneFx` честно откатится на пустышку с записью в лог.
		FName Bone8(TEXT("_8"));
		if (Skel && !Skel->DoesSocketExist(Bone8) && Skel->DoesSocketExist(FName(TEXT("Null_8")))) { Bone8 = FName(TEXT("Null_8")); }
		const bool bMale = (Ch->Sex == 1);
		const TCHAR* Names[2] = { bMale ? TEXT("m_flower01") : TEXT("f_flower01"), bMale ? TEXT("m_flower02") : TEXT("f_flower02") };
		for (const TCHAR* N : Names)
		{
			if (AActor* Fx = SpawnDummyBoneFx(UserIdx, N, FString(), EAsdaFxAttachMode::BoneFull, Bone8)) { Set.Fx.Add(Fx); }
		}
	}
	const int32 N = Set.Fx.Num();
	UE_LOG(LogAsdaBuff, Log, TEXT("[эмоция-реквизит] userIdx=%d состояние %d: эффектов %d"), UserIdx, EmoteState, N);
	if (N > 0) { EmoteProps.Add(MoveTemp(Set)); }
	return N;
}

void UAsdaEffectsSubsystem::TickEmoteProps()
{
	for (int32 i = EmoteProps.Num() - 1; i >= 0; --i)
	{
		FAsdaEmotePropSet& S = EmoteProps[i];
		bool bAnyAlive = false;
		for (const TWeakObjectPtr<AActor>& F : S.Fx) { bAnyAlive |= F.IsValid(); }
		const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(S.Host.Get());
		if (!bAnyAlive || !Ch) { EmoteProps.RemoveAtSwap(i); continue; }   // догорели сами или носитель ушёл из мира
		// Эталон: `if (state != 45 && handle) objectMgrResetByHandle(...)` (и то же для 44). Эмоция у порта считается
		//   «стоящей», только пока идёт её клип (`GetBusyActionCode`) — ровно как у эталона, который по концу прохода пишет 0.
		if (Ch->GetBusyActionCode() == S.State) { continue; }
		for (const TWeakObjectPtr<AActor>& F : S.Fx)
		{
			if (AActor* A = F.Get()) { A->Destroy(); }
		}
		UE_LOG(LogAsdaBuff, Log, TEXT("[эмоция-реквизит] состояние %d снято (сейчас %d) — реквизит погашен"), S.State, Ch->GetBusyActionCode());
		EmoteProps.RemoveAtSwap(i);
	}
}

// ★★★ ВОЛНА 2 — эффект на кости-пустышке. Развёрнутое обоснование — в заголовке.
//
// ★ ИСПРАВЛЕНО (проверка волны 2): режим привязки — НЕ константа, у двух каналов он РАЗНЫЙ.
//   Последний-но-один аргумент `Effect_SpawnByName` в эталоне — это номер attach-режима:
//     • гача/бустер — attach **10** (полный трансформ кости): `eff_lucky_a_01.jeb` и
//       `eff_lucky_a_00.jeb`, `network.c:44557` и `:44576` (то же в `shop_trade.c:12430-12450`);
//     • семь премиум-баффов — attach **7** (ТОЛЬКО позиция кости, без ориентации):
//       `CharEffect_SpawnPMBuff_00@0xF2C2F0` (`effects_particles.c:56979`) и шесть его близнецов.
//   Раньше здесь жёстко стоял `BoneFull` (attach-10) на оба канала, из-за чего премиум-баффы
//   вращались вместе с костью — в оригинале они висят без ориентации.
AActor* UAsdaEffectsSubsystem::SpawnDummyBoneFx(int32 UserIdx, const FString& JebBase, const FString& SoundName,
                                                EAsdaFxAttachMode BoneMode, FName BoneOverride)
{
	if (JebBase.IsEmpty() || UserIdx < 0) { return nullptr; }
	USkeletalMeshComponent* Skel = nullptr;
	AActor* Host = ResolveHost(UserIdx, /*bMob=*/false, Skel);
	if (!Host)
	{
		UE_LOG(LogAsdaBuff, Log,
			TEXT("[предмет-визуал] персонажа userIdx=%d нет в мире — эффект %s не показан "
			     "(эталон при `!CharManager_FindByUserIdx` тоже выходит)"), UserIdx, *JebBase);
		return nullptr;
	}
	UWorld* W = Host->GetWorld();
	if (!W) { return nullptr; }

	// Кость эталона выбирается ПО ПОЛУ модели: `gender == 1 ? "M_DUMMY" : "G_DUMMY"`.
	//   В наших экспортированных скелетах женская пустышка может отсутствовать — тогда честно
	//   откатываемся на `M_DUMMY`, а если нет и её, эффект вешается на позицию персонажа
	//   (`CharPos`), чтобы канал не пропадал целиком (закон `no-simplifications-stubs`:
	//   отсутствие ассета фиксируется в логе, а не проглатывается).
	FName Bone = NAME_None;
	if (!BoneOverride.IsNone())
	{
		Bone = BoneOverride;   // ★ явная кость (эмоция «ухаживание»: цветки на кость `_8`)
	}
	else if (const AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(Host))
	{
		Bone = (PC->Sex == 1) ? FName(TEXT("M_DUMMY")) : FName(TEXT("G_DUMMY"));
	}
	else
	{
		Bone = FName(TEXT("M_DUMMY"));
	}
	// ★ ЭТАЛОННАЯ ПОПРАВКА ВЫСОТЫ. attach-**7** (`effectNodeUpdateCasterItemPos@0xF58990`) кладёт
	//   `T = боневая матрица × мир персонажа; SetPos(T.x, T.y + 15.0, T.z)` — **+15 по игровой Y
	//   = +15 по UE Z**. У attach-**10** (`effectAttachToCharSlot@0xF58F10`, гача `eff_lucky_*`)
	//   поправки НЕТ — там в ноду пишется ПОЛНАЯ матрица без сдвига. Поэтому поправка привязана к
	//   режиму, а не к вызову. Раньше оба канала спавнились без неё (дефект-в-копии, ср. канал
	//   состояний, где `BuffFxTrackRiseZ` уже применялся).
	//   ★ Дедуп №2 В1-C (F12): подъём attach-7 — из общей таблицы кодов эталона (код 7 = кость + 15).
	float RiseZ = 0.f;   // подъём запрошенного режима
	if (BoneMode == EAsdaFxAttachMode::BonePos)
	{
		EAsdaFxAttachMode Mode7 = EAsdaFxAttachMode::BonePos;   // режим кода 7 (тот же BonePos)
		AAsdaEffectActor::EtalonAttach(7, 0, BuffFxTrackRiseZ, Mode7, RiseZ);
	}
	FVector At = Host->GetActorLocation();
	bool bBoneOk = false;
	if (Skel)
	{
		if (Skel->DoesSocketExist(Bone)) { bBoneOk = true; }
		else if (Skel->DoesSocketExist(FName(TEXT("M_DUMMY")))) { Bone = FName(TEXT("M_DUMMY")); bBoneOk = true; }
		if (bBoneOk) { At = Skel->GetSocketLocation(Bone); }
	}
	At.Z += RiseZ;
	// Кости нет ⇒ откат на позицию персонажа: это уже attach-5-подобная ветка, поправка +15
	//   остаётся (её несёт `RiseZ`, посчитанный по ЗАПРОШЕННОМУ режиму).
	//   ★ Дедуп №2 В1-C (F12): спавн, привязка и срок — общей точкой одноразового эффекта на носителе.
	AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnOnCarrier(W, JebBase, At, Host, Skel, bBoneOk ? Bone : NAME_None,
		bBoneOk ? BoneMode : EAsdaFxAttachMode::CharPos, FVector(0.f, 0.f, RiseZ));
	if (!Fx)
	{
		UE_LOG(LogAsdaBuff, Warning,
			TEXT("[предмет-визуал] BP_Fx_%s не собран — эффект не показан (соберите его сборщиком эффектов)"),
			*JebBase.Replace(TEXT("-"), TEXT("_")));
	}
	else
	{
		// ⚠ СТРОГО ПОСЛЕ `ConfigureBoneAttach` (внутри SpawnOnCarrier): он сам обнуляет `bStrictQuadAttach` по режиму.
		Fx->bStrictQuadAttach = GAsdaItemFxStrictQuad;   // см. блок у GAsdaItemFxStrictQuad
	}
	if (!SoundName.IsEmpty())
	{
		if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
		{
			Snd->PlaySound3DAs(FName(*SoundName), At, EAsdaSoundClass::Magic, Host);   // ★ 30.10: «магия»
		}
	}
	UE_LOG(LogAsdaBuff, Log, TEXT("[предмет-визуал] userIdx=%d: %s на кости %s%s"),
		UserIdx, *JebBase, bBoneOk ? *Bone.ToString() : TEXT("(нет — привязка к позиции)"),
		SoundName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" + звук %s"), *SoundName));
	return Fx;
}

// ★ 5473 — порт `OnRecv_OtherPlayerBoost@0xF4C700` (network.c:44525): найти актора по userNum
//   (i16 @+4; charId @+0 клиент не читает — [[charid-vs-usernum]]) и показать тот же эффект удачи,
//   что автор играет у себя. Ветвление «редкий/обычный» и имена файлов совпадают дословно.
void UAsdaEffectsSubsystem::OnOtherPlayerBoost(int32 UserNum, bool bRare)
{
	SpawnBoostFx(UserNum, bRare);
}

AActor* UAsdaEffectsSubsystem::SpawnBoostFx(int32 UserIdx, bool bRare)
{
	return SpawnDummyBoneFx(UserIdx,
		bRare ? TEXT("eff_lucky_a_01") : TEXT("eff_lucky_a_00"),
		bRare ? TEXT("s_item_boostrare") : TEXT("s_item_boostnormal"));
}

AActor* UAsdaEffectsSubsystem::SpawnPmBuffFx(int32 UserIdx, int32 SubType)
{
	// Таблица из кода эталона (`charBuff_ApplyPMBuffFromPacket@0xF26110`), не из данных:
	//   в `special_itemlist` имён .jeb нет вовсе, класс эффекта выбирается ПОДТИПОМ.
	// ⚠ Регистр имени. В эталоне строка записана как `eff_0_char_99_PMbuff_a_0N.jeb`, а файл на диске
	//   (`Donors/TRANSLATE_unpacked/models_orig/Data/Effect/Model`) называется `eff_0_char_99_pmbuff_a_0N.jeb` — по нему
	//   сборщик и назовёт ассет. Пишем ровно то, что лежит на диске, чтобы имя BP совпало буква в букву.
	const TCHAR* Jeb = nullptr;
	switch (SubType)
	{
	case 1:    Jeb = TEXT("eff_0_char_99_pmbuff_a_03"); break;  // CharEffect_SpawnPMBuff_03@0xF2C5C0
	case 2:    Jeb = TEXT("eff_0_char_99_pmbuff_a_02"); break;  // CharEffect_SpawnPMBuff_02@0xF2CB60
	case 3:    Jeb = TEXT("eff_0_char_99_pmbuff_a_04"); break;  // CharEffect_SpawnPMBuff_04@0xF2C890
	case 6:    Jeb = TEXT("eff_0_char_99_pmbuff_a_01"); break;  // CharEffect_SpawnPMBuff_01@0xF2C020
	case 7:    Jeb = TEXT("eff_0_char_99_pmbuff_a_00"); break;  // CharEffect_SpawnPMBuff_00@0xF2C2F0
	case 0x10: Jeb = TEXT("eff_0_char_99_pmbuff_a_05"); break;  // CharBuff_SpawnPMbuff05@0xF2CE30
	case 0x11: Jeb = TEXT("eff_0_char_99_pmbuff_a_06"); break;  // CharBuff_SpawnPMbuff06@0xF2D100
	default:
		// Эталонный `default`: визуала нет, обновляется только иконка в HUD — её обновляет
		//   делегат `OnEffectsChanged` из `AddType2`, отдельного действия здесь не нужно.
		return nullptr;
	}
	// ⚠ Звука у этих семи классов НЕТ (проверено вторым методом — см. заголовок).
	// ★ attach-**7** = только позиция кости (`CharEffect_SpawnPMBuff_00@0xF2C2F0`,
	//   `effects_particles.c:56979`), в отличие от attach-10 у гачи — см. `SpawnDummyBoneFx`.
	return SpawnDummyBoneFx(UserIdx, Jeb, FString(), EAsdaFxAttachMode::BonePos);
}

// ★★★ ПОЛНЫЙ ПОРТ 32-КЕЙСОВОГО ДИСПЕТЧЕРА `netRecvItemBuffApply@0xF23C60` (`network.c:36236-36592`),
//   ветка `case 1` (успех) → `switch (Table2Record->subType − 1)`. Номер кейса в эталоне на ЕДИНИЦУ
//   МЕНЬШЕ подтипа, здесь таблица приведена уже к подтипу — чтобы не тащить смещение в вызывающий код.
//
//   Имена .jeb, порядок спавнов, режим привязки и звуки сняты ДОСЛОВНО с тел спавнеров
//   (`effects_particles.c`), а не из данных: в `special_itemlist` имён эффектов нет вовсе.
//     • `CharEffect_SpawnHpRecovery@0xF28F10`      — PMhpre01 a_00,a_01,a_02,a_00, attach 10, S_e_PMhprecovery
//     • `CharEffect_SpawnPMmpreOnDummy@0xF294F0`   — PMmpre01 a_00,a_01,a_02,a_00, attach 10, S_e_PMhprecovery
//     • `CharEffect_SpawnPMhpreOnDummy@0xF29AA0`   — PMhpre01 a_00,a_01,a_02,a_00, attach 10, S_e_PMhprecovery
//     • `CharEffect_SpawnPMmpreOnDummy_v2@0xF2A170`— PMmpre01 a_00,a_01,a_02,a_00, attach 10, S_e_PMmprecovery
//     • `CharEffect_SpawnPMalreOnDummy@0xF2A720`   — PMalre01 a_00,a_01,a_02,a_00,a_00, attach 10, S_e_PMallrecovery
//     • `CharEffect_PlaySkillReset@0xF2B1A0`       — PMskre01 a_00,              attach **7**, S_e_PMskillreset
//     • `CharEffect_PlayArmorBreak@0xF2B4A0`       — PMarmo01 a_00,              attach 10,   S_e_PMarmor
//     • `CharEffect_SpawnPMhpreOnDummy_v2@0xF2D950`— PMhpre01 a_00,a_01,a_02,a_00, attach 10, S_e_PMhprecovery
//   ⚠ Регистр: в эталоне строки записаны как `…PMhpre01-a_00.jeb`, на диске
//   (`Donors/TRANSLATE_unpacked/models_orig/Data/Effect/Model`) файлы в НИЖНЕМ регистре — пишем как на диске, иначе
//   имя ассета `BP_Fx_…` не совпадёт (`AAsdaEffectActor::SpawnByJebName` делает только `-`→`_`).
//   ⚠ Звук эталон играет ОДИН раз на весь набор, а не по разу на эмиттер.
//
//   ★ Подтипы 15/35/54/55/65/112/120 визуала не имеют: это транспорт (`Recv_MountVehicleRide`),
//     сброс навыков, окно склада и т. п. — они обслуживаются другими подсистемами.
//   ★ Подтипы 20, 21 и 120 эталон РАЗБИРАЕТ (`netParseInventorySlotUpdate`), но эффект НЕ спавнит.
//   ★ Подтипы 34, 38, 48, 49, 50, 51 зовут спавнеры, тела которых в текущем срезе декомпила не
//     восстановлены (`sub_92B827`, `sub_92BDB8`, `sub_93C4C4`, `sub_927268`, `sub_934EC7`,
//     `sub_91F86F` — тонкие переходники без тела в срезе). Имён .jeb для них НЕТ, поэтому визуала
//     здесь тоже нет; это ЗАФИКСИРОВАННЫЙ ПРОБЕЛ ИСТОЧНИКА, а не упрощение — пишем в лог.
int32 UAsdaEffectsSubsystem::SpawnPmItemUseFx(int32 UserIdx, int32 SubType)
{
	// Набор .jeb + звук + режим привязки, разложенный по подтипу.
	const TCHAR* Jebs[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
	int32 Count = 0;
	const TCHAR* Snd = nullptr;
	EAsdaFxAttachMode Mode = EAsdaFxAttachMode::BoneFull;   // attach 10

	auto SetQuad = [&Jebs, &Count](const TCHAR* A0, const TCHAR* A1, const TCHAR* A2)
	{
		Jebs[0] = A0; Jebs[1] = A1; Jebs[2] = A2; Jebs[3] = A0; Count = 4;
	};

	switch (SubType)
	{
	// ── Семь классов, общих с 7-кейсовой таблицей чужого игрока (attach 7, звука нет) ──
	case 1:  case 2:  case 3:  case 6:  case 7:  case 16:  case 17:
		return SpawnPmBuffFx(UserIdx, SubType) ? 1 : 0;
	// ★ Подтип 19 в эталоне идёт ТЕМ ЖЕ кейсом, что и 16 (`case 15: case 18:` при сдвиге −1),
	//   то есть визуал у него — `pmbuff_a_05`. В 7-кейсовой таблице его нет, поэтому в порту
	//   премиум-предмет подтипа 19 (напр. код 500) не показывал НИЧЕГО.
	case 19:
		return SpawnPmBuffFx(UserIdx, /*как подтип 0x10*/ 16) ? 1 : 0;

	// ── Восстановление ЖИЗНЕЙ: `CharEffect_SpawnPMhpreOnDummy` / `_v2` / `SpawnHpRecovery` ──
	case 25: case 42: case 44: case 52:
		SetQuad(TEXT("eff_0_othr_99_pmhpre01-a_00"), TEXT("eff_0_othr_99_pmhpre01-a_01"),
		        TEXT("eff_0_othr_99_pmhpre01-a_02"));
		Snd = TEXT("S_e_PMhprecovery");
		break;
	// ── Восстановление МАНЫ. У «обычного» спавнера звук в эталоне тот же, что у ЖИЗНЕЙ
	//    (`CharEffect_SpawnPMmpreOnDummy@0xF294F0` играет `S_e_PMhprecovery.wav` — это дефект
	//    оригинала, но воспроизводим как есть); у `_v2` — свой `S_e_PMmprecovery.wav`.
	case 45:
		SetQuad(TEXT("eff_0_othr_99_pmmpre01-a_00"), TEXT("eff_0_othr_99_pmmpre01-a_01"),
		        TEXT("eff_0_othr_99_pmmpre01-a_02"));
		Snd = TEXT("S_e_PMhprecovery");
		break;
	case 43:
		SetQuad(TEXT("eff_0_othr_99_pmmpre01-a_00"), TEXT("eff_0_othr_99_pmmpre01-a_01"),
		        TEXT("eff_0_othr_99_pmmpre01-a_02"));
		Snd = TEXT("S_e_PMmprecovery");
		break;
	// ── Полное восстановление: ПЯТЬ спавнов (a_00 повторяется дважды в хвосте) ──
	case 47:
		Jebs[0] = TEXT("eff_0_othr_99_pmalre01-a_00");
		Jebs[1] = TEXT("eff_0_othr_99_pmalre01-a_01");
		Jebs[2] = TEXT("eff_0_othr_99_pmalre01-a_02");
		Jebs[3] = TEXT("eff_0_othr_99_pmalre01-a_00");
		Jebs[4] = TEXT("eff_0_othr_99_pmalre01-a_00");
		Count = 5;
		Snd = TEXT("S_e_PMallrecovery");
		break;
	// ── Сброс навыков: единственный из набора с attach **7** ──
	case 36:
		Jebs[0] = TEXT("eff_0_othr_99_pmskre01-a_00"); Count = 1;
		Snd = TEXT("S_e_PMskillreset");
		Mode = EAsdaFxAttachMode::BonePos;
		break;
	// ── Разрушение брони ──
	case 41:
		Jebs[0] = TEXT("eff_0_char_99_pmarmo01-a_00"); Count = 1;
		Snd = TEXT("S_e_PMarmor");
		break;
	default:
		UE_LOG(LogAsdaBuff, Verbose,
			TEXT("[премиум-визуал] подтип %d: в эталоне у этого кейса своего .jeb нет "
			     "(либо кейса нет вовсе, либо спавнер не восстановлен в срезе декомпила)"), SubType);
		return 0;
	}

	int32 Spawned = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		// Звук эталон играет один раз на весь набор — отдаём его только первому спавну.
		const FString SndName = (i == 0 && Snd) ? FString(Snd) : FString();
		if (SpawnDummyBoneFx(UserIdx, FString(Jebs[i]), SndName, Mode)) { ++Spawned; }
	}
	UE_LOG(LogAsdaBuff, Log,
		TEXT("[премиум-визуал] 5451 подтип %d: заспавнено %d из %d эмиттеров (привязка %s%s)"),
		SubType, Spawned, Count,
		Mode == EAsdaFxAttachMode::BonePos ? TEXT("attach-7 позиция кости") : TEXT("attach-10 трансформ кости"),
		Snd ? *FString::Printf(TEXT(", звук %s"), Snd) : TEXT(", звука нет"));
	return Spawned;
}

// ═══════════════ ★★★ ТРЕТИЙ КАНАЛ ВИЗУАЛА НАВЫКОВ — ЭФФЕКТ СОСТОЯНИЯ (БАФФА) ═══════════════════
//
// Полный реверс — `Reports/skill_fx_anim.md` §16/§17, раскладка строки — `AsdaBuffFxRow.h`.
// Коротко: `skill_info.sif[skillId].buff0/1/2 SkillId` = id СОСТОЯНИЯ 0..349 → ДВЕ таблицы по 350
//   обработчиков (`pChar + 52304 + id` у игрока, `pMob + 1844 + id` у сущности); тело обработчика
//   спавнит `.jeb` литералом и кладёт хэндлы в запись баффа `+24`/`+28`, откуда их гасят
//   `buffStopEffectsBySkillId@0xF83930` / `cCharRemoveSkillBuff@0xA2AC20` / рестарт.
//
// ★★★ ГЕЙТ «ВИДНО ЛИ БАФФ ЧУЖОГО ИГРОКА» — В ОРИГИНАЛЕ НЕ ИСПОЛНЯЕТСЯ (проверено по бинарю).
//   `cChar_restartSkillEffectByType@0xA2AD60` содержит ветку
//     `if (dword_2909970 || pChar[262] == 1) { if (*(DWORD*)(dword_2BBEA9C + 4*buffId) == 1) вызвать; }
//      else вызвать;`
//   Сканирование ВСЕГО образа (`AsdaStory.exe`, 13 344 768 б) даёт по ОДНОЙ ссылке на каждую из
//   этих двух глобалей — ровно эти чтения; ЗАПИСЕЙ НЕТ, а обе лежат в неинициализированном хвосте
//   `.data` (за пределами сырых данных секции) ⇒ на старте нули. Значит `dword_2909970 == 0`, и
//   попадание в гейт означало бы разыменование `*(DWORD*)(0 + 4*buffId)` — падение. Вывод: ветка
//   недостижима, обработчик зовётся ВСЕГДА. Поэтому эффекты состояний показываем и своему
//   персонажу, и чужим, и мобам — без «таблицы видимости».
//   (Формулировка по закону [[dead-code-markings-suspect]]: не «мёртвый код», а «писателей в
//    бинаре нет, исполнение ветки привело бы к падению».)
const FAsdaBuffFxRow* UAsdaEffectsSubsystem::GetBuffFxRow(int32 StateId) const
{
	EnsureBuffFxLoaded();
	return BuffFx.Find(StateId);
}

void UAsdaEffectsSubsystem::EnsureBuffFxLoaded() const
{
	if (bBuffFxLoaded) { return; }
	bBuffFxLoaded = true;
	const UDataTable* DT = AsdaLoadDT(TEXT("/Game/AsdaAssets/Data/DT_BuffFx.DT_BuffFx"));
	if (!DT)
	{
		UE_LOG(LogAsdaBuff, Warning,
			TEXT("[состояния-визуал] DT_BuffFx не загрузился — третий канал визуала навыков молчит. ")
			TEXT("Собери: python Tools\\asda_dump_buff_fx.py, затем Tools\\asda_build_buff_fx_dt.py + asda_build_buff_fx.py"));
		return;
	}
	int32 WithPlayer = 0, WithMob = 0;
	for (const TPair<FName, uint8*>& It : DT->GetRowMap())
	{
		const FAsdaBuffFxRow* R = reinterpret_cast<const FAsdaBuffFxRow*>(It.Value);
		if (!R || R->Id < 0) { continue; }
		BuffFx.Add(R->Id, *R);
		if (!R->PlayerFx.IsEmpty()) { ++WithPlayer; }
		if (!R->MobFx.IsEmpty())    { ++WithMob; }
	}
	UE_LOG(LogAsdaBuff, Log,
		TEXT("[состояния-визуал] DT_BuffFx: состояний %d, с программой у игрока %d, у сущности %d"),
		BuffFx.Num(), WithPlayer, WithMob);
}

void UAsdaEffectsSubsystem::ParseBuffFxProgram(const FString& Cell, TArray<FAsdaBuffFxSpawn>& Out)
{
	Out.Reset();
	if (Cell.IsEmpty()) { return; }
	TArray<FString> Items;
	Cell.ParseIntoArray(Items, TEXT(";"), true);
	for (const FString& It : Items)
	{
		TArray<FString> F;
		It.ParseIntoArray(F, TEXT("|"), false);
		if (F.Num() < 1 || F[0].IsEmpty()) { continue; }
		FAsdaBuffFxSpawn S;
		S.Jeb     = F[0].ToLower();
		S.Attach  = (F.Num() > 1) ? FCString::Atoi(*F[1]) : 0;
		S.Bone    = (F.Num() > 2 && !F[2].IsEmpty()) ? FName(*F[2]) : NAME_None;
		S.Slot    = (F.Num() > 3) ? FCString::Atoi(*F[3]) : -1;
		S.Loop    = (F.Num() > 4) ? FCString::Atoi(*F[4]) : -1;
		S.Variant = (F.Num() > 5) ? FCString::Atoi(*F[5]) : -1;
		S.Owner   = (F.Num() > 6 && !F[6].IsEmpty()) ? FName(*F[6]) : FName(TEXT("self"));
		Out.Add(MoveTemp(S));
	}
}

AActor* UAsdaEffectsSubsystem::ResolveHost(int32 Key, bool bMob, USkeletalMeshComponent*& OutSkel) const
{
	OutSkel = nullptr;
	const UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!WS || Key < 0) { return nullptr; }
	// ★ Аудит №3 (B6): поиск — через ЕДИНУЮ точку `UAsdaWorldSubsystem::ActorByTarget`.
	//   ⚠ `bMob` здесь НЕ сырой байт пакета: это СВОЙ флаг порта, который выбирает один из двух
	//   реестров канала состояний (`Lists` = персонажи по userNum, `MobLists` = сущности по renderId),
	//   и во все вызовы он приходит литералом. Смысл ветвления прежний, эталон тот же:
	//   `Entity_FindByRenderId@0xF772D0` против `CharManager_FindByUserIdx@0x9D1240`
	//   ([[charid-vs-usernum]]). ⚠ Имя `bMob` противоположно значению `rec[0]` 29-байтной записи
	//   (там 1 = ПЕРСОНАЖ) — ровно из-за этой инверсии в единую точку идёт ЯВНЫЙ enum.
	const EAsdaTargetKind Kind = bMob ? EAsdaTargetKind::Entity : EAsdaTargetKind::Character;
	AActor* A = WS->ActorByTarget(Kind, Key);
	if (bMob)
	{
		if (AAsdaMobActor* M = Cast<AAsdaMobActor>(A)) { OutSkel = M->Mesh; }
		return A;
	}
	if (AAsdaPlayerCharacter* C = Cast<AAsdaPlayerCharacter>(A)) { OutSkel = C->BaseSkel; }
	return A;
}

// Эталон CEffectMgr_StopByOwner(rec+24) / (rec+28) — гашение долгоживущих эффектов состояния.
void UAsdaEffectsSubsystem::StopBuffFx(FAsdaEffectEntry& E)
{
	if (AActor* A = E.Fx0.Get()) { A->Destroy(); }
	if (AActor* A = E.Fx1.Get()) { A->Destroy(); }
	E.Fx0.Reset();
	E.Fx1.Reset();
}

bool UAsdaEffectsSubsystem::SpawnBuffFx(FAsdaEffectEntry& E, int32 Key, bool bMob)
{
	// ★ 05.11.2026 [ОРИГИНАЛ] (сессия grenade) состояние 285 «Отпор»: обработчик таблицы состояний эталона шлёт 8215 вид 1 — не визуал,
	//   поэтому до гейта визуала. Разбор — `UAsdaSkillFxSubsystem::OnRepulseState285`.
	if (E.Type == 0 && E.Id == 285 && !E.bFxSpawned)
	{
		if (UAsdaSkillFxSubsystem* Sfx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr)
		{
			Sfx->OnRepulseState285(Key, bMob, E.CasterUserIdx);
		}
	}
	if (!bBuffFxEnabled || E.bFxSpawned) { return true; }
	const FAsdaBuffFxRow* Row = GetBuffFxRow(E.Id);
	if (!Row)
	{
		E.bFxSpawned = true;   // состояния нет в таблице — молчим, повторять нечего
		return true;
	}
	const FString& Cell = bMob ? Row->MobFx : Row->PlayerFx;
	if (Cell.IsEmpty())
	{
		// ★★★ ПОЛОЖИТЕЛЬНЫЙ ФАКТ, А НЕ ПРОБЕЛ: `charSkillFxHandlerTableInit@0x9AB290` сперва
		//   заполняет ВСЕ 350 ячеек заглушкой `noop_stub3` и переопределяет лишь часть. Пустая
		//   программа = в оригинале ЗАПИСАНО «ничего не показывать» (§16.5). Подставлять «похожий»
		//   эффект запрещено ([[never-invent-only-original]]).
		E.bFxSpawned = true;
		return true;
	}

	USkeletalMeshComponent* Skel = nullptr;
	AActor* Host = ResolveHost(Key, bMob, Skel);
	if (!Host) { return false; }        // носителя ещё нет — повторим тиком
	UWorld* W = Host->GetWorld();
	if (!W) { return false; }

	// ★★★ РАЗМЕРНЫЙ КЛАСС НОСИТЕЛЯ (`сущность + 7356`) выбирает вариант `-a_00/-a_01/-a_02` и
	//   высоту привязок 12/13 (+25/+45/+60 и +200/+400/+600, `effects_particles.c:59656-59668`).
	//   ★ ИСТОЧНИК НАЙДЕН И ПОДКЛЮЧЁН 2026-08-11 — прежняя запись «писателя поля в срезе нет»
	//   ОПРОВЕРГНУТА сканом `.text`: пишут `gameObjCtorInit@0xF810B0+0x280` (0 у не-мобов) и
	//   `mobEffect_LoadArchiveStep@0xF81B90+0xEEF` (`Mob list.BIN +0x90` по TYPE-ID моба).
	//   Значение отдаёт `UAsdaSkillFxSubsystem::GetTargetSizeClass` (единый читатель `DT_MobStats`),
	//   чтобы обе подсистемы гарантированно выбирали ОДНУ ветку. Раньше здесь стоял жёсткий 0, и
	//   208 типов мобов из 854 получали не свой вариант эффекта.
	const UAsdaSkillFxSubsystem* SkillFx = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr;
	const int32 SizeClass = SkillFx ? SkillFx->GetTargetSizeClass(Host) : 0;

	TArray<FAsdaBuffFxSpawn> Prog;
	ParseBuffFxProgram(Cell, Prog);
	int32 Spawned = 0;
	for (const FAsdaBuffFxSpawn& S : Prog)
	{
		if (S.Variant >= 0 && S.Variant != SizeClass) { continue; }

		// Носитель ЗАПИСИ: обычно сам обладатель состояния; у двух обработчиков — кастер
		//   (`Effect_SpawnCounterOnChar`/`Effect_SpawnDridg5OnChar`), у одного — цель обладателя
		//   (`CharFx_SpawnBlossomEffect@0x9B4ED0`).
		AActor* Own = Host;
		USkeletalMeshComponent* OwnSkel = Skel;
		bool bOwnIsMob = bMob;
		if (S.Owner == FName(TEXT("caster")))
		{
			Own = ResolveHost(E.CasterUserIdx, /*bMob=*/false, OwnSkel);
			bOwnIsMob = false;
		}
		else if (S.Owner == FName(TEXT("target")))
		{
			// Эталон: `Entity_FindByRenderId(pChar->targetRenderId)` — цель ЛЮБОГО персонажа.
			//   В порту цель отслеживается только у ЛОКАЛЬНОГО игрока (окно выбора цели), поэтому
			//   для чужих эту запись честно пропускаем и пишем в лог, а не подставляем себя.
			Own = nullptr;
			const UGameInstance* GI = GetGameInstance();
			UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			const UAsdaSkillUseSubsystem* Use = GI ? GI->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
			if (WS && Use && !bMob && Key == SelfUserIdx())
			{
				int32 TType = 0, TIdx = -1; FVector2D TPos = FVector2D::ZeroVector; float TDist = 0.f;
				if (Use->GetSelectedTarget(TType, TIdx, TPos, TDist) && TIdx >= 0)
				{
					bOwnIsMob = (TType != 1);
					Own = ResolveHost(TIdx, bOwnIsMob, OwnSkel);
				}
			}
			if (!Own)
			{
				UE_LOG(LogAsdaBuff, Verbose,
					TEXT("[состояния-визуал] состояние %d (%s): запись %s рождается У ЦЕЛИ носителя, а цель неизвестна — пропущена"),
					E.Id, *Row->BuffName, *S.Jeb);
				continue;
			}
		}
		if (!Own) { continue; }

		// ── ПРИВЯЗКА: номер режима эталона → режим эффект-актора + эталонная поправка высоты ─────
		//   ★ Дедуп №2 В1-C (F12): тело `switch` вынесено в общую таблицу `AAsdaEffectActor::EtalonAttach` (ей же
		//   пользуются каналы предмета и пустышки — копии «+15» там уже дважды терялись).
		EAsdaFxAttachMode Mode = EAsdaFxAttachMode::CharPos;
		float RiseZ = 0.f;
		if (!AAsdaEffectActor::EtalonAttach(S.Attach, SizeClass, BuffFxTrackRiseZ, Mode, RiseZ))
		{
			// ★ Закон [[jeb-emitter-routing-hole]]: неизвестный режим обязан КРИЧАТЬ, а не тихо
			//   ехать по ветке по умолчанию. В канале состояний встречаются только 3/5/6/7/8/9/12/13.
			UE_LOG(LogAsdaBuff, Warning,
				TEXT("[состояния-визуал] состояние %d (%s): НЕИЗВЕСТНЫЙ режим привязки %d у %s — ")
				TEXT("взята позиция носителя; проверь Tools/asda_dump_buff_fx.py"),
				E.Id, *Row->BuffName, S.Attach, *S.Jeb);
		}
		// attach-6/8 строят ПОЛНУЮ матрицу носителя, а она в порту берётся с его скелетного
		//   компонента (`FxToBoneW × ComponentTransform`, см. ComputeFollowTargetWorld). Нет
		//   скелета — нет матрицы: честно откатываемся на позицию и пишем в лог.
		if (Mode == EAsdaFxAttachMode::CharWorld && !OwnSkel)
		{
			UE_LOG(LogAsdaBuff, Log,
				TEXT("[состояния-визуал] состояние %d (%s): у носителя нет скелетного компонента — ")
				TEXT("привязка %d сведена к позиции носителя"), E.Id, *Row->BuffName, S.Attach);
			Mode = EAsdaFxAttachMode::CharPos;
		}
		// Кость нужна только режиму 7; если её нет в скелете — эталон при `boneIdx == -1` ветку НЕ
		//   исполняет вовсе, поэтому и мы не спавним «куда-нибудь».
		if (Mode == EAsdaFxAttachMode::BonePos
			&& (!OwnSkel || S.Bone.IsNone() || !OwnSkel->DoesSocketExist(S.Bone)))
		{
			UE_LOG(LogAsdaBuff, Log,
				TEXT("[состояния-визуал] состояние %d (%s): кости «%s» нет у носителя — запись %s пропущена ")
				TEXT("(эталон: List_FindIndexByName == -1 ⇒ ветка не исполняется)"),
				E.Id, *Row->BuffName, *S.Bone.ToString(), *S.Jeb);
			continue;
		}

		const FVector At = (Mode == EAsdaFxAttachMode::BonePos && OwnSkel)
			? OwnSkel->GetSocketLocation(S.Bone) + FVector(0.f, 0.f, RiseZ)
			: Own->GetActorLocation() + FVector(0.f, 0.f, RiseZ);
		AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnByJebName(W, S.Jeb, At, FRotator::ZeroRotator, Own);
		if (!Fx)
		{
			// Либо BP не собран, либо `.jeb` НЕТ НИ В ОДНОЙ СБОРКЕ клиента (29 имён, §16.5) — тогда
			//   и оригинал ничего не показывал (`Effect_SpawnByName` возвращает nullptr, фолбэка нет).
			UE_LOG(LogAsdaBuff, Warning,
				TEXT("[состояния-визуал] состояние %d (%s): BP_Fx_%s не найден ")
				TEXT("(запусти Tools\\asda_build_buff_fx.py; если файла нет и в оригинале — так и должно быть)"),
				E.Id, *Row->BuffName, *S.Jeb.Replace(TEXT("-"), TEXT("_")));
			continue;
		}
		Fx->ConfigureBoneAttach(Own, OwnSkel, S.Bone, Mode, FVector(0.f, 0.f, RiseZ));
		// ★ R5-fix5 (13.11.2026) [ОРИГИНАЛ]: эффект состояния ИГРОКА на его цели-мобе (состояние 296, `CharFx_SpawnBlossomEffect@0x9B4ED0`:
		//   `CEffectMgr_CreateEffect(…, id моба, 9, …)`) гаснет, когда моб умер, — обработчик режима 9: состояние носителя 10 →
		//   эффект удаляется. Состояния самого моба снимает `ClearMobEffects`, а этот эффект числится за игроком. Разбор — AsdaFxDeadHost.h.
		if (S.Owner == FName(TEXT("target")) && bOwnIsMob && Mode == EAsdaFxAttachMode::CharPos) { AsdaFxDeadHost::Mark(Fx); }
		++Spawned;

		// ★★★ 08.10.2026 (ревизия fx_loop, КРИТ-1): ПЕРЕОПРЕДЕЛЕНИЕ endMode КОНТЕЙНЕРА обработчиком состояния —
		//   эталонная запись `эффект+0x2A8` (dword-индекс 170) сразу после спавна: 1 = весь эффект повторяется
		//   каждый период контейнера (оглушение `shocku01` раз в 2 с, яд `dot301`, молчание, паника, `firtim`,
		//   `timbom`, `briec` игрока), 3 = вечный до снятия состояния (заморозки, сон, кровотечение, `straim`,
		//   `flamec`, `defsr`, `cridn`, `evadw`, `attsdw`, `movdw`, `defdw2`, `defdwe`, `briec` мобов, ледяные
		//   стрелы). У всех этих `.jeb` контейнер на диске 0 — без переопределения эффект отыгрывал один раз и
		//   гас посреди состояния. Значения — поле `Loop` программы `DT_BuffFx` (88 записей, сверены со сканом
		//   машинного кода: 38 функций-обработчиков, `Reports/owner_1008/fx_loop_fixes.md` §1).
		//   Ставится ДО срока жизни ниже: `NaturalLifeSpan` учитывает переопределённый контейнер.
		if (S.Loop >= 0) { Fx->SetLoopContainerEndOverride(S.Loop); }

		if (S.Slot == 0)      { E.Fx0 = Fx; }
		else if (S.Slot == 1) { E.Fx1 = Fx; }
		else
		{
			// Хэндл эталон НЕ сохраняет ⇒ гасить нечем и не надо: разовая вспышка догорает сама.
			//   Время жизни — ЕДИНОЙ формулой актора (самый длинный эмиттер; ★ 08.10.2026 — и не короче
			//   контейнера 0/2, МЕЛК-1 ревизии fx_loop). Прежняя копия формулы здесь давала то же
			//   `Clamp(max(StartDelay + Period) + 0.5, 1, 30)` без контейнера.
			Fx->SetLifeSpan(Fx->NaturalLifeSpan(0.5f, 1.f, 30.f));
		}
	}

	// Звук наложения состояния — `Audio_Play3DSound` в теле того же обработчика.
	if (Spawned > 0 && !Row->Wav.IsEmpty())
	{
		if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
		{
			Snd->PlaySound3DAs(FName(*FPaths::GetBaseFilename(Row->Wav).ToLower()), Host->GetActorLocation(), EAsdaSoundClass::Magic, Host);   // ★ 30.10: «магия»
		}
	}

	E.bFxSpawned = true;
	BuffFxWaitSince.Remove(AsdaBuffFxKey(Key, bMob, E.Id));
	if (Spawned > 0)
	{
		UE_LOG(LogAsdaBuff, Log,
			TEXT("[состояния-визуал] %s %d: состояние %d «%s» → эффектов %d (обработчик эталона %s)"),
			bMob ? TEXT("сущность rid") : TEXT("персонаж userIdx"), Key, E.Id, *Row->BuffName, Spawned,
			bMob ? *Row->MobFn : *Row->PlayerFn);
	}
	return true;
}

// ★★★ R5-fix6 (13.11.2026) [ОРИГИНАЛ]: ЭФФЕКТ МГНОВЕННОГО СОСТОЯНИЯ ИЗ ПАКЕТОВ HP 4058 / MP 4059.
//   Эталон: `netHandleHpMpUpdate@0xF43D00` (network.c:42767-42808) и `pkt_OnMpSkillFxUpdate@0xF440E0` (entity_char.c:53102-53138).
//     • тип 4 — игрок пакета (`CharManager_FindByUserIdx(*пакет)`) найден → по полю «навык» (это id СОСТОЯНИЯ) зовётся та же функция,
//       что стоит в таблице обработчиков состояний (skill_buff.c:1761-1788): 267 `charSpawnBleedEffect`, 268 `Char_SpawnBuffFx_Bleed`,
//       269 `Char_SpawnBuffFx_HpReco`, 275 `skillFx_manasu01`, 276 `SkillFx_hill01`, 296 `CharFx_SpawnBlossomEffect`; прочие id — ничего;
//     • тип 6 — эффект на СВОЁМ персонаже: у 4058 `SkillFx_hill01(свой, 276)` (внутри ветки «игрок пакета найден»), у 4059
//       `Char_SpawnBuffFx_HpReco(свой, 269)` (без условия).
//   Все шесть функций — разовый показ без сохранения хэндла, поэтому идёт та же программа `DT_BuffFx`, что у наложения состояния
//   (`SpawnBuffFx` с временной записью: слоты записей −1, эффект догорает сам). От дельты HP/MP показ не зависит.
//   Сервер шлёт тип 4 только самому игроку (GameServer.c:25350, `a6 == 2 || a6 == 4`), прочие типы — рассылкой.
//   Порт прежде рисовал по этим пакетам только цифру (`UAsdaWorldSubsystem::OnVitalsFx`): эффектов лечения, мгновенной потери
//   здоровья/маны, «Ментального шока» и «Похищения крови» от тиков не было.
//   ★ [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] НОСИТЕЛЬ ЭФФЕКТА ТИПА 6. Эталон играет его на своём персонаже, чей бы ни был пакет
//   (`CharManager_GetLocalPlayer`). А тип 6 сервер шлёт РАССЫЛКОЙ по сектору (GameServer.c:43556 `sub_46AB50` — восстановление
//   половины HP/MP в зоне, отправитель `sub_4C28F0`; только типы 2 и 4 идут одному игроку): у оригинала лечение соседа вспыхивало
//   на каждом, кто получил пакет. Порт (значение ключа 1) играет эффект на ИГРОКЕ ПАКЕТА — на том, чьё здоровье или мана
//   изменились; свой пакет даёт эффект на себе, как и раньше. Значение 2 — дословно оригинал (всегда на своём персонаже).
//   Ключ `asda.Fx.VitalsStateFx`: 1 = оригинал с исправленным носителем типа 6; 2 = дословно оригинал; 0 = как было (только цифра).
static ASDA_DEV_CVAR(int32, CVarAsdaFxVitalsStateFx, TEXT("asda.Fx.VitalsStateFx"), 1,
	TEXT("Эффект мгновенного состояния из пакетов HP 4058 / MP 4059 (тип 4 — состояния 267/268/269/275/276/296 у игрока пакета, тип 6 — ")
	TEXT("лечение 276 / мана 269): 1 = как оригинал (netHandleHpMpUpdate, pkt_OnMpSkillFxUpdate), эффект типа 6 — на игроке пакета ")
	TEXT("(дефект оригинала исправлен); 2 = дословно оригинал: эффект типа 6 всегда на своём персонаже; 0 = прежний порт: только цифра."),
	ECVF_Default);

void UAsdaEffectsSubsystem::OnVitalsStateFx(int32 PktUserIdx, uint8 UpdateType, int32 StateId, bool bMp)
{
	const int32 KeyV = CVarAsdaFxVitalsStateFx.GetValueOnGameThread();   // 0 = выкл, 1 = оригинал с исправленным носителем типа 6, 2 = дословно оригинал
	if (KeyV == 0) { return; }
	int32 Who = -1;   // чей эффект (userIdx)
	int32 Id = -1;    // id состояния, чья программа показа играется
	if (UpdateType == 4)
	{
		// Только шесть состояний `switch` эталона; прочие id — `default`, ничего.
		if (StateId != 267 && StateId != 268 && StateId != 269 && StateId != 275 && StateId != 276 && StateId != 296) { return; }
		Who = PktUserIdx;
		Id = StateId;
	}
	else if (UpdateType == 6)
	{
		// Дословно оригинал (ключ 2): 4058 — ветка стоит внутри «игрок пакета найден», 4059 — без условия; носитель — свой персонаж.
		// Исправление (ключ 1): носитель — игрок пакета; нет его в мире — показывать не на ком.
		USkeletalMeshComponent* PktSkel = nullptr;
		const bool bPktFound = ResolveHost(PktUserIdx, /*bMob=*/false, PktSkel) != nullptr;
		if (KeyV == 2)
		{
			if (!bMp && !bPktFound) { return; }
			Who = SelfUserIdx();
		}
		else
		{
			if (!bPktFound) { return; }
			Who = PktUserIdx;
		}
		Id = bMp ? 269 : 276;
	}
	else { return; }
	FAsdaEffectEntry Tmp;   // временная запись: хэндлы эталон не хранит, список состояний не трогаем
	Tmp.Id = Id;
	Tmp.Type = 0;
	Tmp.CasterUserIdx = PktUserIdx;
	const bool bDone = SpawnBuffFx(Tmp, Who, /*bMob=*/false);
	UE_LOG(LogAsdaBuff, Log, TEXT("[состояния-визуал] пакет %d тип %d: эффект состояния %d у userIdx=%d (игрок пакета %d) — %s"),
		bMp ? 4059 : 4058, (int32)UpdateType, Id, Who, PktUserIdx,
		!bBuffFxEnabled ? TEXT("показ состояний выключен — не играется")
			: (bDone ? TEXT("отыгран") : TEXT("носителя нет (эталон: персонаж не найден — ничего)")));
}

// Носитель мог ещё не заспавниться (5272 приходит и до появления соседа в AoI) — повторяем, пока
//   не получится, но не дольше BuffFxHostWaitSec, чтобы не крутить это вечно на ушедших.
void UAsdaEffectsSubsystem::RetryPendingBuffFx()
{
	if (!bBuffFxEnabled) { return; }
	const double Now = FPlatformTime::Seconds();
	auto Pass = [this, Now](TMap<int32, FAsdaEffectList>& Map, bool bMob)
	{
		for (TPair<int32, FAsdaEffectList>& It : Map)
		{
			for (FAsdaEffectEntry& E : It.Value.Items)
			{
				if (E.bFxSpawned || E.Type != 0) { continue; }
				const int64 K = AsdaBuffFxKey(It.Key, bMob, E.Id);
				const double* Since = BuffFxWaitSince.Find(K);
				if (!Since) { BuffFxWaitSince.Add(K, Now); Since = BuffFxWaitSince.Find(K); }
				if (Now - *Since > (double)FMath::Max(BuffFxHostWaitSec, 0.5f))
				{
					E.bFxSpawned = true;   // сдаёмся молча: носитель так и не появился
					BuffFxWaitSince.Remove(K);
					continue;
				}
				SpawnBuffFx(E, It.Key, bMob);
			}
		}
	};
	Pass(Lists, /*bMob=*/false);
	Pass(MobLists, /*bMob=*/true);
}
