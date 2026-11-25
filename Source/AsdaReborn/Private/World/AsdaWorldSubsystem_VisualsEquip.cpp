// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_VisualsEquip.cpp — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem_Visuals.cpp, дословный перенос): смена экипировки своего персонажа, примерочный оверрайд, кольцо друга, кольцо цели.
//   Общее для частей — в AsdaWorldSubsystem_Visuals_Private.h; шапка и реверс — в AsdaWorldSubsystem_Visuals.cpp.
#include "World/AsdaWorldSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"              // ★ 27.09.2026: плашка 221 для отказов 4007 (канал обработчика `Recv_EntityMove`)
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» — единая точка своего номера (E2, 14.09.2026)
#include "World/AsdaPlayerCharacter.h"
#include "Characters/AsdaCharAssembly.h"      // кости/attach-матрицы оружия «в руке ↔ за спиной»
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaGroundTrace.h"
#include "Materials/MaterialInterface.h"      // материал меша стрелы (у ..._1 ссылка битая)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
#include "UI/AsdaInventorySubsystem.h"       // надетое (контейнер 3): проверка сломанных вещей
#include "Items/AsdaGradeColor.h"            // ★ 05.11.2026: drop-палитра `CItemManager_GetDropColor` — имя предмета в строках подбора
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "Components/MeshComponent.h"          // подошва цели: низ ВИДИМОЙ геометрии (TargetSoleZ)
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"          // ★ плашка имени — тоже UMeshComponent, но не тело
#include "Engine/StaticMesh.h"
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "AsdaFxNames.h"                     // ★ дедуп №2 В1-C (F5/F6): теги эффектов персонажа, имена одиночных эффект-BP
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
#include "Login/AsdaLoginData.h"             // DecodeCp1251 (имена монстров в cp1251)
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "AsdaWorldSubsystem_Visuals_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem_Visuals*.cpp

// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] ЗАТОЧКА ПРИ ВОЗВРАТЕ ОРУЖИЯ БЕЗ НЕЁ (найдено самотестом ревизии). Магазин снаряжения 6800 на открытии
//   по эталону снимает с модели оружие и щит (`charApplyEquipPartsModels@0xC28440`, режим 1 → `ApplySelfEquipParts(9→0, 8→0)`),
//   и источник свечения своего (`SelfWeaponGrade/SelfShieldGrade`) обнулялся; на закрытии вещи возвращались БЕЗ заточки
//   (`Grade = −1` — «не знаю»), ветка «не знаю» оставляла обнулённое ⇒ после любого визита к торговцу снаряжением свечение
//   пропадало до переодевания. Эталон таких потерь не знает: свечение читает НАДЕТЫЙ инстанс (`CCharacter+1588`, слот 9/8,
//   байт +0x18), а не модель. 1 — заточка, не переданная вызывающим, берётся из надетого инстанса (контейнер 3, ячейка = слот,
//   `FAsdaItemInstance::GemGrade` +24), если там тот же предмет; 0 — как было.
static ASDA_DEV_CVAR(int32, CVarAsdaWeaponGlowGradeFromWorn,
	TEXT("asda.WeaponGlow.GradeFromWorn"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — оружие/щит, возвращённые на модель без заточки (закрытие магазина снаряжения), берут её из надетого "
	     "инстанса (контейнер 3, байт +24): свечение возвращается; 0 — как было (свечение пропадало до переодевания)."),
	ECVF_Default);

