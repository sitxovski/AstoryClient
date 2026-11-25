// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_VisualsGlow.cpp — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem_Visuals.cpp, дословный перенос): свечение заточенного оружия/щита, дев-команды сверки, замер отставания эффектов.
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
#include "Npc/AsdaMobActor.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "Materials/MaterialInterface.h"      // материал меша стрелы (у ..._1 ссылка битая)
#include "World/AsdaFloatingTextSubsystem.h" // цифры урона (порт CFloatingText)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
#include "Items/AsdaItemTableSubsystem.h"    // тип оружия атакующего (импакт battk01..04)
#include "UI/AsdaInventorySubsystem.h"       // надетое (контейнер 3): проверка сломанных вещей
#include "Items/AsdaGradeColor.h"            // ★ 05.11.2026: drop-палитра `CItemManager_GetDropColor` — имя предмета в строках подбора
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "Resources/AsdaSoundSubsystem.h"    // звуки ударов (S_c_*normal1, фазовые звуки мобов)
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"          // ★ плашка имени — тоже UMeshComponent, но не тело
#include "Engine/StaticMesh.h"
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "AsdaFxNames.h"                     // ★ дедуп №2 В1-C (F5/F6): теги эффектов персонажа, имена одиночных эффект-BP
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
#include "Login/AsdaLoginData.h"             // DecodeCp1251 (имена монстров в cp1251)
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "UI/AsdaEquipShopSubsystem.h"     // ★ 31.10.2026: гейт свечения «открыт магазин снаряжения 6800» (sub_93B448(g_pShopWnd))
#include "UI/AsdaPremiumShopSubsystem.h"   // ★ 31.10.2026: гейт ауры щита «открыт элитный магазин 6000» (helper_getField2075)
#include "AsdaWorldSubsystem_Visuals_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem_Visuals*.cpp

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// ★ СВЕЧЕНИЕ ЗАТОЧЕННОГО ОРУЖИЯ/ЩИТА (полный реверс — Reports/weapon_glow_re.md)
//
// ЭТАЛОН — две функции, обе вызываются КАЖДЫЙ КАДР и для себя (entity_char.c:8077), и для каждого
//   видимого соседа (entity_char.c:8773):
//     CCharacter_UpdateHeldItemEffect@0xA4C070  — ОРУЖИЕ, экип-слот 9 (CCharacter+1588+57*9);
//     CCharacter_UpdateEquipAuraEffect@0xA4DD60 — ЩИТ,    экип-слот 8 (CCharacter+1588+57*8).
//   Обе читают 57-байтный item-инстанс: itemId dword @+0, УРОВЕНЬ ЗАТОЧКИ byte @+0x18.
//   Гейт: `if (itemId && grade >= 5)` — свечение начинается с +5, ниже эффект гасится.
//   Имя .jeb: CCharacter_BuildWeaponJebFileName@0xA4CB10 (оружие) / itemBuildJebEffectName@0xA4E790
//   (щит, база жёстко "shd"): "%s_%s_%s_%s_%s.jeb" = база_буква_редкость_скин_состояние.
//   Кость (эталон List_FindIndexByName по списку костей текущего анимслота):
//     оружие  — "8_a1"; ЛУКИ (itemSubType 8/10) — "_8_a1"; убранное за спину — "2_a1";
//     щит     — "_7_a1"; убранный за спину — "_2_a1".
//   Крепление: полный трансформ живой кости (эталон умножает worldMatrix на матрицу кости) —
//     в порту это bFollowBoneFull (attach-10), ровно как у крыльев.
// СОСТОЯНИЕ «убрано за спину» (CCharacter[1]+15160) в порт НЕ портировано (оружие всегда в руке),
//   поэтому используем ветку «в руке». Когда состояние появится — добавить вторую пару костей.
// ─────────────────────────────────────────────────────────────────────────────────────────────────
static const FName kWeaponGlowTag(AsdaCharFxTags::kWeaponGlow);   // ★ В1-C F6: общий тег (AsdaFxNames.h)
static const FName kShieldGlowTag(AsdaCharFxTags::kShieldGlow);

// ★ Отладочный форс уровня заточки (0 = выключено, штатное поведение). Позволяет увидеть свечение,
//   когда на сервере нет заточенных вещей: `asda.ForceWeaponGlowGrade 7` в консоли → все персонажи
//   получают указанную заточку на оружие/щит. Значение сохраняется только на сессию.
static int32 GAsdaForceWeaponGlowGrade = 0;
static ASDA_DEV_CVAR_REF(CVarAsdaForceWeaponGlowGrade,
	TEXT("asda.ForceWeaponGlowGrade"),
	GAsdaForceWeaponGlowGrade,
	TEXT("Отладка: форсировать уровень заточки (5/6/7+) для свечения оружия и щита. 0 = штатно."),
	ECVF_Cheat);

// ★★★ 31.10.2026 [ОРИГИНАЛ] СВЕЧЕНИЕ — СОСТОЯНИЕ КАДРА, А НЕ РАЗОВЫЙ СПАВН (баг игроков «свечение гаснет при фарме
//   и после смерти до переодевания»). Эталон зовёт `CCharacter_UpdateHeldItemEffect@0xA4C070` /
//   `CCharacter_UpdateEquipAuraEffect@0xA4DD60` КАЖДЫЙ КАДР (свой — entity_char.c:8076, соседи — :8772): гейт
//   `RenderCtx_GetEnable && !GM-скрыт(sub_93B218) && заточка>=5 && char[52136]` (:28379), а `char[52136]` пересчитывает
//   `charEntityUpdateTick@0xA1ED30` (:22650-22709) — гасят только рыбалка/копка 40/43, «занят» с флагами и невидимость.
//   Состояния СМЕРТИ в гейте нет: труп свечение держит. Погасший эффект (`gap_A42[700] = -1`) эталон в следующем
//   же кадре грузит заново (:28390). В порту свечение было разовым актором: смерть его уничтожала
//   (`AsdaPlayerCharacter_Move.cpp`, состояние Dead), на трупе строить запрещалось, а пересобрать после воскрешения
//   или после любого сноса/залипшего скрытия было некому — до смены зоны, перезахода или переодевания.
//   1 — свечение на трупе остаётся, сверка `AsdaWeaponGlowReconcile` возвращает пропавшее/залипшее; 0 — как было.
static ASDA_DEV_CVAR(int32, CVarAsdaWeaponGlowPersist,
	TEXT("asda.WeaponGlow.Persist"), 1,
	TEXT("[ОРИГИНАЛ] 1 — свечение заточки как у эталона пересчитывается постоянно: не гаснет на смерти, пропавшее или "
	     "оставшееся скрытым после показа носителя возвращается само; 0 — как было (гаснет на смерти до переодевания)."),
	ECVF_Default);

// Включена ли постоянная модель свечения (ключ `asda.WeaponGlow.Persist`); аксессор для других .cpp (имя ключа вне журналов не светим).
bool AsdaWeaponGlowPersistOn() { return CVarAsdaWeaponGlowPersist.GetValueOnGameThread() != 0; }

// ★★★ 31.10.2026 [ОРИГИНАЛ] ГЕЙТ МАГАЗИНОВ (ревизия координатора, Б3). Эталон в том же покадровом гейте:
//   оружие — `if (sub_93B448(g_pShopWnd)) [696] = 0` (entity_char.c:28377): открыт МАГАЗИН СНАРЯЖЕНИЯ 6800 (тот же признак
//   закрывает `shopWin_Close`, input.c:857) → `clearEffectMgr` эффекта, после закрытия — загрузка заново (`[700] = -1`);
//   щит — `sub_93B448(g_pShopWnd)` ИЛИ `helper_getField2075(g_pShopWnd)` (:28796-28799, ЭЛИТНЫЙ МАГАЗИН 6000). Признак общий,
//   гаснет у ВСЕХ персонажей, не только у своего. В порту: `UAsdaEquipShopSubsystem::IsOpen` / `UAsdaPremiumShopSubsystem::IsOpen`.
//   Не перенесено (своя система — примерка элитного магазина): подмена своего оружия предметом витрины
//   (`g_nShopPreviewItemId6`, :28332-28348) при `helper_getField2075`.
static ASDA_DEV_CVAR(int32, CVarAsdaWeaponGlowShopGate,
	TEXT("asda.WeaponGlow.ShopGate"), 1,
	TEXT("[ОРИГИНАЛ] 1 — пока открыт магазин снаряжения (6800) свечения оружия и щита нет ни у кого, у щита — и при элитном магазине "
	     "(6000); после закрытия эффект строится заново. 0 — как было (магазины свечение не трогают)."),
	ECVF_Default);

// Гейт магазинов эталона для свечения носителей: `bOutWeaponOff` — гасить свечение оружия, `bOutShieldOff` — ауру щита.
static void AsdaGlowShopGates(const UGameInstance* GI, bool& bOutWeaponOff, bool& bOutShieldOff)
{
	bOutWeaponOff = bOutShieldOff = false;
	if (!GI || CVarAsdaWeaponGlowShopGate.GetValueOnGameThread() == 0) { return; }
	const UAsdaEquipShopSubsystem* Eq = GI->GetSubsystem<UAsdaEquipShopSubsystem>();
	const UAsdaPremiumShopSubsystem* Pr = GI->GetSubsystem<UAsdaPremiumShopSubsystem>();
	const bool bEquipShop = Eq && Eq->IsOpen();      // sub_93B448(g_pShopWnd)
	const bool bElite = Pr && Pr->IsOpen();          // helper_getField2075(g_pShopWnd)
	bOutWeaponOff = bEquipShop;
	bOutShieldOff = bEquipShop || bElite;
}

