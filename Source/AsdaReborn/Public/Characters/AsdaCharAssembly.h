// Copyright (c) 2026 @sitxovski (Discord)
// Единая СИСТЕМА сборки визуала персонажа из носимых частей.
// Порт CCharacter_SetPartsModelByType@0x9E16E0 / playerCharObjectLoadColored@0xF0A2B0 (эталон).
// Один путь сборки для ВСЕХ мест, где рисуется персонаж: char-select (превью в слоте),
// char-create (превью создаваемого), и персонажи/NPC в мире (P2 «вход-в-мир»).
// Раньше эта логика жила тремя почти-дублями в AsdaLoginGameMode (ApplyEquipToSlot/ApplyCreateParts);
// консолидирована сюда как переиспользуемая система (правило classify-before-building).
#pragma once

#include "CoreMinimal.h"
#include "Characters/AsdaWornParts.h"   // ★ R3: FAsdaWornParts (вынесено отсюда)
#include "AsdaCharAssembly.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UAsdaItemTableSubsystem;

// ★ R3 (11.11.2026): `FAsdaWornParts` вынесено дословно в `Characters/AsdaWornParts.h` (включён в шапке этого файла).

// Статическая система-помощник (без состояния): собирает части поверх уже созданного базового скелета.
// Не зависит от GameMode — вызывается из любого владельца (char-select/create GameMode, актёр персонажа мира).
UCLASS()
class ASDAREBORN_API UAsdaCharAssembly : public UObject
{
	GENERATED_BODY()

public:
	// Собрать все носимые части на базовый скелет BaseSkel (idle-поза базы деформирует части через LeaderPose).
	// Каждая часть с ненулевым itemId → экип-меш заменяет совпадающую базовую секцию. Возвращает число надетых частей.
	static int32 BuildWornParts(AActor* Owner, USceneComponent* Root, USkeletalMeshComponent* BaseSkel,
		bool bMale, const FAsdaWornParts& Parts, UAsdaItemTableSubsystem* Items);

	// Единичная часть-примитив: itemId → имя меша (item-таблица) → грузит EquipParts/<name>/<name> (менеджер модели),
	// крепит к Root, LeaderPose=база, скрывает базовую секцию BaseSectionToHide (или nullptr — не трогать базу).
	// Возвращает созданный компонент или nullptr (не надето / меш не найден).
	static USkeletalMeshComponent* AttachPart(AActor* Owner, USceneComponent* Root, USkeletalMeshComponent* BaseSkel,
		bool bMale, int32 ItemId, const TCHAR* BaseSectionToHide, UAsdaItemTableSubsystem* Items);

	// Грузит скелет-меш экип-части по её мешевому имени через ModelAnim-менеджер (никогда не LoadObject напрямую).
	static USkeletalMesh* LoadEquipPartMesh(const FString& MeshName);

	// ★ ОБЩИЕ ТОЧКИ ПУТЕЙ АССЕТОВ СБОРКИ (закон port-shared-points): ими пользуются и сама сборка, и
	//   предзагрузка экрана выбора персонажа (`AAsdaLoginGameMode::RequestCharSelectPreload`), чтобы раскладка
	//   путей не разъехалась. Формат каждого — тот, что сборка использовала всегда.
	static FString EquipPartMeshPath(const FString& MeshName);   // ПАКЕТНЫЙ путь скелетной части экипировки (ключ менеджера моделей)
	static FString CharStaticMeshPath(const FString& MeshName);  // объектный путь жёсткого меша причёски/оружия
	static FString CharTexturePath(const FString& TexName);      // объектный путь текстуры модели персонажа
	static FString FaceTexturePath(bool bMale, int32 FaceIndex); // объектный путь текстуры лица (NN = 2 знака)
	static FString DefaultHandsMeshName(bool bMale);             // имя меша стоковых кистей (часть без itemId)