// ══════════════ ★★★ СМЕНА ЭКИПИРОВКИ СВОЕГО ПЕРСОНАЖА — МОДЕЛЬ (equip_change_fix.md) ══════════════
//
// ЭТАЛОН (полный путь, см. также шапку объявления):
//   5001 result=1 → `itemMoveApply@0xD607C0`
//     • приёмник = контейнер 3 → `inventoryApplySlotItem@0xD60BA0` case 3 →
//       `itemUI_ApplyEquipWithColor@0xC6B5B0` → `tagCHARACTER_SetPartsModelByType(self, itemType, id)`;
//     • источник = контейнер 3 → `slotItemDrop@0xD60EB0` case 3 →
//       `cAvatar_PreviewPartModel@0xC6B8A0`  → `tagCHARACTER_SetPartsModelByType(self, itemType, 0)`.
//   Раскладка типа части (`tagCHARACTER_SetPartsModelByType@0x9E18D0`, entity_char.c:10955):
//     0 → LoadHelmetPart, 1 → Chest, 2 → Pants, 3 → Shose, 4 → Gloves, 8 → Armor(щит), 9 → оружие.
//   Прочие номера (5/6/7 украшения, 10/11) в свитче отсутствуют ⇒ модель не меняют — и `slotItemDrop`
//   их явно отсекает (`if (a4 == 5 || a4 == 6 || a4 == 7 || a4 == 10 || a4 == 11)`).
// ══════════════ ★★★ ОБЩАЯ ТОЧКА «ПРИМЕРОЧНЫЙ ОВЕРРАЙД ЭКИПИРОВКИ» (см. объявление в .h) ═════════
FAsdaWornParts UAsdaWorldSubsystem::VisualSelfParts() const
{
	FAsdaWornParts V = SelfParts;
	if (EquipTryOn.Num() == 0) { return V; }
	for (const TPair<FName, TMap<int32, int32>>& Sys : EquipTryOn)
	{
		for (const TPair<int32, int32>& Kv : Sys.Value)
		{
			switch (Kv.Key)
			{
			case 0: V.Helmet = Kv.Value; break;
			case 1: V.Chest  = Kv.Value; break;
			case 2: V.Pants  = Kv.Value; break;
			case 3: V.Shoes  = Kv.Value; break;
			case 4: V.Gloves = Kv.Value; break;
			case 9: V.Weapon = Kv.Value; break;
			case 8: V.Shield = Kv.Value; break;   // ★ 25.10.2026: щит — ветка 8 эталонного свитча (`SetParts_Armor`)
			default: break;   // 5/6/7/10/11 модели не имеют — эталон их в свитче тоже не держит
			}
		}
	}
	return V;
}

void UAsdaWorldSubsystem::PushEquipTryOn(FName Owner, const TArray<FIntPoint>& SlotItemPairs)
{
	if (Owner.IsNone() || SlotItemPairs.Num() == 0) { return; }
	TMap<int32, int32>& M = EquipTryOn.FindOrAdd(Owner);
	M.Reset();
	for (const FIntPoint& P : SlotItemPairs) { M.Add(P.X, P.Y); }
	if (AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(LocalPlayerActor.Get()))
	{
		Me->InitAppearance(SelfSex, VisualSelfParts());
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("[примерка] «%s» надела оверрайд на %d слот(ов); всего систем в примерке: %d"),
		*Owner.ToString(), M.Num(), EquipTryOn.Num());
}

void UAsdaWorldSubsystem::PopEquipTryOn(FName Owner)
{
	if (EquipTryOn.Remove(Owner) == 0) { return; }
	if (AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(LocalPlayerActor.Get()))
	{
		Me->InitAppearance(SelfSex, VisualSelfParts());
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("[примерка] «%s» снята; осталось систем в примерке: %d"),
		*Owner.ToString(), EquipTryOn.Num());
}

void UAsdaWorldSubsystem::ApplySelfEquipPart(int32 EquipSlot, int32 ItemId, int32 Grade)
{
	// Одна часть — частный случай пакетной точки (см. её объявление в .h).
	TArray<FIntPoint> One;
	One.Emplace(EquipSlot, ItemId);
	ApplySelfEquipParts(One, Grade);
}