// Последний заказ свечения носителя: что просили построить и что реально построилось (сверка возвращает только построенное,
//   чтобы не долбить спавн там, где эффекта нет по данным — нет кости/буквы/BP).
struct FAsdaGlowWant
{
	int32 WeaponItemId = 0;     // оружие (экип-слот 9)
	int32 WeaponGrade = 0;      // его заточка (инстанс +0x18)
	int32 ShieldItemId = 0;     // щит (экип-слот 8)
	int32 ShieldGrade = 0;      // его заточка
	int32 UserIdx = -1;         // для журнала
	bool bWeaponBuilt = false;  // свечение оружия построено последним заказом
	bool bShieldBuilt = false;  // аура щита построена последним заказом
	bool bWeaponGated = false;  // свечение оружия снято гейтом магазина — после закрытия строить заново
	bool bShieldGated = false;  // аура щита снята гейтом магазина
};
// Заказы по носителям (слабые ключи: ушедший актор вычищается при следующем заказе).
static TMap<TWeakObjectPtr<AActor>, FAsdaGlowWant> GAsdaGlowWants;
// ★ Ревизия Б1: какую часть пересобирает `ApplyWeaponGlowToActor` — 0 обе (штатный заказ), 1 только оружие, 2 только щит.
//   Ставит сверка на время своего вызова, чтобы пропажа одной части не перезапускала другую (видимое мигание).
static int32 GAsdaGlowOnlyPart = 0;

// ★ ЖИВОЙ ЧИСЛЕННЫЙ ЗАМЕР ПОСАДКИ СВЕЧЕНИЯ (`asda.VerifyWeaponGlow`). Нужен потому, что в headless
//   расхождение НЕ воспроизводится: там у актора ровно ОДНА скелеталка и НЕТ камеры (billboard-ветка
//   квадов не исполняется), а в игре и то и другое иначе.
// ★ МЕТРИКА v2 (2026-07-31, после первого живого прогона). Первая версия мерила расстояние до
//   БЕСКОНЕЧНОЙ ПРЯМОЙ и печатала «вдоль» даже когда эмиттер уходил в противоположную сторону —
//   так делать нельзя. Теперь всё считается в СЫРЫХ КЛИЕНТСКИХ единицах (тех же, в которых авторены
//   и .jbm оружия, и .jeb свечения — доказано дампом: нода меша оружия имеет ЕДИНИЧНУЮ локальную
//   матрицу, эффект сидит на той же кости):
//     raw = ( localX/100,  localZ/100,  −localY/100 )        // импорт .jbm: glTF=(x,y,−z), UE=(x,−z,y)·100
//   Ось «вдоль оружия» = самая длинная сырая ось меша, ЗНАК выбран «от хвата (кости) к ДАЛЬНЕМУ концу
//   меша». Печатается: диапазон самого меша по этой оси, t каждой секции (СО ЗНАКОМ), поперечный
//   радиус, доля вершин секции внутри цилиндра меша и явный вердикт.
// ★ ЖИВОЙ ПОЛНЫЙ ДАМП СВЕЧЕНИЯ (`asda.VerifyWeaponGlow`, версия 4 — 2026-07-31).
//   ЗАЧЕМ ПЕРЕПИСАН: версии 1-3 мерили ТОЛЬКО «центр полотна против бокса меша оружия» и трижды
//   отрапортовали «всё на оружии», тогда как глазами свечение бьёт ИЗ КИСТИ. Значит мерилось не то,
//   что рисуется, либо рисуется что-то ЕЩЁ. Версия 4 печатает ВСЮ картину без скриншотов:
//     • сколько эффект-акторов висит на персонаже и с какими тегами (ловит ДУБЛИ);
//     • Loc/Rot/Scale КАЖДОГО актора + мировой скейл его корня (ловит схлопывание в точку);
//     • мировой трансформ кости хвата и меша оружия (ловит расхождение «эффект на кости / меш смещён»);
//     • для КАЖДОЙ секции: мировой центр, габарит, 4 угла первого квада, И — главное — ОБРАТНЫЙ
//       ПЕРЕСЧЁТ мировых вершин в СЫРЫЕ FX-КООРДИНАТЫ (game-оси .jeb) через ту же матрицу
//       A = FxToBone(swapXY×100)·BoneCS·Comp, которой рантайм сажает эффект. Это позволяет сравнить
//       напрямую с ЧИСЛОМ ИЗ .jeb (pos-канал узла) — если в .jeb pos=(2.2,0,90), а обратный пересчёт
//       даёт ~(0,0,0), значит pos-канал в рантайме ТЕРЯЕТСЯ и полотно сидит в кисти.
//   Эталонная точка отсчёта (CCharacter_UpdateHeldItemEffect@0xA4C070, entity_char.c:28277):
//     матрица эффекта = boneLive("8_a1") × charWorld, т.е. НАЧАЛО КООРДИНАТ ЭФФЕКТА = КИСТЬ,
//     а на клинок полотна выносит ТОЛЬКО pos-канал узлов (.jeb: z=20…170 «вдоль оружия»).
//     Поэтому «светится из кисти» == pos-канал не доехал до вершин.
// ★ ОСЬ МЕША ОРУЖИЯ В МИРЕ (общая для обеих проверок). Для режима «ЗА СПИНОЙ» это ЕДИНСТВЕННЫЙ
//   численный критерий: эталон кладёт меш и его свечение в ОДНУ матрицу, значит полотна обязаны
//   лежать вдоль оси меша и в режиме «в руке», и в режиме «за спиной».
//   Ось = самая длинная ЛОКАЛЬНАЯ ось bbox меша (у .jbm-оружия длина идёт по локальному −Y),
//   знак — от точки хвата (origin компонента) к дальнему торцу.
static bool AsdaWeaponWorldAxis(const UStaticMeshComponent* WC, FVector& OutGrip, FVector& OutAxis, double& OutLen)
{
	if (!WC || !WC->GetStaticMesh()) { return false; }
	const FBox LB = WC->GetStaticMesh()->GetBoundingBox();
	const FVector Ext = LB.Max - LB.Min;
	const int32 C = (Ext.X >= Ext.Y && Ext.X >= Ext.Z) ? 0 : ((Ext.Y >= Ext.Z) ? 1 : 2);
	const double Lo = LB.Min[C], Hi = LB.Max[C];
	FVector Dir(0, 0, 0);
	Dir[C] = (FMath::Abs(Lo) > FMath::Abs(Hi)) ? -1.0 : 1.0;   // дальний торец от origin (= хват)
	const FTransform WT = WC->GetComponentTransform();
	OutGrip = WT.GetLocation();
	OutAxis = WT.TransformVector(Dir).GetSafeNormal();
	OutLen  = FMath::Max(FMath::Abs(Lo), FMath::Abs(Hi)) * FMath::Abs(WT.GetScale3D()[C]);
	return !OutAxis.IsNearlyZero();
}

// ★ МИРОВАЯ ОСЬ ЛУЧА СВЕЧЕНИЯ. pos-канал .jeb направлен «вдоль оружия» по game +Z; импортёр хранит
//   fx-вектора как to_ue_v(g) = (−g.z, g.x, g.y) ⇒ в локальных осях актора эффекта это −X.
static FVector AsdaGlowBeamAxis(const AActor* Fx)
{
	return Fx ? Fx->GetActorQuat().RotateVector(FVector(-1, 0, 0)).GetSafeNormal() : FVector::ZeroVector;
}

static void AsdaVerifyWeaponGlowCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (!World) { UE_LOG(LogAsdaWorld, Warning, TEXT("[замер] мира нет")); return; }
	static const FName kTagW(AsdaCharFxTags::kWeaponGlow);   // ★ В1-C F6
	static const FName kTagS(AsdaCharFxTags::kShieldGlow);
	auto V3 = [](const FVector& V) { return FString::Printf(TEXT("(%.1f,%.1f,%.1f)"), V.X, V.Y, V.Z); };

	int32 nChar = 0;
	for (TActorIterator<AAsdaPlayerCharacter> It(World); It; ++It)
	{
		AAsdaPlayerCharacter* Ch = *It;
		// ── эффект-акторы, ПРИВЯЗАННЫЕ к этому персонажу (владелец ИЛИ attach-родитель) ──────────────
		TArray<AAsdaEffectActor*> Fxs;
		for (TActorIterator<AAsdaEffectActor> Et(World); Et; ++Et)
		{
			AAsdaEffectActor* E = *Et;
			if (E && (E->GetOwner() == Ch || E->GetAttachParentActor() == Ch)) { Fxs.Add(E); }
		}
		if (Fxs.Num() == 0) { continue; }
		++nChar;

		USkeletalMeshComponent* Sk = Ch->BaseSkel;
		UStaticMeshComponent* WC = nullptr;
		for (UActorComponent* C : Ch->GetComponents())
		{
			if (UStaticMeshComponent* S = Cast<UStaticMeshComponent>(C))
			{ if (S->GetFName() == FName(TEXT("WeaponMesh"))) { WC = S; break; } }
		}
		UE_LOG(LogAsdaWorld, Warning, TEXT("[замер] ══════ персонаж %s  actorLoc=%s  actorScale=%s"),
			*Ch->GetName(), *V3(Ch->GetActorLocation()), *V3(Ch->GetActorScale3D()));
		if (Sk)
		{
			UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]   BaseSkel '%s': компонент loc=%s scale=%s (det<0 = зеркало)"),
				*GetNameSafe(Sk->GetSkeletalMeshAsset()), *V3(Sk->GetComponentLocation()), *V3(Sk->GetComponentScale()));
		}
		else { UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]   BaseSkel = NULL (!)")); }

		// ── меш оружия: мировой трансформ + дальний конец ───────────────────────────────────────────
		FVector WGripW = FVector::ZeroVector, WTipW = FVector::ZeroVector; bool bHaveWeapon = false;
		if (WC && WC->GetStaticMesh())
		{
			const FTransform WT = WC->GetComponentTransform();
			const FBox LB = WC->GetStaticMesh()->GetBoundingBox();
			double Best = -1.0; FVector BestC = FVector::ZeroVector;
			FBox WB(ForceInit);
			for (int32 c = 0; c < 8; ++c)
			{
				const FVector L(((c & 1) ? LB.Max.X : LB.Min.X), ((c & 2) ? LB.Max.Y : LB.Min.Y), ((c & 4) ? LB.Max.Z : LB.Min.Z));
				const FVector Wp = WT.TransformPosition(L);
				WB += Wp;
				const double D = FVector::Dist(Wp, WT.GetLocation());
				if (D > Best) { Best = D; BestC = Wp; }
			}
			WGripW = WT.GetLocation(); WTipW = BestC; bHaveWeapon = true;
			UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]   меш оружия '%s' (кость '%s'): хват(мир)=%s дальний конец=%s длина=%.1f  compScale=%s"),
				*GetNameSafe(WC->GetStaticMesh()), *WC->GetAttachSocketName().ToString(),
				*V3(WGripW), *V3(WTipW), Best, *V3(WC->GetComponentScale()));
			UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]   мир-бокс меша оружия: min=%s max=%s"), *V3(WB.Min), *V3(WB.Max));
		}
		// ★ ОСЬ МЕША В МИРЕ — эталон обязан положить полотна свечения ВДОЛЬ неё в ОБОИХ режимах.
		FVector WAxis = FVector::ZeroVector; double WAxLen = 0.0; bool bHaveAxis = false;
		{
			FVector G; FVector A; double L;
			if (AsdaWeaponWorldAxis(WC, G, A, L)) { WGripW = G; WAxis = A; WAxLen = L; bHaveAxis = true; }
		}
		if (bHaveAxis)
		{
			UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]   ОСЬ МЕША ОРУЖИЯ (мир): хват=%s направление=%s длина=%.1f  (режим: %s)"),
				*V3(WGripW), *V3(WAxis), WAxLen,
				(WC && WC->GetAttachSocketName() == FName(TEXT("2_a1"))) ? TEXT("ЗА СПИНОЙ") : TEXT("в руке"));
		}
		else { UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]   МЕША ОРУЖИЯ НЕТ (компонент 'WeaponMesh' не найден)")); }

		UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]   ЭФФЕКТ-АКТОРОВ НА ПЕРСОНАЖЕ: %d"), Fxs.Num());
		for (int32 fi = 0; fi < Fxs.Num(); ++fi)
		{
			AAsdaEffectActor* Fx = Fxs[fi];
			FString TagStr;
			for (const FName& T : Fx->Tags) { TagStr += (TagStr.IsEmpty() ? TEXT("") : TEXT(",")); TagStr += T.ToString(); }
			const bool bGlow = Fx->Tags.Contains(kTagW) || Fx->Tags.Contains(kTagS);
			UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]   ── FX#%d '%s' jeb='%s' теги=[%s]%s"),
				fi, *Fx->GetName(), *Fx->EffectName, *TagStr, bGlow ? TEXT("") : TEXT("   ← НЕ свечение"));
			const FRotator R = Fx->GetActorRotation();
			UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]      actor Loc=%s Rot=(P%.1f Y%.1f R%.1f) Scale=%s  rootCompScale(мир)=%s  EmitterScale=%.2f"),
				*V3(Fx->GetActorLocation()), R.Pitch, R.Yaw, R.Roll, *V3(Fx->GetActorScale3D()),
				*V3(Fx->GetRootComponent() ? Fx->GetRootComponent()->GetComponentScale() : FVector::OneVector),
				Fx->EffectEmitterScale);
			const USkeletalMeshComponent* FSk = Fx->FollowSkelComponent
				? ToRawPtr(Fx->FollowSkelComponent)
				: (Fx->FollowActor ? Fx->FollowActor->FindComponentByClass<USkeletalMeshComponent>() : nullptr);
			UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]      follow: кость='%s' skelComp='%s' (asset '%s') boneFull=%d strictAttach=%d followPos=%d baseUE=%d ofs=%s"),
				*Fx->FollowBone.ToString(), *GetNameSafe(FSk), *GetNameSafe(FSk ? FSk->GetSkeletalMeshAsset() : nullptr),
				Fx->bFollowBoneFull ? 1 : 0, Fx->bStrictQuadAttach ? 1 : 0, Fx->bFollowPosition ? 1 : 0,
				Fx->bBaseAlreadyUE ? 1 : 0, *V3(Fx->FollowOffset));
			// ★ ОСЬ ЛУЧА vs ОСЬ МЕША — главный вердикт (работает и «в руке», и «за спиной»).
			const FVector BeamAx = AsdaGlowBeamAxis(Fx);
			if (bHaveAxis && bGlow && !BeamAx.IsNearlyZero())
			{
				const double Dev = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp<double>(FVector::DotProduct(BeamAx, WAxis), -1.0, 1.0)));
				UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]      ОСЬ ЛУЧА (мир)=%s  ОТКЛОНЕНИЕ ОТ ОСИ МЕША=%.1f°  attachT=%s  → %s"),
					*V3(BeamAx), Dev, *V3(Fx->FollowBoneAttach.GetOrigin()),
					(Dev <= 12.0) ? TEXT("вдоль оружия — ОК")
					: (Dev >= 168.0) ? TEXT("!! ВДОЛЬ, НО В ОБРАТНУЮ СТОРОНУ !!")
					: TEXT("!! НЕ ВДОЛЬ ОРУЖИЯ !!"));
			}

			// ── ★ Д3: ТА ЖЕ матрица, что строит Tick — ComputeFollowBoneMatrix: A = FxToBone·FollowBoneAttach·
			//    BoneCS·Comp. Её инверсией переводим мировые вершины ОБРАТНО в сырые fx-координаты .jeb.
			//    ★ИСПРАВЛЕНО (Д3): раньше зонд считал матрицу БЕЗ FollowBoneAttach — при «оружие за спиной»
			//    он расходился с рантаймом и мог дать ложный вердикт; теперь единая точка = дубля нет.
			bool bHaveA = false; FMatrix Ainv = FMatrix::Identity; FVector BoneW = FVector::ZeroVector;
			FMatrix Aglow;
			if (FSk && Fx->FollowBone != NAME_None && Fx->ComputeFollowBoneMatrix(Aglow))
			{
				Ainv = Aglow.Inverse(); BoneW = Aglow.GetOrigin(); bHaveA = true;
				UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]      кость '%s' мир=%s  det(A)=%.4f  |actorLoc−кость|=%.3f%s"),
					*Fx->FollowBone.ToString(), *V3(BoneW), Aglow.Determinant(),
					FVector::Dist(Fx->GetActorLocation(), BoneW),
					bHaveWeapon ? *FString::Printf(TEXT("  |actorLoc−хват меша|=%.3f"), FVector::Dist(Fx->GetActorLocation(), WGripW)) : TEXT(""));
			}
			else if (FSk && Fx->FollowBone != NAME_None)
			{
				UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]      !! КОСТЬ '%s' В СКЕЛЕТЕ НЕ НАЙДЕНА — эффект замер в точке спавна"), *Fx->FollowBone.ToString());
			}

			UProceduralMeshComponent* Pmc = Fx->FindComponentByClass<UProceduralMeshComponent>();
			if (!Pmc) { UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]      ProceduralMesh НЕТ")); continue; }
			int32 nDrawn = 0;
			for (int32 i = 0; i < Fx->Emitters.Num(); ++i)
			{
				const FAsdaFxEmitter& E = Fx->Emitters[i];
				const FProcMeshSection* Sec = Pmc->GetProcMeshSection(i);
				const int32 NV = (Sec ? Sec->ProcVertexBuffer.Num() : 0);
				// авторские значения из .jeb (первый и последний узел)
				FVector P0(0), P1(0), B0(0); float SX0 = 0.f, SY0 = 0.f, A0 = 0.f;
				if (E.Nodes.Num() > 0)
				{
					P0 = E.Nodes[0].RingPosOfs; B0 = E.Nodes[0].RingBasePos;
					SX0 = E.Nodes[0].SizeX; SY0 = E.Nodes[0].SizeY; A0 = E.Nodes[0].Alpha;
					P1 = E.Nodes.Last().RingPosOfs;
				}
				if (NV == 0)
				{
					UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]      [%02d] %-14s type=0x%02X bb=%d quad=%d ring=%d  ВЕРШИН НЕТ (не рисуется)  .jeb pos=%s size=(%.0f,%.0f)"),
						i, *E.Name, E.JebType, E.BillboardMode, E.bSingleQuad ? 1 : 0, E.bRing ? 1 : 0, *V3(P0), SX0, SY0);
					continue;
				}
				++nDrawn;
				FBox WB(ForceInit); FVector Sum(0);
				FBox RB(ForceInit); FVector RSum(0);
				for (const FProcMeshVertex& V : Sec->ProcVertexBuffer)
				{
					WB += V.Position; Sum += V.Position;
					if (bHaveA) { const FVector Rw(Ainv.TransformPosition(V.Position)); RB += Rw; RSum += Rw; }
				}
				const FVector WCen = Sum / NV;
				const FVector WExt = WB.Max - WB.Min;
				UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]      [%02d] %-14s type=0x%02X bb=%d quad=%d ring=%d верш=%-4d  мир: центр=%s габарит=%s (макс %.0f юн)"),
					i, *E.Name, E.JebType, E.BillboardMode, E.bSingleQuad ? 1 : 0, E.bRing ? 1 : 0, NV,
					*V3(WCen), *V3(WExt), WExt.GetMax());
				UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]           .jeb: pos0=%s posN=%s base0=%s size=(%.0f,%.0f) alpha0=%.2f узлов=%d period=%.2f"),
					*V3(P0), *V3(P1), *V3(B0), SX0, SY0, A0, E.Nodes.Num(), E.Period);
				if (bHaveA)
				{
					// ★ ИСПРАВЛЕНИЕ МЕТРИКИ (v4.1). A⁻¹ возвращает координаты в STORED-UE фрейме
					//   (FxToBone на входе ждёт УЖЕ свопнутые game→UE координаты), а .jeb хранит
					//   GAME-оси. Версия 4.0 печатала UE-фрейм под подписью «сырые fx» и сравнивала
					//   его с game-числом — отсюда ложный вывод «применён лишний своп»
					//   (факт был ровно to_ue_v(pos) = (−z, x, y), т.е. свап применён РОВНО ОДИН РАЗ).
					//   Теперь возвращаемся в game-оси: from_ue_v(u) = (u.Y, u.Z, −u.X).
					auto ToGame = [](const FVector& U) { return FVector(U.Y, U.Z, -U.X); };
					const FVector RCenG = ToGame(RSum / NV);
					const FVector RExtG = ToGame(RB.Max - RB.Min).GetAbs();
					// Ожидание: pos-канал крутится КАНАЛЬНЫМИ вращениями (D3DX row-vector) — у квадов;
					//   у колец офсет добавляется ПОСЛЕ вращений (Gl = G + RingBasePos) ⇒ не крутится,
					//   но центр секции смещён на полуось цилиндра (height·scale/2).
					const float DegR2 = PI / 180.f;
					auto RXd2 = [](const FVector& P, float A) { const float c = FMath::Cos(A), s = FMath::Sin(A); return FVector(P.X, P.Y*c - P.Z*s, P.Y*s + P.Z*c); };
					auto RYd2 = [](const FVector& P, float A) { const float c = FMath::Cos(A), s = FMath::Sin(A); return FVector(P.X*c + P.Z*s, P.Y, -P.X*s + P.Z*c); };
					auto RZd2 = [](const FVector& P, float A) { const float c = FMath::Cos(A), s = FMath::Sin(A); return FVector(P.X*c - P.Y*s, P.X*s + P.Y*c, P.Z); };
					const FVector RotN = (E.Nodes.Num() > 0) ? E.Nodes[0].RingRot : FVector::ZeroVector;
					const float rx = RotN.X*DegR2, ry = RotN.Y*DegR2, rz = RotN.Z*DegR2;
					auto Rot1 = [&](const FVector& P) { return RZd2(RYd2(RXd2(P, rx), ry), rz); };
					FVector Pexp;
					if (E.bRing)
					{
						// ★ КАНОН 0xE (дизасм ring-Draw 0x1215C10): центр = basePos(сырой мир) + posChan·R²,
						//   а вершины идут через R ОДИН раз ⇒ центр СЕКЦИИ = якорь + половина оси цилиндра.
						//   B0 хранится с негацией z (её снимает эталонная ветка рантайма) — снимаем и здесь.
						const FVector PC(B0.X, B0.Y, -B0.Z);
						const FVector Anchor = Rot1(Rot1(PC));                       // R дважды — причуда оригинала
						const float H = (E.Nodes.Num() > 0) ? (E.Nodes[0].RingH * E.Nodes[0].RingScale) : 0.f;
						const FVector Axis = Rot1(FVector(0.f, H, 0.f));
						Pexp = Anchor + Axis * 0.5f + P0;                            // P0 = RingPosOfs = basePos (сырой мир)
					}
					else
					{
						Pexp = Rot1(P0 + B0);
					}
					const double DPos = FVector::Dist(RCenG, Pexp);
					const double Tol = E.bRing ? FMath::Max(15.0, 0.15 * RExtG.GetMax()) : FMath::Max(20.0, 0.30 * Pexp.Size());
					const TCHAR* Verd =
						(RCenG.Size() < 8.0 && Pexp.Size() > 15.0) ? TEXT("!!! СХЛОПНУЛОСЬ В КИСТЬ (офсет потерян) !!!")
						: (DPos > Tol) ? TEXT("!! ЦЕНТР НЕ ТАМ, ГДЕ .jeb !!")
						: TEXT("совпадает с .jeb");
					UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]           GAME-оси (.jeb, через A⁻¹): центр=%s габарит=%s  ожидалось=%s  Δ=%.1f (допуск %.1f)  → %s"),
						*V3(RCenG), *V3(RExtG), *V3(Pexp), DPos, Tol, Verd);
				}
				// 4 угла ПЕРВОГО квада секции — мир и сырые
				const int32 NC = FMath::Min(4, NV);
				FString CW, CR;
				for (int32 k = 0; k < NC; ++k)
				{
					CW += V3(Sec->ProcVertexBuffer[k].Position) + TEXT(" ");
					if (bHaveA) { const FVector U(Ainv.TransformPosition(Sec->ProcVertexBuffer[k].Position)); CR += V3(AsdaCoord::UeToGame3D(U)) + TEXT(" "); }   // ★ Аудит №3 (B1)
				}
				UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]           углы(мир): %s"), *CW);
				if (bHaveA) { UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]           углы(GAME-оси .jeb): %s"), *CR); }
				if (bHaveA)
				{
					UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]           расстояния: центр→кисть=%.1f%s"),
						FVector::Dist(WCen, BoneW),
						bHaveWeapon ? *FString::Printf(TEXT("  центр→конец оружия=%.1f  (длина оружия %.1f)"),
							FVector::Dist(WCen, WTipW), FVector::Dist(WGripW, WTipW)) : TEXT(""));
					// ★ проекция центра полотна на ось МЕША: t вдоль оружия (0=хват, ≈длина=остриё) и
					//   поперечный радиус. Для «за спиной» именно эта пара ловит «луч в другую сторону».
					if (bHaveAxis)
					{
						const FVector D = WCen - WGripW;
						const double T = FVector::DotProduct(D, WAxis);
						const double Rad = (D - WAxis * T).Size();
						UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]           вдоль оси меша: t=%+.1f (длина %.1f) поперёк=%.1f → %s"),
							T, WAxLen, Rad,
							(T < -0.15 * WAxLen) ? TEXT("!! ЗА ХВАТОМ (в обратную сторону) !!")
							: (Rad > FMath::Max(15.0, 0.35 * WAxLen)) ? TEXT("!! УЕХАЛО В СТОРОНУ ОТ ОРУЖИЯ !!")
							: TEXT("на оружии"));
					}
				}
			}
			UE_LOG(LogAsdaWorld, Warning, TEXT("[замер]      итог актора: эмиттеров=%d, рисуется секций=%d"), Fx->Emitters.Num(), nDrawn);
		}
	}
	UE_LOG(LogAsdaWorld, Warning, TEXT("[замер] персонажей с эффектами: %d"), nChar);
}