	// ★ ЗЕРКАЛО `BuildWornParts` ДЛЯ ПРЕДЗАГРУЗКИ: те же ветви (лицо, режим волос под шлемом, причёска и её цвет,
	//   части и их эталонные текстуры, стоковые кисти, оружие), но НИЧЕГО не грузит и не создаёт — только
	//   дописывает объектные пути («пакет.имя») в Out без повторов. Правишь ветви сборки — правь и здесь.
	static void CollectWornPartPaths(bool bMale, const FAsdaWornParts& Parts, UAsdaItemTableSubsystem* Items, TArray<FString>& Out);

	// ★ ЭТАЛОННАЯ ТЕКСТУРА предмета (item_img_table.TexMale) на все слоты компонента через свап "Tex"
	//   (dynamic MID). Текстура В МЕШЕ (запечённая из .jbm) НЕНАДЁЖНА: там бывает чужая (swrd003→swrd002)
	//   или мусор; к тому же один меш шарится разными предметами с разным TexMale. Движок ставит TexMale —
	//   делаем так же. TexName пуст → не трогаем (оставляем запечённую). Путь как у ApplyHair/ApplyFace.
	static void ApplyItemTexture(class UMeshComponent* Comp, const FString& TexName);

	// Лицо (эталон tagCHARACTER_SetPartsModel_Face@0x9E48F0 → SetParts_FaceTexture@0x9E1F90): свап ТЕКСТУРЫ
	//   секции материала face001 базового скелета на ch1_{m|f}_00_face001_{NN}_00_0 (NN = 2-знака, 0..10).
	//   Меш секции не меняется — только текстура (Tex-параметр материала). FaceIndex<0 → не трогать.
	static void ApplyFace(USkeletalMeshComponent* BaseSkel, bool bMale, int32 FaceIndex);

	// Гасит ВШИТУЮ в базовый превью-меш причёску (артефакт экспорта cremmy: секция hair001 запечена в скелет-меш →
	//   двойные волосы). Своя причёска приходит из ApplyHair. База должна быть лысой.
	// ★ 2026-07-31: раньше гасили свапом "Tex" на прозрачную T_Transparent и полагались на alpha-test мастера.
	//   Материалы персонажа теперь НЕПРОЗРАЧНЫЕ (эталон EmptyProject.fx/PShade: альфа = маска блика, а НЕ
	//   прозрачность), поэтому секция скрывается штатным ShowMaterialSection(false) — как у экип-частей.
	static void HideBaseHair(USkeletalMeshComponent* BaseSkel);

	// Волосы (эталон CCharacter_LoadHairModel@0x9E2700 / @0x9E2DC0): причёска = отдельный жёсткий меш поверх
	//   головы + цвет-текстура.
	//   МЕШ  — из hairtable по ключу (gender, hairStyle, hairColor), поле Mesh (запись+45) + ".jbm".
	//          Нет записи → эталонный хардкод-switch: styles 0..4 → ch1_{m|f}_00_hair{style+1:03}_00_00_0,
	//          иначе м → hair001, ж → ch1_f_00_hair000_00_00_1 (лысая).
	//          ВАЖНО: у стилей 25..37 имя меша в таблице несёт ХВОСТ "_0" (ch1_f_00_hair026_00_00_0_0) —
	//          формулой его не получить, поэтому таблица обязательна.
	//   ТЕКСТУРА — ВСЕГДА по формуле эталона sprintf("ch1_%c_00_hair00%d_00_0%d_0.TEX", style+1, color):
	//          "hair00" + ДЕСЯТИЧНОЕ число (style+1) БЕЗ ведущих нулей → style 0 → hair001, style 10 → hair0011,
	//          style 37 → hair0038. Материал самого .jbm игнорируется (в файлах он для 011..025 битый).
	//   HairStyle<0 → без волос. Возвращает созданный компонент (умирает вместе с актёром при пересборке).
	// `bUnderHelmet` — режим 1 эталона: у ЖЕНЩИН причёска подменяется «подшлемным» мешем
	//   `ch1_f_00_hair000_00_00_1` (`CCharacter_LoadHairModel(char, 1)`, `entity_char.c:11485-11488`);
	//   у мужчин эталон в этом же режиме грузит обычную причёску.
	static class UStaticMeshComponent* ApplyHair(AActor* Owner, USceneComponent* Root, USkeletalMeshComponent* BaseSkel, bool bMale, int32 HairStyle, int32 ColorIdx, UAsdaItemTableSubsystem* Items = nullptr, bool bUnderHelmet = false);