// ★★★ ПАКЕТНАЯ раскатка набора частей. Сверка снимка с контейнером 3 делается ОДИН раз на весь
//   набор — иначе каждая следующая часть затирала бы примеренную предыдущую (см. .h).
void UAsdaWorldSubsystem::ApplySelfEquipParts(const TArray<FIntPoint>& SlotItemPairs, int32 Grade)
{
	if (SlotItemPairs.Num() == 0) { return; }
	AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(LocalPlayerActor.Get());
	if (!Me)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[экип self] пакет из %d частей: павна ещё нет — снимок обновим без пересборки"),
			SlotItemPairs.Num());
	}
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;

	// ★★★ СВЕРКА СНИМКА С АВТОРИТЕТОМ. Носимые части при спавне берутся из записи char-list, а она —
	//   СНИМОК НА МОМЕНТ ВЫБОРА ПЕРСОНАЖА (и её может не быть вовсе: `RawSlotId != LastSelectedRawSlot`).
	//   Авторитет надетого — КОНТЕЙНЕР 3, он приезжает пакетом 1027 и дальше живёт по 5001. Если бы мы
	//   пересобирали модель по устаревшему снимку, «надел шлем» могло бы заодно снять с персонажа
	//   нагрудник и штаны. Причёску/лицо/пол контейнер 3 не несёт — их оставляем из снимка спавна.
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (Inv && Inv->IsContainerReceived(3))   // 1027 ещё не приезжал ⇒ контейнер пуст, сверять не с чем
	{
		auto Worn = [Inv](int32 Slot) -> int32
		{
			const FAsdaItemInstance* C = Inv->GetCell(3, Slot);
			return (C && !C->IsEmpty()) ? C->ItemCode : 0;
		};
		SelfParts.Helmet = Worn(0);
		SelfParts.Chest  = Worn(1);
		SelfParts.Pants  = Worn(2);
		SelfParts.Shoes  = Worn(3);
		SelfParts.Gloves = Worn(4);
		SelfParts.Weapon = Worn(9);
		SelfParts.Shield = Worn(8);   // ★ 25.10.2026: щит — тоже часть модели (`SetParts_Armor`)
	}

	bool bRebuildMesh = false;
	bool bAnyApplied  = false;
	for (const FIntPoint& Pair : SlotItemPairs)
	{
		const int32 EquipSlot = Pair.X;
		const int32 ItemId    = Pair.Y;
		// ★ 31.10.2026 (`asda.WeaponGlow.GradeFromWorn`): заточка не передана — из надетого инстанса того же предмета.
		int32 SlotGrade = Grade;
		if (SlotGrade < 0 && ItemId > 0 && (EquipSlot == 8 || EquipSlot == 9) && CVarAsdaWeaponGlowGradeFromWorn.GetValueOnGameThread() != 0)
		{
			const UAsdaInventorySubsystem* WornInv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			const FAsdaItemInstance* Worn = WornInv ? WornInv->GetCell(3, EquipSlot) : nullptr;
			if (Worn && Worn->ItemCode == ItemId) { SlotGrade = (int32)Worn->GemGrade; }
		}
		switch (EquipSlot)
		{
		case 0: SelfParts.Helmet = ItemId; bRebuildMesh = true; break;
		case 1: SelfParts.Chest  = ItemId; bRebuildMesh = true; break;
		case 2: SelfParts.Pants  = ItemId; bRebuildMesh = true; break;
		case 3: SelfParts.Shoes  = ItemId; bRebuildMesh = true; break;
		case 4: SelfParts.Gloves = ItemId; bRebuildMesh = true; break;
		case 9:
			SelfParts.Weapon = ItemId;
			bRebuildMesh = true;
			if (Net) { Net->SelfWeaponItemId = ItemId; if (SlotGrade >= 0) { Net->SelfWeaponGrade = ItemId > 0 ? SlotGrade : 0; } else if (ItemId <= 0) { Net->SelfWeaponGrade = 0; } }
			break;
		case 8:
			// ★ 25.10.2026 [ОРИГИНАЛ] Щит: эталон `itemUI_ApplyEquipWithColor` → `tagCHARACTER_SetPartsModelByType(self, 8, id)`
			//   → `CCharacter_SetParts_Armor` — МЕШ щита на "_7_a1" (снятие — та же ветка с id 0). Источник свечения заточки
			//   (`SelfShieldItemId/Grade`) обновляется как прежде.
			SelfParts.Shield = ItemId;
			bRebuildMesh = true;
			if (Net) { Net->SelfShieldItemId = ItemId; if (SlotGrade >= 0) { Net->SelfShieldGrade = ItemId > 0 ? SlotGrade : 0; } else if (ItemId <= 0) { Net->SelfShieldGrade = 0; } }
			break;
		default:
			// 5/6/7 — украшения, 10/11 — служебные слоты: у эталона модели нет вовсе.
			UE_LOG(LogAsdaWorld, Log,
				TEXT("[экип self] слот %d (предмет %d) модель не меняет — у эталона в `tagCHARACTER_SetPartsModelByType` этой ветки нет"),
				EquipSlot, ItemId);
			continue;
		}
		bAnyApplied = true;

		// ★★★ СНИМОК char-list ТОЖЕ СТАРЕЕТ. `RefreshSelfWeaponGlow` при расхождении «запись char-list ≠
		//   текущее оружие» доверяет char-list (так лечили баг 2026-07-31 «на себе эффектов нет вообще»).
		//   После смены оружия в мире этот снимок УЖЕ УСТАРЕЛ — и свечение строилось бы для СНЯТОГО
		//   клинка, а после снятия оружия возвращалось бы вовсе. Обновляем снимок вместе с моделью:
		//   склад по-прежнему один, просто он теперь синхронный.
		if (Net && (EquipSlot == 8 || EquipSlot == 9 || (EquipSlot >= 0 && EquipSlot <= 4)))
		{
			// ★ 30.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Possess.BodyIdentity`): запись — общей точкой. При одержимости её нет: раньше вещи ТЕЛА
			//   записывались в запись СВОЕГО персонажа с тем же номером слота.
			const FAsdaCharRecord* SelRec = AsdaSelf::FindSelectedCharRecord(Net);
			for (FAsdaCharRecord& R : Net->CharList)
			{
				if (&R != SelRec) { continue; }
				if (R.Equip.IsValidIndex(EquipSlot))
				{
					R.Equip[EquipSlot].ItemId  = ItemId;
					R.Equip[EquipSlot].Enchant = (ItemId > 0 && SlotGrade >= 0) ? SlotGrade : 0;
				}
				break;
			}
		}
	}
	if (!bAnyApplied) { return; }

	if (Me && bRebuildMesh)
	{
		// Пересборка ОДНИМ входом (`InitAppearance` → `BuildWornParts`): он с этого круга
		//   идемпотентен — сначала снимает прежние части (`UAsdaCharAssembly::ClearWornParts`),
		//   потом надевает актуальный набор, и не сбивает играющий клип (базовый меш не меняется).
		// ★ Модель ИГРОКА собираем снимком С УЧЁТОМ примерочных оверрайдов (`PushEquipTryOn`):
		//   сам снимок остаётся зеркалом авторитета, примерка живёт отдельным слоем.
		Me->InitAppearance(SelfSex, VisualSelfParts());
	}
	// Свечение заточки пересобираем ВСЕГДА: меш оружия/щита только что пересоздан, старые эффекты
	//   висели на уничтоженном компоненте (закон `weapon-glow-saga`: начало = кисть).
	RefreshSelfWeaponGlow();
	FString Applied;
	for (const FIntPoint& Pair : SlotItemPairs) { Applied += FString::Printf(TEXT(" %d→%d"), Pair.X, Pair.Y); }
	UE_LOG(LogAsdaWorld, Log,
		TEXT("[экип self] части%s (заточка %d): модель %s"),
		*Applied, Grade, bRebuildMesh ? TEXT("пересобрана") : TEXT("не трогается (только аксессуары)"));
}