// ★★★ ДИАГНОСТИКА ПОЛА (`asda.Gender`) — печатает пол ИЗ КАЖДОГО ИСТОЧНИКА, чтобы расхождение
//   «список персонажей ↔ блок 1017 ↔ снапшот появления ↔ фактическая модель» было видно сразу.
//   Источники (см. AsdaNetSubsystem.h): 1017 блок+27 (эталонный, game_main.c:2801), 4005 body[37] /
//   4065 rec[54] (снапшот появления), char-list 0x3EE rec+22. Плюс фактические меши павнов.
FString UAsdaWorldSubsystem::DumpGenderDiagnostics() const
{
	auto Word = [](uint8 G) { return (G == 2) ? TEXT("ЖЕН") : (G == 1) ? TEXT("МУЖ") : TEXT("—"); };
	auto MeshOf = [](const AActor* A) -> FString
	{
		const AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(A);
		USkeletalMesh* M = (PC && PC->BaseSkel) ? PC->BaseSkel->GetSkeletalMeshAsset() : nullptr;
		return M ? M->GetName() : FString(TEXT("<нет меша>"));
	};

	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return TEXT("[пол] NetSubsystem недоступен"); }

	FString S = Net->DumpGenderSources();
	const AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(LocalPlayerActor.Get());
	S += FString::Printf(TEXT("  ФАКТ (свой павн): поле Sex=%u %s, базовый меш='%s'\n"),
		Me ? (uint32)Me->Sex : 0u, Me ? Word(Me->Sex) : TEXT("павна нет"),
		Me ? *MeshOf(Me) : TEXT("—"));
	S += TEXT("── СОСЕДИ (пол из снапшота: 4005 body[37] / 4065 rec[54]) ──\n");
	int32 N = 0;
	for (const TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		const AActor* A = It.Value.Actor.Get();
		S += FString::Printf(TEXT("  userIdx=%-5d '%s' пол=%u %s, базовый меш='%s'\n"),
			It.Key, *It.Value.Name, (uint32)It.Value.Sex, Word(It.Value.Sex),
			A ? *MeshOf(A) : TEXT("<актора нет>"));
		++N;
	}
	if (N == 0) { S += TEXT("  (соседей в AoI нет)\n"); }
	return S;
}

static void AsdaGenderDumpCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!WS) { UE_LOG(LogAsdaWorld, Warning, TEXT("[пол] WorldSubsystem недоступен")); return; }
	UE_LOG(LogAsdaWorld, Warning, TEXT("%s"), *WS->DumpGenderDiagnostics());
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaGenderDump(
	TEXT("asda.Gender"),
	TEXT("Пол персонажей из КАЖДОГО источника: char-list 0x3EE rec+22, блок 1017 +27, снапшот 4005/4065, фактический меш павна."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsdaGenderDumpCmd));
#endif // ASDA_DEV_CONSOLE

// ★ Живая проверка ГРУППОВОГО площадного баффа без подсистемы отряда: задать ростер вручную.
//   `asda.Party.Set 3 7` — считать согруппниками игроков с сессионными userNum 3 и 7;
//   `asda.Party.Set` без аргументов — очистить.
static void AsdaPartySetCmd(const TArray<FString>& Args, UWorld* World)
{
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!WS) { return; }
	TArray<int32> Idxs;
	for (const FString& A : Args) { if (A.IsNumeric()) { Idxs.Add(FCString::Atoi(*A)); } }
	WS->SetPartyRoster(Idxs);
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaPartySet(
	TEXT("asda.Party.Set"),
	TEXT("Ростер отряда для групповых площадных баффов (пока нет пакетов группы): asda.Party.Set <userNum…>."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsdaPartySetCmd));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaVerifyWeaponGlow(
	TEXT("asda.VerifyWeaponGlow"),
	TEXT("Полный дамп свечения: акторы/теги/трансформы/секции/углы + обратный пересчёт вершин в сырые fx-координаты .jeb."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsdaVerifyWeaponGlowCmd));
#endif // ASDA_DEV_CONSOLE