	// Имя меша причёски по (пол, стиль, цвет) — 1:1 резолв эталона (hairtable → fallback switch). Пусто = нет.
	static FString ResolveHairMeshName(bool bMale, int32 HairStyle, int32 ColorIdx, UAsdaItemTableSubsystem* Items);

	// ═══ ★★★ ВОЛОСЫ ПОД ШЛЕМОМ — ПРАВИЛО ОТРИСОВКИ ПЕРСОНАЖА (жалоба владельца 2026-08-28:
	//   «после покупки пышной причёски она просачивается сквозь шлем») ═══════════════════════════
	//
	// ЭТАЛОН. Решает ДАННАЯ ЗАПИСЬ ШЛЕМА, а не причёска: `CCharacter_LoadHelmetPart@0x9E3390`
	//   (`entity_char.c:12013-12045`, `:12243-12245`, `:12460-12471`) читает у ImgRecord шлема
	//   поле **+288 (муж) / +292 (жен)**, кладёт его в `effectSlots[112].field_318` и ветвится:
	//     • **0** → `LoadHairModel(char, 0)` — причёска обычная;
	//     • **1** → мужчине `LoadHairModel(char, 0)`, женщине `LoadHairModel(char, 1)`, а `a2 == 1`
	//               у женщин выбирает ОТДЕЛЬНЫЙ «подшлемный» меш `ch1_f_00_hair000_00_00_1.jbm`
	//               (`entity_char.c:11485-11488`);
	//     • **2** → `LoadHairModel` НЕ ЗОВЁТСЯ ВОВСЕ и снимается признак `fieldE4` ⇒ волос нет;
	//     • иное → шлем не грузится (`return 0`).
	//   При СНЯТИИ шлема (`entity_char.c:12691-12695`) — `LoadHairModel(char, 0)`, `field_318 = 0`.
	//
	// ДАННЫЕ ПОРТА: это колонки `SubTypeMale` (+0x120) и `SubTypeFemale` (+0x124) таблицы
	//   `item_img_table` — они УЖЕ импортированы. Живой срез: 0 → 34023/34988 записей,
	//   1 → 784/2471, 2 → 8364/5712, −1 → 15707 (это не шлемы, поле у них не значит ничего).
	//   ⇒ Никакого «если код N»: режим целиком берётся из таблицы, и редактор предметов его меняет.
	//
	// Возвращает 0/1/2 (режим шлема). `HelmetItemId <= 0` (шлема нет) ⇒ 0.
	static int32 ResolveHelmetHairMode(int32 HelmetItemId, bool bMale, UAsdaItemTableSubsystem* Items);

	// ★ 25.10.2026 [ОРИГИНАЛ] Код шлема, который РИСУЕТСЯ: `Parts.Helmet`, а при снятом флаге показа
	//   (`bHelmetShown == false`) — 0, то есть ветка «шлема нет» эталона (`LoadHelmetPart`: флаг 0 → шлем не грузится,
	//   причёска обычная). Единая точка для сборки и предзагрузки. Ключ `asda.Char.HelmetShowFlag 0` — прежнее
	//   поведение порта (флаг не учитывается, шлем рисуется всегда).
	static int32 EffectiveHelmetId(const FAsdaWornParts& Parts);

	// Имя цвет-текстуры причёски — 1:1 формула эталона (без .TEX). Пусто при HairStyle<0.
	static FString ResolveHairTextureName(bool bMale, int32 HairStyle, int32 ColorIdx);