void UAsdaWorldSubsystem::ApplySoulRingToActor(AActor* Ch, bool bOn)
{
	if (!Ch) { return; }
	static const FName kRingTag(AsdaCharFxTags::kSoulRing);   // ★ В1-C F6
	AsdaCharFxTags::DestroyAttached(Ch, kRingTag);
	if (!bOn) { return; }
	// ★ ВЫСОТА КОЛЬЦА (фикс 2026-07-30, §H10). Эталон playerUpdateSelectedItemEffect@0xA382D0:
	//   boneIdx = self[52854] = 0 → CClass_GetItemByIndex(pModelObj + animSlot*64, 0) = ПЕРВАЯ кость списка,
	//   позиция = (живая матрица кости × charWorld).трансляция, затем к game-Y прибавляется flt_13998C0
	//   (= 15.0, сверено в live.dmp) и CEffect_SetPos — ТОЛЬКО позиция, ориентация от самого .jeb.
	//   ЗАМЕР дампа: кость[0] живой модели = "M" на высоте 83.2 (ТАЗ), а НЕ корневая кость у ног ⇒
	//   кольцо живёт на 83+15 = ~98 см = ПОЯС. Порт ставил его от актора (ступни) +15 см — «в ногах».
	FName Bone(TEXT("M"));
	if (const USkeletalMeshComponent* Sk = Ch->FindComponentByClass<USkeletalMeshComponent>())
	{
		if (Sk->GetBoneIndex(Bone) == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SoulRing] кости '%s' нет в '%s' — кольцо останется у ног"),
				*Bone.ToString(), *GetNameSafe(Sk->GetSkeletalMeshAsset()));
			Bone = NAME_None;
		}
	}
	// ★ Аудит №3 (A11): привязка — через единую точку. Кольцо — единственный потребитель, который
	//   раньше полагался на СЫРОЙ блок внутри `SpawnCharFx` и НЕ вызывал `ConfigureBoneAttach`.
	//   Режим `BonePos` = attach-7 (`CEffect_SetPos` по позиции кости + Offset, ориентация из .jeb) —
	//   ровно то, что делал сырой блок, но с явными флагами режима.
	// ★ Дедуп №2 В1-C (F12): «+15» — общая константа эталонной поправки attach-7 (была литералом ×2).
	if (AActor* Fx = SpawnCharFx(TEXT("eff_1_char_99_soullink01-a_00"), Ch, FVector(0.f, 0.f, AAsdaEffectActor::kEtalonTrackRiseZ), kRingTag))
	{
		if (Bone != NAME_None)   // кости нет — кольцо едет с актором как attached (прежнее поведение)
		{
			if (AAsdaEffectActor* E = Cast<AAsdaEffectActor>(Fx))
			{
				E->ConfigureBoneAttach(Ch, /*Skel*/nullptr, Bone, EAsdaFxAttachMode::BonePos, FVector(0.f, 0.f, AAsdaEffectActor::kEtalonTrackRiseZ));
			}
		}
		// ★ 19.09.2026 (glow_fix5, С2): кольцо соулмейта в группу свечения НЕ входит — `bGlowWithCarrier` не ставится.
		//   Эталон `playerUpdateSelectedItemEffect@0xA382D0`: узел слота персонажа +0x336C0 идёт только в очередь
		//   0x3537FA8 (`CRenderQueue_AddNode`, ключ +150), её рисует шаг «5.1.5» — после сведения свечения.
	}
}