// ★ ЖИВАЯ ПРОВЕРКА «ОРУЖИЕ ЗА СПИНОЙ + БОЕВАЯ СТОЙКА» (`asda.VerifyWeaponStance`).
//   Печатает по КАЖДОМУ персонажу в мире таблицу «условие → набор клипов → где меш оружия (кость)»:
//   зона (бит 0x80 тайла .TER) / седло, боевая стойка, класс оружия, действующий набор _NN,
//   кость и локальный трансформ меша, кость и attach-матрица свечения. Сверяется с эталоном:
//     в руке  — "8_a1" (луки "_8_a1"), local = bind (0,46.3,0) UE;
//     убрано  — "2_a1" всегда; поправки (UAsdaCharAssembly::WeaponStowAttachRaw — ЕДИНЫЙ источник
//               для меша и свечения; оси кости UE, сырые единицы): класс 2/5 → Yaw−90
//               (4170,−3430,−140); класс 9 → Roll+90 (2000,−3000,−2500); класс 11 → Yaw−90
//               (1820,−4970,−140); луки 8/10 → меш (1000,−3500,0) / свечение (0,−3500,0);
//               классы 0/1/3/4 → поправки нет (bind).
//   ★ ГЛАВНОЕ ЧИСЛО режима «за спиной»: ОСЬ МЕША ОРУЖИЯ В МИРЕ и ОТКЛОНЕНИЕ ЛУЧА свечения от неё —
//     должно быть ~0°, ровно как в режиме «в руке» (эталон кладёт меш и свечение в ОДНУ матрицу).
static void AsdaVerifyWeaponStanceCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (!World) { return; }
	int32 N = 0;
	for (TActorIterator<AAsdaPlayerCharacter> It(World); It; ++It)
	{
		AAsdaPlayerCharacter* Ch = *It;
		if (!Ch) { continue; }
		++N;
		const FName WBone = Ch->CurrentWeaponBone();
		FString XfStr(TEXT("—"));
		const UStaticMeshComponent* WC = nullptr;
		TArray<UStaticMeshComponent*> SMs;
		Ch->GetComponents<UStaticMeshComponent>(SMs);
		for (const UStaticMeshComponent* C : SMs)
		{
			if (C && C->ComponentHasTag(UAsdaCharAssembly::WeaponMeshTag))
			{
				WC = C;
				const FTransform T = C->GetRelativeTransform();
				XfStr = FString::Printf(TEXT("loc=(%.1f,%.1f,%.1f) rot=(P%.0f Y%.0f R%.0f)"),
					T.GetLocation().X, T.GetLocation().Y, T.GetLocation().Z,
					T.Rotator().Pitch, T.Rotator().Yaw, T.Rotator().Roll);
				break;
			}
		}
		// ★ ОСЬ МЕША ОРУЖИЯ В МИРЕ — опора для проверки режима «ЗА СПИНОЙ».
		FVector WGrip = FVector::ZeroVector, WAxis = FVector::ZeroVector; double WAxLen = 0.0;
		const bool bHaveAxis = AsdaWeaponWorldAxis(WC, WGrip, WAxis, WAxLen);
		if (bHaveAxis)
		{
			UE_LOG(LogAsdaWorld, Warning, TEXT("[стойка]   ось меша оружия(мир): хват=(%.1f,%.1f,%.1f) направление=(%.3f,%.3f,%.3f) длина=%.1f"),
				WGrip.X, WGrip.Y, WGrip.Z, WAxis.X, WAxis.Y, WAxis.Z, WAxLen);
		}
		UE_LOG(LogAsdaWorld, Warning, TEXT("[стойка] %s: зона=%s бой=%s оружие=subType %d набор=_%02d меш на кости=%s %s"),
			*Ch->GetName(), Ch->bWeaponMotionHidden ? TEXT("город/седло") : TEXT("поле"),
			Ch->IsCombatStance() ? TEXT("да") : TEXT("нет"), Ch->EquippedWeaponSubType,
			Ch->CurMotionSet(), *WBone.ToString(), *XfStr);
		TArray<AActor*> Attached;
		Ch->GetAttachedActors(Attached);
		for (AActor* A : Attached)
		{
			const bool bW = A && A->ActorHasTag(FName(AsdaCharFxTags::kWeaponGlow));   // ★ В1-C F6
			const bool bS = A && A->ActorHasTag(FName(AsdaCharFxTags::kShieldGlow));
			if (!bW && !bS) { continue; }
			if (const AAsdaEffectActor* E = Cast<AAsdaEffectActor>(A))
			{
				const FVector Att = E->FollowBoneAttach.GetOrigin();
				UE_LOG(LogAsdaWorld, Warning, TEXT("[стойка]   свечение %s: кость=%s attachT=(%.1f,%.1f,%.1f) %s"),
					bS ? TEXT("щита") : TEXT("оружия"), *E->FollowBone.ToString(), Att.X, Att.Y, Att.Z,
					E->FollowBoneAttach.Equals(FMatrix::Identity, 0.001f) ? TEXT("(identity)") : TEXT("(поправка эталона)"));
				// ★ ГЛАВНЫЙ ВЕРДИКТ: луч обязан лежать вдоль оси меша оружия (эталон = одна матрица).
				const FVector BeamAx = AsdaGlowBeamAxis(E);
				if (bW && bHaveAxis && !BeamAx.IsNearlyZero())
				{
					const double Dev = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp<double>(FVector::DotProduct(BeamAx, WAxis), -1.0, 1.0)));
					const double DGrip = FVector::Dist(E->GetActorLocation(), WGrip);
					UE_LOG(LogAsdaWorld, Warning, TEXT("[стойка]     ось луча(мир)=(%.3f,%.3f,%.3f) отклонение от оси меша=%.1f° |начало луча−хват|=%.1f → %s"),
						BeamAx.X, BeamAx.Y, BeamAx.Z, Dev, DGrip,
						(Dev <= 12.0 && DGrip <= FMath::Max(10.0, 0.25 * WAxLen)) ? TEXT("СОВПАДАЕТ С ОРУЖИЕМ")
						: (Dev >= 168.0) ? TEXT("!! ЛУЧ В ОБРАТНУЮ СТОРОНУ !!")
						: TEXT("!! ЛУЧ НЕ ВДОЛЬ ОРУЖИЯ !!"));
				}
			}
		}
	}
	UE_LOG(LogAsdaWorld, Warning, TEXT("[стойка] персонажей в мире: %d"), N);
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaVerifyWeaponStance(
	TEXT("asda.VerifyWeaponStance"),
	TEXT("Дамп «условие → набор клипов → кость меша оружия/свечения» + состояние боевой стойки по всем персонажам."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsdaVerifyWeaponStanceCmd));
#endif // ASDA_DEV_CONSOLE

namespace AsdaFxLagProbe {
// ═════════════════════════════════════════════════════════════════════════════════════════════════
// ★ ЗАМЕР ОТСТАВАНИЯ ЭФФЕКТОВ, ПРИВЯЗАННЫХ К ПЕРСОНАЖУ (`asda.FxFollowLagProbe [сек]`).
//   ЗАЧЕМ: жалоба «эффекты оружия дёргаются и отстают при движении». Отставание = эффект строит свою
//   мировую геометрию по трансформу носителя ПРОШЛОГО кадра. Меряем ЧЕСТНО, В КОНЦЕ КАДРА (делегат
//   OnWorldPostActorTick — после ВСЕХ тик-групп, т.е. ровно та поза, с которой кадр уйдёт в рендер):
//     ошибка = | позиция актора эффекта  −  живая мировая позиция его кости/носителя |
//   Если эффект тикает до носителя, ошибка ≈ пройденному за кадр пути (скорость/FPS). Если после —
//   ошибка обязана быть нулевой (субпиксель). Печатается среднее/максимум и путь носителя за кадр.
// ═════════════════════════════════════════════════════════════════════════════════════════════════
	struct FEntry
	{
		TWeakObjectPtr<AAsdaEffectActor> Fx;
		double SumErr = 0.0, MaxErr = 0.0, SumStep = 0.0;
		int32 Frames = 0;
		FVector PrevTarget = FVector::ZeroVector;
		bool bHavePrev = false;
	};
	static FDelegateHandle Handle;
	static TArray<FEntry> Entries;
	static double EndTime = 0.0;
	static bool bQuitWhenDone = false;

	// Живая мировая точка носителя — ТА ЖЕ цепочка, что строит AAsdaEffectActor::Tick.
	static bool LiveTarget(const AAsdaEffectActor* Fx, FVector& Out)
	{
		if (!Fx) { return false; }
		// ★ Д3: attach-6/10 — через ЕДИНУЮ точку эффекта (та же матрица, что строит Tick), без дубля.
		if (Fx->bFollowCharWorld || Fx->bFollowBoneFull)
		{
			FVector Loc; FQuat Q;
			if (!Fx->ComputeFollowTargetWorld(Loc, Q)) { return false; }
			Out = Loc + Q.RotateVector(Fx->FollowOffset);
			return true;
		}
		// socket-follow (не attach-6/10): позиция сокета + offset.
		const USkeletalMeshComponent* Sk = Fx->FollowSkelComponent
			? ToRawPtr(Fx->FollowSkelComponent)
			: (Fx->FollowActor ? Fx->FollowActor->FindComponentByClass<USkeletalMeshComponent>() : nullptr);
		if (!Sk || Fx->FollowBone == NAME_None) { return false; }
		const int32 BI = Sk->GetBoneIndex(Fx->FollowBone);
		if (BI == INDEX_NONE || !Sk->GetComponentSpaceTransforms().IsValidIndex(BI)) { return false; }
		Out = Sk->GetSocketLocation(Fx->FollowBone) + Fx->FollowOffset;
		return true;
	}