	// Оружие в руке (эталон CCharacter_SetupModelFromItem@0x9E97A0): itemId → имя меша (item-таблица,
	//   ch1_b_XX_*) → StaticMesh /Game/AsdaAssets/Models/Char/<name>/StaticMeshes/<name>, крепится к кости
	//   M_DUMMY базового скелета с relative = обратный bind-CS кости (как ApplyHair к кости "4"): меш в
	//   модельном простр. ложится 1:1 и следует за рукой в idle/атаке. itemId 0 → без оружия.
	//   Щит — отдельная часть (экип-слот 8, кость "_7_a1"), см. `AttachShield`.
	static class UStaticMeshComponent* AttachWeapon(AActor* Owner, USceneComponent* Root, USkeletalMeshComponent* BaseSkel,
		bool bMale, int32 ItemId, UAsdaItemTableSubsystem* Items);

	// Тег компонента меша оружия — по нему персонаж находит его для пересадки «в руку ↔ за спину».
	static const FName WeaponMeshTag;

	// ═══ ★★★ 25.10.2026 [ОРИГИНАЛ] МЕШ ЩИТА (жалоба игроков: «щит надет и даёт защиту, но на модели его нет») ═══
	//   Эталон `CCharacter_SetParts_Armor@0x9EB3C0` (entity_char.c:15670-16009), зовётся `tagCHARACTER_SetPartsModelByType`
	//   (экип-слот 8) и тиком `charUpdateActionStateTick@0xA216B0` при смене флага «оружие убрано» (вместе с оружием):
	//     • меш  — `Model\<ImgRecord+4 (gender 1) | +68>.jbm` (в порту — StaticMesh Char/<имя>/StaticMeshes/<имя>);
	//     • текстура — ImgRecord+36 / +100 + ".TEX" на все подсетки (порт: `ApplyItemTexture`);
	//     • кость — `gap_A42[15160]` (оружие убрано: город, седло, трансформация, занят) ? "_2_a1" : "_7_a1";
	//       поправок локальной матрицы у щита НЕТ (в отличие от копья/посоха/арбалета/баллисты/луков);
	//     • bind меша: собственный .jbb щита — кость "_7_a1" с ЕДИНИЧНОЙ матрицей во всех ключах (все 40 .jbb щитов
	//       ORIG-сборки, замер 25.10) ⇒ relative = Identity (у оружия bind T(0,0,0.463) ⇒ +46.3);
	//     • не загрузилось (пустое имя / нет файла) — эталон возвращает 0, щита нет.
	//   Ключ `asda.Char.ShieldMesh` (1 — меш щита; 0 — прежний порт: щит только источником свечения заточки).
	//   Возвращает компонент или nullptr (щита нет / ключ 0 / нет меша / нет кости — превью выбора персонажа).
	static class UStaticMeshComponent* AttachShield(AActor* Owner, USceneComponent* Root, USkeletalMeshComponent* BaseSkel,
		bool bMale, int32 ItemId, UAsdaItemTableSubsystem* Items);

	// Тег компонента меша щита — по нему персонаж пересаживает щит «в руку ↔ за спину» и прячет его на рыбалке.
	static const FName ShieldMeshTag;

	// Кость щита (эталон SetParts_Armor): убран — "_2_a1", в руке — "_7_a1". Та же, что у свечения (`WeaponGlowBone(…, true)`).
	static FName ShieldAttachBone(bool bStowed);

	// Пересадить уже созданный меш щита «в руку ↔ за спину» (эталон пересоздаёт часть через SetParts_Armor;
	//   нам достаточно сменить кость, relative остаётся единичным). Кости нет — не трогаем.
	static void ApplyShieldStow(USkeletalMeshComponent* BaseSkel, class UStaticMeshComponent* ShieldComp, bool bStowed);

	// Включён ли меш щита (ключ `asda.Char.ShieldMesh`) — аксессор для других .cpp (имя ключа в рабочий код не вписывать).
	static bool ShieldMeshOn();