// ★ 19.09.2026: офлайн-проверка пункта «Свечение» — разбор у объявления. Пол крыльев — как у пакета 6585
//   (`Sex != 2` = мужской), носитель — только персонаж игрока.
void UAsdaWorldSubsystem::DebugApplyItemFx(AActor* Ch, int32 WeaponItemId, int32 WeaponGrade, int32 WingId, bool bRing)
{
	AAsdaPlayerCharacter* P = Cast<AAsdaPlayerCharacter>(Ch);
	if (!P) { UE_LOG(LogAsdaWorld, Warning, TEXT("[свечение-проверка] носитель не персонаж игрока — эффекты не повешены")); return; }
	if (WeaponItemId > 0) { ApplyWeaponGlowToActor(P, WeaponItemId, WeaponGrade, 0, 0, -1); }
	if (WingId > 0) { ApplyWingToActor(P, P->Sex != 2, WingId, true); }
	if (bRing) { ApplySoulRingToActor(P, true); }
	UE_LOG(LogAsdaWorld, Log, TEXT("[свечение-проверка] %s: заточка item=%d grade=%d, крылья %d, кольцо %d"),
		*P->GetName(), WeaponItemId, WeaponGrade, WingId, bRing ? 1 : 0);
}

// ★★★ ПОДЪЁМ КОЛЬЦА НАД ПОДОШВОЙ ЦЕЛИ. Число близнеца (`CSelectRing::Update@0x00A7CBE9`:
//   `pos.y += 10.0`, double @`0x15859B8`), вынесено в ключ, чтобы владелец мог поправить посадку
//   на живой сцене без пересборки. ОТСЧЁТ идёт от НИЗА ВИДИМОЙ ГЕОМЕТРИИ носителя — см. разбор
//   в `ApplyTargetRingToActor`.
static ASDA_DEV_CVAR(float, CVarAsdaTargetRingLift,
	TEXT("asda.Target.RingLift"), 10.f,
	TEXT("Высота кольца выбора НАД ПОДОШВОЙ цели, юниты (эталон близнеца = 10). ")
	TEXT("Отсчёт от низа видимой геометрии носителя, а не от начала координат актора."),
	ECVF_Default);