	static void Report()
	{
		UE_LOG(LogAsdaWorld, Warning, TEXT("[fx-лаг] ── ИТОГ (кадров учтено на эффект) ──"));
		for (const FEntry& E : Entries)
		{
			const AAsdaEffectActor* Fx = E.Fx.Get();
			if (!Fx || E.Frames == 0) { continue; }
			UE_LOG(LogAsdaWorld, Warning,
				TEXT("[fx-лаг] %-28s jeb='%s' кость='%s' группа=%s кадров=%d  ОШИБКА ср=%.3f макс=%.3f  путь кости/кадр ср=%.3f  ⇒ %s"),
				*Fx->GetName(), *Fx->EffectName, *Fx->FollowBone.ToString(),
				Fx->PrimaryActorTick.TickGroup == TG_PostUpdateWork ? TEXT("PostUpdateWork") : TEXT("PrePhysics"),
				E.Frames, E.SumErr / E.Frames, E.MaxErr, E.SumStep / E.Frames,
				(E.MaxErr < 0.01) ? TEXT("СИНХРОННО") : TEXT("ОТСТАЁТ"));
		}
		Entries.Reset();
	}

	static void OnPostActorTick(UWorld* W, ELevelTick, float)
	{
		if (!W) { return; }
		for (FEntry& E : Entries)
		{
			AAsdaEffectActor* Fx = E.Fx.Get();
			FVector T;
			if (!Fx || !LiveTarget(Fx, T)) { continue; }
			const double Err = FVector::Dist(Fx->GetActorLocation(), T);
			E.SumErr += Err; E.MaxErr = FMath::Max(E.MaxErr, Err); ++E.Frames;
			if (E.bHavePrev) { E.SumStep += FVector::Dist(T, E.PrevTarget); }
			E.PrevTarget = T; E.bHavePrev = true;
		}
		if (W->GetTimeSeconds() >= EndTime)
		{
			FWorldDelegates::OnWorldPostActorTick.Remove(Handle);
			Handle.Reset();
			Report();
			if (bQuitWhenDone) { FGenericPlatformMisc::RequestExit(false); }
		}
	}

	// Запуск: собрать все эффекты-на-носителе и мерить Seconds секунд.
	static void Start(UWorld* W, float Seconds, bool bQuit)
	{
		if (!W) { return; }
		if (Handle.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(Handle); Handle.Reset(); }
		Entries.Reset();
		for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
		{
			AAsdaEffectActor* Fx = *It;
			if (!Fx || !Fx->bFollowPosition || !Fx->FollowActor) { continue; }
			FEntry E; E.Fx = Fx; Entries.Add(E);
		}
		UE_LOG(LogAsdaWorld, Warning, TEXT("[fx-лаг] замер %.1f с, эффектов-на-носителе: %d (asda.FxFollowTickFix=%d)"),
			Seconds, Entries.Num(), IConsoleManager::Get().FindConsoleVariable(TEXT("asda.FxFollowTickFix"))
				? IConsoleManager::Get().FindConsoleVariable(TEXT("asda.FxFollowTickFix"))->GetInt() : -1);
		bQuitWhenDone = bQuit;
		EndTime = W->GetTimeSeconds() + Seconds;
		Handle = FWorldDelegates::OnWorldPostActorTick.AddStatic(&OnPostActorTick);
	}
}