	// ★★★ ТЕГ ЛЮБОГО КОМПОНЕНТА, СОЗДАННОГО ЭТОЙ СИСТЕМОЙ (части брони, кисти, причёска, оружие).
	//   Нужен для `ClearWornParts`: пересборка внешности обязана СНЯТЬ прежние части, иначе они
	//   копятся слоями (см. `ClearWornParts`). Ставится в `AttachPart` / `ApplyHair` / `AttachWeapon`
	//   и на «стоковые кисти» внутри `BuildWornParts`.
	static const FName WornPartTag;

	// ★★★ СНЯТЬ ВСЁ, ЧТО НАДЕЛА ЭТА СИСТЕМА, и вернуть базовому телу все его секции.
	//
	//   ЗАЧЕМ. `BuildWornParts` каждый раз создаёт НОВЫЕ компоненты (`NewObject`) и гасит секции
	//   базового меша (`ShowMaterialSection(false)`). Пока внешность собиралась ровно один раз (спавн),
	//   это было незаметно. Как только внешность стала пересобираться на смене экипировки
	//   (0xFB5/0xFB7 у чужих, локальное применение 5001 у себя), тот же вызов начал:
	//     • ДОБАВЛЯТЬ ещё один меш поверх старого (старый шлем остаётся на голове рядом с новым);
	//     • НАВСЕГДА оставлять базовую секцию скрытой — снял нагрудник, а торс остался невидимым.
	//   Эталон такой проблемы не имеет: `tagCHARACTER_SetPartsModelByType@0x9E18D0` меняет ОДИН
	//   слот модели, а не досоздаёт объекты (`CCharacter_SetParts_*` перезаписывают указатель части).
	//
	//   ЧТО ДЕЛАЕТ. Уничтожает все компоненты владельца с тегом `WornPartTag` и вызывает
	//   `ShowAllMaterialSections(0)` на базовом скелете. Чужие компоненты (транспорт, флаг клана,
	//   плашка, эффекты свечения/крыльев) НЕ ТРОГАЕТ — отбор строго по тегу.
	static void ClearWornParts(AActor* Owner, USkeletalMeshComponent* BaseSkel);

	// ★ РАЗРЕШЁННЫЙ класс оружия (эталон CCharacter_SetupModelFromItem@0x9E97A0, entity_char.c:14700-14709):
	//   перед выбором кости клиент ПОДМЕНЯЕТ equippedWeaponType для удочек (ItemSubType 10 + ItemVariant 1)
	//   на 2. Без этого удочка уезжала в ЛЕВУЮ руку (ветка луков 8/10) вместо правой. 99 = нет оружия.
	static int32 ResolveWeaponClass(int32 ItemId, UAsdaItemTableSubsystem* Items);

	// ★ КОСТЬ И ЛОКАЛЬНЫЙ ТРАНСФОРМ МЕША ОРУЖИЯ (эталон entity_char.c:14710-14776, case 19).
	//   В РУКЕ:      "8_a1"; луки/арбалеты-«лук» (класс 8/10) — вторая рука "_8_a1"; локальный трансформ =
	//                bind меша (.jbb t=(0,0,0.463) D3D = (0,46.3,0) UE).
	//   ЗА СПИНОЙ:   ВСЕГДА "2_a1" (обе ветки эталона), плюс поправки локальной матрицы для части классов:
	//                 класс 2/5  (копьё/посох)  local = T(34.3,−1.4,−41.7)·RotY(−π/2)
	//                 класс 9    (арбалет)      local = T(20,−30,−25)·RotX(−π/2)
	//                 класс 11   (баллиста)     local = T(49.7,−1.4,−18.2)·RotY(−π/2)
	//                 класс 8/10 (луки)         строка трансляции local := (10,0,35), поворот = bind
	//                 классы 0,1,3,4            поправок НЕТ — bind как есть
	//   Перевод в наши оси/единицы — см. WeaponStowAttachRaw (ЕДИНЫЙ источник для меша и свечения).
	static void WeaponAttachInfo(int32 WeaponClass, bool bStowed, FName& OutBone, FTransform& OutXf);