// ★★★ ПОДОШВА НОСИТЕЛЯ = НИЗ ЕГО ВИДИМОЙ ГЕОМЕТРИИ (а не начало координат актора).
//   ЗАЧЕМ (замечание владельца 2026-08-21: «высота должна быть в нижней части ног NPC»).
//   Кольцо крепится режимом `CharPos` — каждый кадр встаёт в `GetActorLocation() + Offset`.
//   ЗАМЕР (`Tools/_probe_target_ring_height.py`, карта alpen, лог
//   `Reports/headless__probe_target_ring_height.log`) показал, что начало координат актора
//   НЕ совпадает с подошвой и расходится ПО МОДЕЛЯМ: у обычных моделей низ геометрии лежит ниже
//   `Z` актора на 3.0 (волк) … 16.0 (Ian) юнита, а по всей карте разброс 0.1 … 118.5
//   (36 NPC и 387 мобов) — то есть один и тот же подъём «+10» давал у разных целей разную высоту
//   над ступнями (13…26 у обычных). Это и есть «кольцо плавает у высоких/низких».
//   У близнеца такой беды нет: его `pos = target[0x158]` — это «точка стояния» сущности
//   (та же, что делится на 100 и даёт тайл серверной сетки, `0x00A8154D`), то есть ЗЕМЛЯ.
//
//   ★ СЧИТАЕМ ТОЛЬКО ПО ВИДИМЫМ МЕШ-КОМПОНЕНТАМ, а не по `GetActorBounds`.
//   `GetActorBounds(bOnlyColliding=false)` складывает габариты ВСЕХ зарегистрированных
//   примитивов, включая невидимые: капсулу пикинга, виджет плашки имени и СКРЫТУЮ квестовую
//   метку с её обводкой (у метки `bUsingAbsoluteRotation`, и в редакторской позе её габариты
//   уезжают куда угодно). Живой прогон это и поймал: у `AsdaNpcActor_67` общий низ оказался на
//   54.8 ниже корня, хотя сама модель стоит на земле. Тело и надетые части — это `UMeshComponent`
//   (скелетал + статик), поэтому по ним и меряем; невидимые пропускаем.
//
//   КЛАМП «не выше начала координат»: у летающих/подвешенных моделей низ геометрии может
//   оказаться ВЫШЕ корня — тогда кольцо ушло бы в воздух; близнец в этом случае кладёт его на
//   позицию сущности (= земля), поэтому берём минимум из двух.
//   КЛАМП СНИЗУ (`kMaxSoleDrop`): защита от невалидных габаритов (меш ещё не построен, поза не
//   посчитана). Просадка глубже полуметра означает не «длинные ноги», а сбой замера — тогда
//   честнее вернуться к позиции актора, чем утопить кольцо под землю. Случай пишется в лог.
// ★★★ ЕДИНАЯ ТОЧКА «габариты по видимой геометрии» (разбор — у объявления в заголовке).
//   Ею пользуются и подошва кольца выбора, и якорь всплывающего текста
//   (`UAsdaFloatingTextSubsystem::SpawnOverActor`, порт эталонного `soundPos`).
bool UAsdaWorldSubsystem::VisibleMeshBounds(const AActor* A, FVector& OutOrigin, FVector& OutExtent)
{
	if (!A) { return false; }
	FBox Box(ForceInit);
	for (const UActorComponent* AC : A->GetComponents())
	{
		const UMeshComponent* MC = Cast<UMeshComponent>(AC);
		if (!MC || !MC->IsRegistered() || !MC->IsVisible()) { continue; }
		// ★ `UWidgetComponent` в UE наследуется от `UMeshComponent`, но телом сущности не является:
		//   плашка имени висит НАД головой (`WorldHeight = 200`) и растянула бы габариты вверх.
		//   Сейчас она рисуется экранным виджетом и невидима, то есть отбор ничего не меняет, —
		//   но правило пишем явно, чтобы включение мировой плашки не сдвинуло чужие расчёты.
		if (MC->IsA<UWidgetComponent>()) { continue; }
		const FBoxSphereBounds B = MC->Bounds;
		if (B.BoxExtent.Z <= 1.f) { continue; }           // вырожденные габариты в счёт не идут
		Box += B.GetBox();
	}
	if (!Box.IsValid) { return false; }
	Box.GetCenterAndExtents(OutOrigin, OutExtent);
	return true;
}

float UAsdaWorldSubsystem::TargetSoleZ(const AActor* Ch)
{
	if (!Ch) { return 0.f; }
	const float ActorZ = Ch->GetActorLocation().Z;
	FVector VmOrigin, VmExtent;
	if (!VisibleMeshBounds(Ch, VmOrigin, VmExtent)) { return ActorZ; }   // видимых мешей нет
	const float Bottom = (float)(VmOrigin.Z - VmExtent.Z);
	static constexpr float kMaxSoleDrop = 50.f;        // допустимая просадка низа под корень
	if (ActorZ - Bottom > kMaxSoleDrop)
	{
		UE_LOG(LogAsdaWorld, Warning,
			TEXT("[кольцо] у «%s» низ видимой геометрии на %.1f ниже корня — это не подошва, беру корень"),
			*GetNameSafe(Ch), ActorZ - Bottom);
		return ActorZ;
	}
	return FMath::Min(Bottom, ActorZ);
}

// ★★★ КОЛЬЦО ПОД ВЫБРАННОЙ ЦЕЛЬЮ (отступление от эталона по решению владельца — см. шапку метода
//   в заголовке). Форма 1:1 как у `ApplySoulRingToActor`: сначала снять прежнее по тегу, потом,
//   если надо, поставить новое. Тег ОДИН на оба цвета — иначе смена «моб → NPC» оставила бы старое.
// Мировой размер кольца выбранной цели. Нужен потому, что акторы монстров живут в масштабе 0.01
// и кольцо, унаследовав его, сжималось в точку — у NPC и игроков масштаб единичный, там было видно.
static ASDA_DEV_CVAR(float, CVarAsdaTargetRingScale,
	TEXT("asda.Target.RingScale"), 1.0f,
	TEXT("Мировой масштаб кольца выбранной цели (по умолчанию 1.0, не зависит от масштаба самой цели)."),
	ECVF_Default);