static void AsdaFxFollowLagProbeCmd(const TArray<FString>& Args, UWorld* World)
{
	float Sec = 2.f;
	bool bQuit = false;
	for (const FString& A : Args)
	{
		if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		else if (A.IsNumeric()) { Sec = FMath::Clamp(FCString::Atof(*A), 0.2f, 60.f); }
	}
	AsdaFxLagProbe::Start(World, Sec, bQuit);
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaFxFollowLagProbe(
	TEXT("asda.FxFollowLagProbe"),
	TEXT("Замер отставания эффектов на персонаже: |позиция эффекта − живая кость| в конце кадра. Аргументы: [секунды] [quit]."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsdaFxFollowLagProbeCmd));
#endif // ASDA_DEV_CONSOLE

// ★ HEADLESS-СЦЕНАРИЙ ЗАМЕРА (`asda.FxFollowLagTest [сек] [quit]`): спавнит персонажа со скелетом,
//   вешает на кость оружия эффект (attach-10, как свечение заточки), запускает бег и меряет зондом.
//   Нужен потому, что живой клиент требует сервера, а порядок тиков воспроизводится и в пустом мире.
static void AsdaFxFollowLagTestCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World) { return; }
	float Sec = 2.f; bool bQuit = false;
	for (const FString& A : Args)
	{
		if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		else if (A.IsNumeric()) { Sec = FMath::Clamp(FCString::Atof(*A), 0.2f, 60.f); }
	}
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAsdaPlayerCharacter* PC = World->SpawnActor<AAsdaPlayerCharacter>(AAsdaPlayerCharacter::StaticClass(),
		FVector::ZeroVector, FRotator::ZeroRotator, SP);
	if (!PC) { UE_LOG(LogAsdaWorld, Warning, TEXT("[fx-лаг] персонаж не заспавнился")); return; }
	// ★ 02.09.2026: путь — из общей точки `kAsdaCharPreviewBaseM` (закон `port-shared-points`).
	USkeletalMesh* SkM = UAsdaModelAnimSubsystem::LoadMeshChecked(kAsdaCharPreviewBaseM);
	if (SkM && PC->BaseSkel) { PC->BaseSkel->SetSkeletalMesh(SkM); }
	FName Bone(TEXT("8_a1"));
	if (PC->BaseSkel && PC->BaseSkel->GetBoneIndex(Bone) == INDEX_NONE)
	{
		Bone = (PC->BaseSkel->GetNumBones() > 1) ? PC->BaseSkel->GetBoneName(1) : NAME_None;
	}
	// Эффект: реальный BP свечения, если есть; иначе голый актор с одним пустым эмиттером (Tick требует эмиттер).
	AAsdaEffectActor* Fx = nullptr;
	// ★ В1-C F5: спавн одиночного BP — общей точкой `SpawnByBPName` (те же AlwaysSpawn и «без владельца», что у прежнего SP).
	Fx = AAsdaEffectActor::SpawnByBPName(World, AsdaFxBp::kLagTest, FVector::ZeroVector, FRotator::ZeroRotator, nullptr);
	if (!Fx)
	{
		Fx = World->SpawnActor<AAsdaEffectActor>(AAsdaEffectActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
		if (Fx && Fx->Emitters.Num() == 0) { Fx->Emitters.Add(FAsdaFxEmitter()); }
	}
	if (!Fx) { UE_LOG(LogAsdaWorld, Warning, TEXT("[fx-лаг] эффект не заспавнился")); return; }
	Fx->AttachToComponent(PC->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	Fx->CullDistance = 1.0e9f;                 // без камеры дистанц-кулл не должен глушить симуляцию
	// ★ Д1: attach-10 через ЕДИНУЮ точку (режим BoneFull). bFollowTickAfterPose=true = сам фикс
	//   порядка тиков (модулируется cvar asda.FxFollowTickFix через ApplyFollowTickOrder).
	Fx->ConfigureBoneAttach(PC, PC->BaseSkel, Bone, EAsdaFxAttachMode::BoneFull);
	PC->SetMoveDirection(FVector2D(1.f, 0.f), 0.35f, FVector2D::ZeroVector, false);   // бег 350 юн/с
	UE_LOG(LogAsdaWorld, Warning, TEXT("[fx-лаг] сценарий: персонаж '%s' скелет=%s кость='%s' эффект='%s'"),
		*PC->GetName(), *GetNameSafe(SkM), *Bone.ToString(), *Fx->GetName());
	AsdaFxLagProbe::Start(World, Sec, bQuit);
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaFxFollowLagTest(
	TEXT("asda.FxFollowLagTest"),
	TEXT("Headless-сценарий: персонаж + эффект на кости оружия + бег, затем замер отставания. Аргументы: [секунды] [quit]."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsdaFxFollowLagTestCmd));
#endif // ASDA_DEV_CONSOLE

// ★ волна 3 п.16 (15.09.2026): база имени по itemSubType оружия, буква, грейд и ступень — AsdaWeaponGlow (World/AsdaFxLightSettings.cpp):
//   одна таблица баз на C++, сверяемая с Python и JSON (Tools/asda_gen_fx_light_ini.py --check).
FString UAsdaWorldSubsystem::BuildGlowJebName(int32 ItemId, int32 Enhance, bool bShield) const
{
	// Порог заточки (эталон gradeThresh5_2 = 5 в обеих функциях).
	if (ItemId <= 0 || Enhance < AsdaWeaponGlow::MinEnhance) { return FString(); }
	// ★ Спец-предметы (эталон 0xA4CB10, ветка ДО всех таблиц): 41956/56048 → "test-000.jeb",
	//   и в UpdateHeldItemEffect они включают свечение независимо от заточки.
	if (!bShield && AsdaWeaponGlow::IsTestGlowItem(ItemId)) { return AsdaWeaponGlow::TestGlowJeb(); }

	UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return FString(); }
	const FAsdaItemRecord* Rec = IT->GetItem(ItemId);
	if (!Rec) { return FString(); }
	const FAsdaImgRecord* Img = IT->GetImgRecord(Rec->ImgIndex);
	if (!Img) { return FString(); }

	// ★ волна 3 п.16 (15.09.2026): база (оружие по подтипу / щит), буква imgSubType, грейд и ступень — одна формула AsdaWeaponGlow::ComposeJeb
	return AsdaWeaponGlow::ComposeJeb(bShield, Rec->ItemSubType, Img->ImgSubType, Rec->ItemGrade, Enhance);
}

void UAsdaWorldSubsystem::ApplyWeaponGlowToActor(AActor* Ch, int32 WeaponItemId, int32 WeaponGrade,
	int32 ShieldItemId, int32 ShieldGrade, int32 UserIdxForLog)
{
	if (!Ch) { return; }
	// ★ Ревизия Б1: сверка может пересобирать одну часть (`GAsdaGlowOnlyPart`); штатный заказ — обе.
	const bool bDoWeapon = GAsdaGlowOnlyPart != 2;   // пересобрать свечение оружия
	const bool bDoShield = GAsdaGlowOnlyPart != 1;   // пересобрать ауру щита
	// Всегда сначала гасим (эталон: при смене itemId/грейда сбрасывает хендл эффекта и строит заново).
	if (bDoWeapon) { AsdaCharFxTags::DestroyAttached(Ch, kWeaponGlowTag); }
	if (bDoShield) { AsdaCharFxTags::DestroyAttached(Ch, kShieldGlowTag); }

	// Заказ запоминаем ДО гейтов: сверка `AsdaWeaponGlowReconcile` повторяет именно его. Пересборка одной части прочие
	//   отметки не трогает.
	for (auto It = GAsdaGlowWants.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
	FAsdaGlowWant& Want = GAsdaGlowWants.FindOrAdd(TWeakObjectPtr<AActor>(Ch));
	Want.WeaponItemId = WeaponItemId; Want.WeaponGrade = WeaponGrade;
	Want.ShieldItemId = ShieldItemId; Want.ShieldGrade = ShieldGrade;
	Want.UserIdx = UserIdxForLog;
	if (bDoWeapon) { Want.bWeaponBuilt = false; Want.bWeaponGated = false; }
	if (bDoShield) { Want.bShieldBuilt = false; Want.bShieldGated = false; }
	bool bShopWeaponOff = false, bShopShieldOff = false;   // ★ ревизия Б3: гейт магазинов эталона (разбор у ключа ShopGate)
	AsdaGlowShopGates(GetGameInstance(), bShopWeaponOff, bShopShieldOff);

	// Прежний порт: труп свечения не носит. ★ 31.10.2026 [ОРИГИНАЛ] (`asda.WeaponGlow.Persist`): в гейте эталона
	//   (entity_char.c:28379) состояния смерти нет — при ключе 1 свечение строится и на трупе.
	if (const AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(Ch))
	{
		if (PC->AnimState == EAsdaPlayerState::Dead && !AsdaWeaponGlowPersistOn()) { return; }
	}
	if (GAsdaForceWeaponGlowGrade > 0)
	{
		if (WeaponItemId > 0) { WeaponGrade = GAsdaForceWeaponGlowGrade; }
		if (ShieldItemId > 0) { ShieldGrade = GAsdaForceWeaponGlowGrade; }
	}
	UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	// ★ Скелет носителя — ЯВНО BaseSkel (тот же, к которому AttachWeapon крепит МЕШ оружия), а не
	//   FindComponentByClass: у персонажа скелетных компонентов много и порядок обхода не определён.
	const AAsdaPlayerCharacter* PChSk = Cast<AAsdaPlayerCharacter>(Ch);
	const USkeletalMeshComponent* Sk = (PChSk && PChSk->BaseSkel)
		? ToRawPtr(PChSk->BaseSkel) : Ch->FindComponentByClass<USkeletalMeshComponent>();

	auto SpawnGlow = [&](int32 ItemId, int32 Enh, bool bShield, FName Tag)
	{
		const FString Jeb = BuildGlowJebName(ItemId, Enh, bShield);
		if (Jeb.IsEmpty())
		{
			if (ItemId > 0 && Enh >= 5)
			{
				UE_LOG(LogAsdaWorld, Log, TEXT("[заточка] userIdx=%d %s itemId=%d grade=%d → эффекта нет (нет буквы/редкости в таблицах)"),
					UserIdxForLog, bShield ? TEXT("щит") : TEXT("оружие"), ItemId, Enh);
			}
			return;
		}
		// ★ Ревизия Б3 [ОРИГИНАЛ]: открыт магазин — эффекта нет (эталон `clearEffectMgr`); сверка построит его после закрытия.
		if (bShield ? bShopShieldOff : bShopWeaponOff)
		{
			(bShield ? Want.bShieldGated : Want.bWeaponGated) = true;
			UE_LOG(LogAsdaWorld, Log, TEXT("[заточка] userIdx=%d %s: открыт магазин — эффект не строится до закрытия (эталон entity_char.c:%s)"),
				UserIdxForLog, bShield ? TEXT("аура щита") : TEXT("свечение оружия"), bShield ? TEXT("28796") : TEXT("28377"));
			return;
		}
		// Кость крепления. Оружие: лук (itemSubType 8/10) — в ЛЕВОЙ руке ("_8_a1"), остальное — "8_a1".
		//   Щит — off-hand "_7_a1". Имена костей есть в нашем скелете cre_cremmy (дамп 51 кости).
		// ★ Класс оружия и флаг «убрано за спину» берём с самого персонажа (он их держит по эталону);
		//   для не-игровых носителей — из записи предмета, флаг = «в руке».
		const int32 WClass = PChSk ? PChSk->EquippedWeaponSubType
			: UAsdaCharAssembly::ResolveWeaponClass(bShield ? 0 : ItemId, IT);
		const bool bStowed = PChSk ? PChSk->bWeaponMotionHidden : false;
		const FName Bone = UAsdaCharAssembly::WeaponGlowBone(WClass, bStowed, bShield);
		if (Sk && Sk->GetBoneIndex(Bone) == INDEX_NONE)
		{
			UE_LOG(LogAsdaWorld, Warning, TEXT("[заточка] кости '%s' нет в скелете — свечение %s пропущено"),
				*Bone.ToString(), bShield ? TEXT("щита") : TEXT("оружия"));
			return;
		}
		AActor* Fx = SpawnCharFx(Jeb, Ch, FVector::ZeroVector, Tag);
		if (!Fx) { return; }
		// ★ Д1: attach-10 свечения заточки через ЕДИНУЮ точку ConfigureBoneAttach (режим WeaponGlow).
		//   • strictQuad: эталон effectQuad_updateAndDrawBillboard@0x120AEB0 — roll крутит только
		//     ГЕОМЕТРИЮ, трансляция = basePos + posChan·R·N48·N520 (без π-«швартовки» башни alpen);
		//     координаты .jeb свечения = сырым координатам МЕША ОРУЖИЯ (обе в матрице кости 8_a1).
		//   • Skel = ЯВНО BaseSkel (тот же, к которому AttachWeapon крепит МЕШ оружия); у персонажа
		//     скелеталок много (BaseSkel + Equip_* LeaderPose + MountMesh + ClanFlagMesh), порядок обхода
		//     OwnedComponents НЕ определён — FindComponentByClass мог вернуть чужой (в headless не видно).
		//   • attachMatrix «оружие за спиной» (копьё/посох, арбалет, баллиста, луки); Identity в руке.
		if (AAsdaEffectActor* E = Cast<AAsdaEffectActor>(Fx))
		{
			// ★ 13.09.2026 (конфигуратор свечения): эффект знает свой предмет — по паре (.jeb, код) ищется
			//   переопределение «этого меча», а не всех мечей той же буквы и редкости. [ПОРТ]
			E->SetSourceItemId(ItemId);
			USkeletalMeshComponent* GlowSkel = nullptr;
			if (AAsdaPlayerCharacter* PCh = Cast<AAsdaPlayerCharacter>(Ch)) { GlowSkel = PCh->BaseSkel; }
			E->ConfigureBoneAttach(Ch, GlowSkel, Bone, EAsdaFxAttachMode::WeaponGlow,
				FVector::ZeroVector, UAsdaCharAssembly::WeaponGlowAttachRaw(WClass, bStowed, bShield));
			// ★ 31.10.2026 (ревизия Б2): «построено» — только у настоящего эффект-актора с привязкой; иначе сверка
			//   пересоздавала бы чужой класс каждые 8 кадров.
			(bShield ? Want.bShieldBuilt : Want.bWeaponBuilt) = true;
			// ★ 08.10.2026 (ЦИКЛ 1:1, КРИТ-1 ревизии fx_loop): щит — `CCharacter_UpdateEquipAuraEffect@0xA4DD60`
			//   пишет `эффект+0x2A8 = 3` (@0xA4E512, вечный контейнер). У всех 192 `shd_*.jeb` контейнер на диске и так 3 —
			//   картинка не меняется, запись нужна для 1:1 (у оружия `CCharacter_UpdateHeldItemEffect` такой записи нет).
			if (bShield) { E->SetLoopContainerEndOverride(3); }
			// ★ 19.09.2026 (glow_fix5, С2): свечение заточки оружия и щита в группу свечения НЕ входит —
			//   `bGlowWithCarrier` не ставится. Эталон: оружие — `CCharacter_UpdateHeldItemEffect@0xA4C070` грузит .jeb
			//   в слот персонажа +0x33B18 (`CEffect_LoadJEB_Wrapper`) и кладёт узел +0x33B1C в очередь 0x3537FA8
			//   (@0xA4C656); щит — `CCharacter_UpdateEquipAuraEffect@0xA4DD60`: `Effect_SpawnByName` менеджера 0x349E860
			//   (@0xA4E4EA) + узел +12 в ту же очередь (@0xA4E568). Очередь рисует `renderShadowMeshQueue@0x97E870` на
			//   шаге «5.1.5» — после сведения свечения. Прежняя ссылка на `itemSetupSowelGlow@0x9ED480` была ошибкой:
			//   это полосы «совела» (вставленного камня, pbbBuildStripBatch), в порту их нет. Разбор — у объявления
			//   `AAsdaEffectActor::bGlowWithCarrier`.
		}
		UE_LOG(LogAsdaWorld, Log, TEXT("[заточка] userIdx=%d grade=%d → эффект=%s (%s, кость %s, itemId=%d)"),
			UserIdxForLog, Enh, *Jeb, bShield ? TEXT("щит") : TEXT("оружие"), *Bone.ToString(), ItemId);
	};

	if (bDoWeapon) { SpawnGlow(WeaponItemId, WeaponGrade, /*bShield=*/false, kWeaponGlowTag); }
	if (bDoShield) { SpawnGlow(ShieldItemId, ShieldGrade, /*bShield=*/true,  kShieldGlowTag); }
}

void UAsdaWorldSubsystem::RefreshSelfWeaponGlow()
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(LocalPlayerActor.Get());
	if (!Me) { return; }
	// ★ ИСТОЧНИК ДАННЫХ О СВОЁМ ЭКИПЕ (фикс 2026-07-31 «на себе эффектов нет вообще»):
	//   основной — 0x403 (эталонный g_aEnterWorldCharData → CCharacter+1588, слоты 9/8 с байтом заточки);
	//   фолбэк — та же запись char-list, из которой собран ВИД персонажа (Equip[itemType]), чтобы эффект
	//   гарантированно строился для ТОГО ЖЕ предмета, что и надетый меш (раньше свечение брало itemId
	//   из батча 4048, где поле +5 — ячейка инвентаря: в «слот 9» попадал свиток itemType 11 ⇒ имя .jeb
	//   не строилось и на себе эффекта не было НИКОГДА, тогда как на чужих он есть — там 4005/4065).
	int32 WId = Net->SelfWeaponItemId, WGr = Net->SelfWeaponGrade;
	int32 SId = Net->SelfShieldItemId, SGr = Net->SelfShieldGrade;
	// ★ 30.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Possess.BodyIdentity`): запись — общей точкой; при одержимости её нет (своя запись с тем же
	//   номером слота — другой персонаж, его оружие перебивало оружие тела «рассинхроном источников»).
	if (const FAsdaCharRecord* SelRec = AsdaSelf::FindSelectedCharRecord(Net))
	{
		const FAsdaCharRecord& R = *SelRec;
		const int32 CharWeapon = R.Equip.IsValidIndex(9) ? R.Equip[9].ItemId : 0;
		const int32 CharShield = R.Equip.IsValidIndex(8) ? R.Equip[8].ItemId : 0;
		if (WId <= 0) { WId = CharWeapon; }
		if (SId <= 0) { SId = CharShield; }
		if (CharWeapon > 0 && WId > 0 && CharWeapon != WId)
		{
			// Надет один предмет, а свечение строится для другого — рассинхрон источников.
			UE_LOG(LogAsdaWorld, Warning, TEXT("[заточка] СВОЁ оружие расходится: char-list=%d, 0x403=%d — берём надетое"),
				CharWeapon, WId);
			WId = CharWeapon; WGr = Net->SelfWeaponGrade;
		}
	}
	ApplyWeaponGlowToActor(Me, WId, WGr, SId, SGr, (int32)Net->SelfUserNum);
	// ★ 05.11.2026 [ОРИГИНАЛ] (`asda.Char.SowelGlow`): обводка самоцвета на своём оружии/щите (`itemSetupSowelGlow@0x9ED480` —
	//   эталон зовёт её там же, где пересобирает части своего персонажа). Разбор — AsdaSowelGlow.cpp.
	AsdaSowelGlowRefresh(Me);
}

// ★★★ 31.10.2026 [ОРИГИНАЛ] СВЕРКА СВЕЧЕНИЯ — порт покадрового `CCharacter_UpdateHeldItemEffect`/`UpdateEquipAuraEffect`
//   (разбор у `CVarAsdaWeaponGlowPersist`). Зовётся из тика персонажа (`AAsdaPlayerCharacter::Tick`), раз в 8 кадров
//   со сдвигом по актору. Держит ровно последний заказ `ApplyWeaponGlowToActor`:
//   • построенный эффект исчез (снесён любым путём) или его носитель-скелет сменился → заказ строится заново
//     (эталон: `gap_A42[700] == -1` → `CEffect_LoadJEB_Wrapper` в том же кадре);
//   • эффект скрыт, а носитель виден и его не прячет ни одна система (GM-скрытие/невидимость — метки носителя,
//     рыбалка/копка — `IsFishWeaponHidden` и метка `AsdaFishHidden`, своя метка эффекта `AsdaGmHiddenByHost`) → показать
//     (эталон рисует при открытом гейте каждый кадр; у нас скрытие, поставленное при спавне на скрытом носителе
//     (`SpawnCharFx`), иначе не снимал никто).
//   • ★ ревизия Б3: открыт магазин (`asda.WeaponGlow.ShopGate`) → часть гасится, после закрытия строится заново.
//   Части (оружие/щит) сверяются и пересобираются ПОРОЗНЬ (ревизия Б1). Гейт магазина живёт в сверке, поэтому при
//   `asda.WeaponGlow.Persist 0` он действует только в момент построения.
void AsdaWeaponGlowReconcile(AAsdaPlayerCharacter* Ch)
{
	if (!Ch || !AsdaWeaponGlowPersistOn()) { return; }
	if (((GFrameCounter + (uint64)Ch->GetUniqueID()) & 7) != 0) { return; }
	const FAsdaGlowWant* Found = GAsdaGlowWants.Find(TWeakObjectPtr<AActor>(Ch));
	if (!Found || (!Found->bWeaponBuilt && !Found->bShieldBuilt && !Found->bWeaponGated && !Found->bShieldGated)) { return; }
	const FAsdaGlowWant Want = *Found;   // копия: пересборка ниже перепишет запись карты
	AAsdaEffectActor* WFx = nullptr;     // живое свечение оружия на носителе
	AAsdaEffectActor* SFx = nullptr;     // живая аура щита на носителе
	TArray<AActor*> Attached;
	Ch->GetAttachedActors(Attached);
	for (AActor* A : Attached)
	{
		AAsdaEffectActor* E = Cast<AAsdaEffectActor>(A);
		if (!E || E->IsPendingKillPending()) { continue; }
		if (!WFx && E->ActorHasTag(kWeaponGlowTag)) { WFx = E; }
		else if (!SFx && E->ActorHasTag(kShieldGlowTag)) { SFx = E; }
	}
	UWorld* W = Ch->GetWorld();
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaWorldSubsystem* Ws = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!Ws) { return; }
	bool bShopWeaponOff = false, bShopShieldOff = false;   // ★ ревизия Б3: гейт магазинов эталона
	AsdaGlowShopGates(GI, bShopWeaponOff, bShopShieldOff);
	// ★ Ревизия Б1: каждая часть сверяется и пересобирается ОТДЕЛЬНО — пропажа ауры щита не перезапускает свечение оружия.
	bool bRebuilt = false;   // что-то пересобрано в этот проход (показ скрытого — со следующего)
	for (const bool bShield : { false, true })
	{
		AAsdaEffectActor* Fx = bShield ? SFx : WFx;
		const bool bBuilt = bShield ? Want.bShieldBuilt : Want.bWeaponBuilt;
		const bool bGated = bShield ? Want.bShieldGated : Want.bWeaponGated;
		const bool bShopOff = bShield ? bShopShieldOff : bShopWeaponOff;
		const TCHAR* What = bShield ? TEXT("аура щита") : TEXT("свечение оружия");
		const TCHAR* Why = nullptr;   // причина пересборки части (журнал)
		if (bShopOff)
		{
			// Эталон: открыт магазин → `clearEffectMgr` (гаснет, после закрытия грузится заново).
			if (Fx || bBuilt) { Why = TEXT("открыт магазин — гасим до закрытия"); }
		}
		else if (bGated)                                   { Why = TEXT("магазин закрыт — строим заново"); }
		else if (bBuilt && !Fx)                            { Why = TEXT("исчезло — строим заново"); }
		else if (Fx && Ch->BaseSkel && Fx->FollowSkelComponent != Ch->BaseSkel) { Why = TEXT("сменился скелет носителя — строим заново"); }
		if (!Why) { continue; }
		UE_LOG(LogAsdaWorld, Log, TEXT("[заточка] userIdx=%d: %s — %s (оружие %d +%d, щит %d +%d)"),
			Want.UserIdx, What, Why, Want.WeaponItemId, Want.WeaponGrade, Want.ShieldItemId, Want.ShieldGrade);
		const int32 PrevOnly = GAsdaGlowOnlyPart;
		GAsdaGlowOnlyPart = bShield ? 2 : 1;   // построение под гейтом магазина само снимет эффект и отметит «снято магазином»
		Ws->ApplyWeaponGlowToActor(Ch, Want.WeaponItemId, Want.WeaponGrade, Want.ShieldItemId, Want.ShieldGrade, Want.UserIdx);
		GAsdaGlowOnlyPart = PrevOnly;
		bRebuilt = true;
	}
	if (bRebuilt) { return; }
	if (Ch->IsHidden() || AsdaHostHiddenByTag(Ch) || Ch->IsFishWeaponHidden()) { return; }
	static const FName kFishHidden(TEXT("AsdaFishHidden"));        // метка рыбалки (`ApplyFishWeaponHide`)
	static const FName kHiddenByHost(TEXT("AsdaGmHiddenByHost"));  // своя метка эффекта при скрытом носителе (`AAsdaEffectActor::Tick`)
	for (AAsdaEffectActor* E : { WFx, SFx })
	{
		if (!E || !E->IsHidden() || E->Tags.Contains(kFishHidden) || E->Tags.Contains(kHiddenByHost)) { continue; }
		E->SetActorHiddenInGame(false);
		UE_LOG(LogAsdaWorld, Log, TEXT("[заточка] userIdx=%d: %s оставалось скрытым при видимом носителе — показано"),
			Want.UserIdx, E == WFx ? TEXT("свечение оружия") : TEXT("аура щита"));
	}
}

// ★ НАБОР МОУШЕНОВ СВОЕГО ПЕРСОНАЖА при смене оружия в мире (4021/4048).
//   При спавне набор ставит InitAppearance (Parts.Weapon из char-list); здесь ловим смену на лету.
void UAsdaWorldSubsystem::RefreshSelfWeaponMotion()
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(LocalPlayerActor.Get());
	if (!Net || !Me) { return; }
	int32 WId = Net->SelfWeaponItemId;
	if (WId <= 0)
	{
		// Фолбэк — запись char-list, из которой собран ВИД персонажа (тот же источник, что у меша).
		// ★ 30.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Possess.BodyIdentity`): общей точкой; при одержимости записи нет — вид тела собран из
		//   снимка `SelfParts` (1027/1024 на спавне), его оружие и берём.
		if (const FAsdaCharRecord* SelRec = AsdaSelf::FindSelectedCharRecord(Net))
		{
			WId = SelRec->Equip.IsValidIndex(9) ? SelRec->Equip[9].ItemId : 0;
		}
		else if (Net->SelfSoulState == 2)
		{
			WId = SelfParts.Weapon;
		}
	}
	Me->SetWeaponFromItem(WId, GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>());
}