	// ★★ ЕДИНЫЙ ИСТОЧНИК ПОПРАВКИ «ОРУЖИЕ ЗА СПИНОЙ» (и для МЕША, и для СВЕЧЕНИЯ).
	//   Эталон строит ОДНУ И ТУ ЖЕ матрицу двумя разными кусками кода:
	//     МЕШ      — CCharacter_SetupModelFromItem@0x9E97A0, entity_char.c:14727-14775
	//                (пишет в локальную матрицу ноды pModelPartArray+3576);
	//     СВЕЧЕНИЕ — CCharacter_UpdateHeldItemEffect@0xA4C070, entity_char.c:28518-28556
	//                (attachMatrix слева от живой матрицы кости).
	//   В обоих:  D3DXMatrixMultiply(out, T, R) ⇒ при row-векторах  M = T·R  (сперва перенос, потом
	//   поворот), а применяется как  world = M · boneLive("2_a1") · charWorld.
	//   Dispatch_CallPtr15B6008 = D3DXMatrixRotationY, mathBuildRotMatrix = D3DXMatrixRotationX
	//   (импорт-указатели 0x15B6004/8/C = RotX/RotY/RotZ; пара pitch/yaw/roll в effects_particles.c:91102).
	//   ─────────────────────────────────────────────────────────────────────────────────────────────
	//   ПЕРЕВОД В НАШЕ ПРОСТРАНСТВО. Числа эталона — в КЛИЕНТСКИХ единицах (персонаж ≈150, меч ≈65,
	//   лук ≈108). Наши сырые единицы скелета и .jbm-мешей = клиентские ×100 (замер: ch1_b_00_swrd001
	//   длиной 6511, ch1_b_00_sbow001 — 10830, рост модели 15010), оси переставлены импортом glTF:
	//        Q(game) = 100·( x, −z, y )        (см. entity/mirror_and_weapon_transform_re.md: UE = (x,−z,y)·100)
	//   ⇒ поправка в кости UE:  M_UE = Q̂⁻¹·M_game·Q̂,  строка трансляции = 100·Q̂(t·R).
	//   ЭТО ЖЕ ПРОСТРАНСТВО, в котором живёт и relative-трансформ меша оружия, и FollowBoneAttach
	//   эффекта (там оно стоит РОВНО после FxToBone: A = FxToBone·Attach·BoneCS·Comp).
	//   bMesh: у ЛУКОВ эталон пишет мешу (10,0,35), а свечению (0,0,35) — единственное расхождение.
	static FMatrix WeaponStowAttachRaw(int32 WeaponClass, bool bMesh);

	// Пересадить уже созданный меш оружия «в руку ↔ за спину» (эталон переcоздаёт модель через
	//   SetupModelFromItem; нам достаточно сменить кость и локальный трансформ).
	static void ApplyWeaponStow(USkeletalMeshComponent* BaseSkel, class UStaticMeshComponent* WeaponComp,
		int32 WeaponClass, bool bStowed);

	// ★ Кость свечения заточки (эталон CCharacter_UpdateHeldItemEffect@0xA4C070, entity_char.c:28471-28485
	//   для оружия и :28878-28881 для щита): оружие в руке — "8_a1" (луки 8/10 — "_8_a1"), за спиной — "2_a1";
	//   щит в руке — "_7_a1", за спиной — "_2_a1".
	static FName WeaponGlowBone(int32 WeaponClass, bool bStowed, bool bShield);

	// ★ Эталонная attachMatrix свечения «за спиной» в осях КОСТИ UE (= WeaponStowAttachRaw(class,false)).
	//   Identity, если поправки нет (щит, классы 0/1/3/4, оружие в руке).
	static FMatrix WeaponGlowAttachRaw(int32 WeaponClass, bool bStowed, bool bShield);
};