void UAsdaWorldSubsystem::ApplyTargetRingToActor(AActor* Ch, bool bOn, bool bHostile)
{
	if (!Ch) { return; }
	static const FName kTargetRingTag(AsdaCharFxTags::kTargetRing);   // ★ В1-C F6
	const TCHAR* Jeb = bHostile ? TEXT("unfriendlyselect") : TEXT("friendlyselect");
	// ★★★ ЗАЩИТА ОТ ПЕРЕСПАВНА = ЭТАЛОННОЕ ПРАВИЛО «ТА ЖЕ ЦЕЛЬ — НЕ ПЕРЕИГРЫВАТЬ».
	//   Близнец решает это в `0x00A819E0`: если ни вид цели (`this[0x2F6C]`), ни её идентификатор
	//   (`this[0x2F70]`) не изменились — функция возвращает 0 и НЕ зовёт ни `SetEnable`, ни
	//   vtbl[1]-`Restart`, то есть кольцо второй раз не проигрывается
	//   (`npc_target_ring_fix.md` §2.2). У нас роль этого сравнения играет пара «ранний выход
	//   `SetTarget` при `TargetActor == NewTarget`» + эта проверка.
	//   Практический повод не убирать её: на двойном клике движок присылает ДВА события
	//   (IE_Pressed + IE_DoubleClick, см. контроллер) ⇒ без неё кольцо за один двойной клик
	//   переспавнилось бы и проигралось дважды. Если нужное кольцо уже висит — не трогаем вовсе.
	//   ★ Отыгравший эффект остаётся жить как ЗАМЕРШИЙ на конечном кадре актор (`end_mode = 3`),
	//     то есть кольцо видно всё время, пока цель выбрана. Снимает его только эта же функция.
	if (bOn)
	{
		TArray<AActor*> Attached;
		Ch->GetAttachedActors(Attached);
		for (AActor* A : Attached)
		{
			const AAsdaEffectActor* E = Cast<AAsdaEffectActor>(A);
			if (E && A->ActorHasTag(kTargetRingTag) && E->EffectName == Jeb) { return; }
		}
	}
	AsdaCharFxTags::DestroyAttached(Ch, kTargetRingTag);
	if (!bOn) { return; }
	// ★★★ ПОДЪЁМ = 10.0 НАД ПОДОШВОЙ (число близнеца), А НЕ НАД НАЧАЛОМ КООРДИНАТ АКТОРА.
	//   Число: кадровая ветка кольца близнеца `CSelectRing::Update@0x00A7CB50` берёт позицию цели
	//   (`target+0x158`) и делает `pos.y += 10.0` — константа `0x15859B8` (double 10.0),
	//   game-ось Y = вверх. У близнеца `target+0x158` — позиция сущности НА ЗЕМЛЕ, то есть его
	//   «+10» отсчитывается ОТ СТУПНЕЙ.
	//   ★ ИСПРАВЛЕНО 2026-08-21 (замечание владельца «высота должна быть в нижней части ног»):
	//   у НАС `GetActorLocation()` ступням не равен — замер по alpen дал расхождение 4.7…16.0
	//   юнита, РАЗНОЕ У РАЗНЫХ МОДЕЛЕЙ (полный разбор — `UAsdaWorldSubsystem::TargetSoleZ` выше и
	//   `Reports/npc_target_ring_fix2.md` §1). Поэтому база — подошва, а не корень.
	//   ★★★ ИСПРАВЛЕНО 2026-08-25 (владелец: «кольцо у моба то работает, то не работает»).
	//   Подошва бралась из ГАБАРИТОВ видимых мешей, а у скелетала они пересчитываются каждый кадр
	//   ПО ТЕКУЩЕЙ ПОЗЕ: у бегущего или бьющего монстра низ геометрии уезжает вниз. В живом логе
	//   это видно прямо — один и тот же монстр (rid=49) дал подъём +3.4, потом −27.6, потом −18.1.
	//   Отрицательный подъём = кольцо под землёй, то есть «пропало».
	//   Берём высоту ЗЕМЛИ под целью — это и есть точка близнеца `target+0x158` («точка стояния»,
	//   та самая, что делится на 100 и даёт тайл серверной сетки), от которой он и делает `+10`.
	//   Земля не зависит ни от позы, ни от модели, ни от масштаба. Габариты остаются запасным
	//   путём — на случай, если под целью земли не нашлось (полёт над пропастью, дыра в сетке).
	// ★★★ 22.10.2026 СТАТИЧНЫЕ ОБЪЕКТЫ (доски, гробницы, камни): кольцо — под ЦЕНТРОМ короба выделения (кости ∪ модель,
	//   `AAsdaNpcActor::GetSelectBoxWorld`) и по его размеру: у таких моделей начало координат стоит у края/столба, и кольцо в нём
	//   висело сбоку от объекта, а размер 50 был меньше самой модели (жалоба владельца 22.10.2026). Обычные цели — как раньше.
	FVector Loc = Ch->GetActorLocation();
	FVector2D RingXY(0.f, 0.f);    // смещение кольца от начала координат цели по горизонтали
	float RingSize = 1.f;          // во сколько раз кольцо больше обычного (обод 50 ед. — полуразмер хитбокса NPC)
	if (const AAsdaNpcActor* StaticNpc = Cast<AAsdaNpcActor>(Ch))
	{
		FBox Sel;
		if (StaticNpc->GetSelectBoxWorld(Sel))
		{
			const FVector C = Sel.GetCenter();
			RingXY = FVector2D(C.X - Loc.X, C.Y - Loc.Y);
			RingSize = FMath::Max(1.f, FMath::Max(Sel.GetExtent().X, Sel.GetExtent().Y) / 50.f);
		}
	}
	const FVector RingAt(Loc.X + RingXY.X, Loc.Y + RingXY.Y, Loc.Z);
	const float GroundZ = AsdaGround::GroundZ(Ch->GetWorld(), (float)RingAt.X, (float)RingAt.Y, (float)Loc.Z, Ch);
	const bool  bGroundOk = FMath::IsFinite(GroundZ) && FMath::Abs(GroundZ - (float)Loc.Z) <= 400.f;
	const float BaseZ = bGroundOk ? GroundZ : TargetSoleZ(Ch);
	const FVector Lift(RingXY.X, RingXY.Y, (BaseZ - (float)Loc.Z)
		+ CVarAsdaTargetRingLift.GetValueOnGameThread());
	AActor* Fx = SpawnCharFx(Jeb, Ch, Lift, kTargetRingTag);
	// ★★★ КОЛЬЦО НЕ ДОЛЖНО ЗАВИСЕТЬ ОТ МАСШТАБА ЦЕЛИ (заявка владельца 24.08.2026: «кольцо есть
	//   у NPC, привяжи и к монстрам с игроками»). Причина, по которой у монстров его не было видно:
	//   их акторы живут в масштабе **0.01** (см. спавн монстра выше и память `mob-static-scale`),
	//   а эффект крепится к их корню — значит наследует этот масштаб и сжимается в сто раз,
	//   превращаясь в точку. У NPC и игроков масштаб единичный, поэтому там кольцо и было видно.
	//   Лечение: после крепления явно задаём кольцу МИРОВОЙ масштаб, а не относительный.
	if (Fx)
	{
		const float Want = FMath::Max(0.01f, CVarAsdaTargetRingScale.GetValueOnGameThread()) * RingSize;
		const FVector OwnerScale = Ch->GetActorScale3D();
		const FVector Safe(FMath::IsNearlyZero(OwnerScale.X) ? 1.f : OwnerScale.X,
		                   FMath::IsNearlyZero(OwnerScale.Y) ? 1.f : OwnerScale.Y,
		                   FMath::IsNearlyZero(OwnerScale.Z) ? 1.f : OwnerScale.Z);
		// Относительный масштаб подбираем так, чтобы мировой получился ровно `Want`.
		Fx->SetActorRelativeScale3D(FVector(Want / Safe.X, Want / Safe.Y, Want / Safe.Z));
		UE_LOG(LogAsdaWorld, Log,
			TEXT("[кольцо цели] «%s» на %s: масштаб цели %.3f → мировой масштаб кольца %.2f, подъём %.1f ")
			TEXT("(база: %s, земля %.1f, корень %.1f)"),
			Jeb, *Ch->GetName(), OwnerScale.X, Want, Lift.Z,
			bGroundOk ? TEXT("земля") : TEXT("габариты — земли не нашлось"), GroundZ, (float)Loc.Z);
	}
	if (!Fx)
	{
		// BP не собран — сборщик `Tools/asda_build_target_ring_fx.py`; SpawnCharFx уже написал в лог.
		return;
	}
	if (AAsdaEffectActor* E = Cast<AAsdaEffectActor>(Fx))
	{
		// attach-3/5/9/12: КАЖДЫЙ КАДР только позиция носителя, ориентация — своя (квад лежит в
		//   плоскости земли и от разворота цели зависеть не должен). Эта же ветка сама гасит эффект,
		//   когда носитель исчез из мира (`FollowActor` протух) — то есть «цель ушла» закрыто здесь,
		//   а не отдельным сторожем.
		E->ConfigureBoneAttach(Ch, /*Skel*/nullptr, NAME_None, EAsdaFxAttachMode::CharPos, Lift);
	}
}
