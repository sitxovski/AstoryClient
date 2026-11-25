// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillDatabase.h — СТАТИЧЕСКИЕ данные навыков (то, что в оригинале держит `CSkillMgr`).
//
// ★ ЗАЧЕМ ОТДЕЛЬНЫЙ ОБЪЕКТ, А НЕ ПОЛЯ ПОДСИСТЕМЫ ОКНА
//   В оригинале таблица навыков — глобальный синглтон `g_pSkillInfoTable` (`CSkillMgr`, размер 0x9444),
//   который читают и окно навыков, и панель быстрого доступа, и боевой код, и эффекты. Окно — лишь один
//   из потребителей. Поэтому таблица живёт отдельным объектом, а окно её только просит.
//
// ИСТОЧНИКИ (всё подтверждено байтами, разбор — `Reports/skill_window_port.md` §2):
//   • `skill_info.sif` → `DT_skill_info` (`FAsdaSkillInfoRow`), запись 273 б, БЕЗ заголовка,
//     индекс записи == skillCode. Загрузчик оригинала `CSkillMgr_LoadSkillData@0xA79F30` читает файл
//     ОДНИМ блоком, поэтому раскладка файла == раскладка в памяти, и колонка `DataHex` = вся запись 1:1.
//   • `Skilltooltip.BIN` → `DT_skilltooltip` (`FAsdaSkilltooltipRow`), 20 б заголовок + 1255 × 504 б.
//     Доступ у оригинала — ПО ИНДЕКСУ (`uiGetRecordByIndex@0xE52930` = base + 504·skillCode), не поиском.
//     В записи: +0 i32 id, +4 имя(32), **+36 имя файла иконки (ASCII)**, +100 описание, +300 описание EN.
//   • `SkillCoinTable.bin` → `DT_SkillCoin` (`FAsdaSkillCoinRow`) — три числа под иконкой слота.
//     ★ Аудит №3 (Дд4): берётся у `UAsdaItemTableSubsystem::GetSkillCoin` (ключевой индекс SkillId).
//     Своей загрузки этой таблицы у базы навыков НЕТ — иначе в памяти жили две её копии.
//   • `JobType.BIN` → `DT_jobtype` (`FAsdaJobtypeRow`) — 45 записей: имя класса + родитель-дерево.
//
// ★ Аудит №3 (A12/B7): база — ПОДСИСТЕМА `UGameInstanceSubsystem`, а не `NewObject` внутри окна
//   навыков. Раньше объект создавался окном, и три внешних потребителя (полоса применения, диспетчер
//   применения, панель быстрого доступа) ходили к данным ЧЕРЕЗ окно: не открывал окно — базы нет.
//   Оригинальный `g_pSkillInfoTable` живёт независимо от книги навыков, теперь так же и у нас.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AsdaSkillDatabase.generated.h"

class UGameInstance;

// Одна запись `skill_info.sif` (273 б), разобранная в поля. Комментарий у поля = смещение в записи.
USTRUCT(BlueprintType)
struct FAsdaSkillDef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 SkillCode = -1;      // +0x00 i16 (== индексу записи)
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") FString Name;              // имя (skilltooltip +4, фолбэк skill_info +2)
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") FString Description;       // skilltooltip +100 (с inline-тегами [ms]/[sr]/[sdt1])
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") FString IconName;          // skilltooltip +36, ASCII (напр. skl_f1_hamblow)

	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Kind = 0;            // +0x2A 1 = активный, 2 = пассивный
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 MaxLevel = 0;        // +0x2B КАП уровня (гейт «можно ли вкачивать»)
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 SubType = 0;         // +0x2C 3/4 = в квикслот НЕ перетаскивается
	// ★ 31.10.2026 (баг 7): время каста, мс — то же поле, что серверное `SkillInfo[209·id + 45]` (`sub_47B580` кладёт его в
	//   CUser+23999, `CheckSkillCastingTime@0x47B480` по нему шлёт конец каста → 5271). Проверено: 960 → 3000, 927 → 1500
	//   (живой журнал: 5270→5271 через 3.0 и 1.6 с). Читает предохранитель состояния 14 в `UAsdaSkillUseSubsystem`.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 CastTimeMs = 0;      // +0x2D i16
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 CooldownMs = 0;      // +0x2F i32
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 CastType = 0;        // +0x33 (нотис = значение + 9483)
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 TargetSlotType = 0;  // +0x34 (нотис = значение + 9488)
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 RangeMeters = 0;     // +0x35
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 ResourceType = 0;    // +0x36
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 AoeRadius = 0;       // +0x37 ⚠ в IDA поле зовётся maxSkillLevel — МИСНЕЙМ
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 MpCostBase = 0;      // +0x3A i16
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 MpCostPerLevel = 0;  // +0x3C i16
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 MsBase = 0;          // +0x3E i16
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 MsPerLevel = 0;      // +0x40 i16
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 SrBase = 0;          // +0x44 u8  [sr]-величина база
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 SrPerLevel = 0;      // +0x45 u8  [sr] за уровень
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 S1Base = 0;          // +0x4A i16 величина эффекта
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 S1PerLevel = 0;      // +0x4C i16
	// ★ Аудит №3: id СОСТОЯНИЯ, которое навык вешает ПЕРВЫМ слотом (эталон `rec->buff0SkillId`).
	//   Смещение снято в `Reports/skill_fx_anim.md:1429` и подтверждено якорями (787 → 169,
	//   784 → 185, 786 → 104). Нужно отражению урона 4015 (`cclassApplyReflectSkillDamage@0xF8A970`):
	//   поле лежало ТРЕТЬЕЙ читалкой `skill_info` в подсистеме мира — сведено сюда, к остальной записи.
	//   −1 — законное значение («состояния в слоте 0 нет»), «навыка нет» = `Get()` вернул nullptr.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Buff0StateId = -1;          // +0x48 i16
	// Три «buff-слота» одинаковой раскладки — источники плейсхолдеров [s2]/[s3]/[sdt*]/[sdtt*].
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Buff0Duration = 0;          // +0x4E i32, мс
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Buff0DurationPerLevel = 0;  // +0x52 i32
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Buff0Cooldown = 0;          // +0x56 i16, мс
	// ★ id СОСТОЯНИЯ буфф-слота 1 (+0x59). Шаг буфф-блока записи — 0x11: 0x48 → 0x59 → 0x6A.
	//   Снято с сырых данных 2026-09-16 (`skill_info.sif`, запись 273 б): навык 1209 отдаёт по этим
	//   трём смещениям 231 / 98 / 233, навыки 592 / 795 / 1240 — 27 / 174 / 247 в слоте 0 и −1 в
	//   слотах 1 и 2. −1 — законное значение «состояния в этом слоте нет».
	//   Зачем поле: множитель темпа атаки (`UAsdaWorldSubsystem::GetAttackTempoMul`) обязан знать,
	//   В КАКОМ слоте лежит искомое состояние, чтобы взять ПАРУ S1/S2/S3 этого же слота. Оригинал
	//   всегда читал S1 — это его дефект, исправление помечено у места расчёта.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Buff1StateId = -1;          // +0x59 i16
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 S2Base = 0;                 // +0x5B i16
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 S2PerLevel = 0;             // +0x5D i16
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Buff1Duration = 0;          // +0x5F i32
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Buff1DurationPerLevel = 0;  // +0x63 i32
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Buff1Cooldown = 0;          // +0x67 i16
	// ★ id СОСТОЯНИЯ буфф-слота 2 (+0x6A) — тот же блок с шагом 0x11, см. комментарий у Buff1StateId.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Buff2StateId = -1;          // +0x6A i16
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 S3Base = 0;                 // +0x6C i16
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 S3PerLevel = 0;             // +0x6E i16
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Buff2Duration = 0;          // +0x70 i32
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Buff2DurationPerLevel = 0;  // +0x74 i32
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Buff2Cooldown = 0;          // +0x78 i16
	// ★ Битовая маска ДОПУСТИМОГО ОРУЖИЯ (+0x83, u64). Тултип печатает «Оружие (нотис 2307) : …»,
	//   перечисляя нотисы 9455 + номер бита (0..18); если ни одного бита — нотис 2335 «Нет».
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int64 WeaponMask = 0;             // +0x83 u64
	// ★ АТРИБУТ (стихия). В IDA поле названо `areaShape` — МИСНЕЙМ: тултип печатает
	//   «Атрибут (9496) : {значение == 6 ? 9497 «Нет» : значение + 9498}» (Огонь/Вода/Земля/Воздух/Свет/Тьма).
	// ★★★ РЕАКЦИЯ ЦЕЛИ НА УДАР — ОТБРАСЫВАНИЕ (+0x8B, IDA `behaviorType`). Уточнено 2026-08-11, §25:
	//   это НЕ «наземный площадной навык». Switch по полю стоит в трёх местах эталона, и во всех
	//   результат применяется К ЦЕЛИ: `charApplySkillImpact` (skill_buff.c:4654) и
	//   `skillSetupTargetPos@0xA29C00` (skill_buff.c:4779) —
	//     0     сильный отброс: `cameraComputeSpringTarget` (точка «цель − 450·normalize(кастер−цель)»)
	//           → `GroundTarget_PickOnMesh` → `charSetMoveTarget(цель, …, 200.0f)` + состояние 14;
	//     1/2   слабый отброс: то же без пика, скорость 100.0f, состояние 14;
	//     3     без сдвига, взводится флаг цели +18800;
	//     4     реакции нет (и `behaviorType != 4` гейтит эффект удара — effects_particles.c:6214).
	//   Наземные тайлы `rec[+15]/[+17]` заявки 5256/6158 пишет только ветка 0, и это ТОЧКА
	//   ПРИЗЕМЛЕНИЯ ОТБРОСА, а не зона поражения. Навыков с 0 двенадцать: 502/506/515/544/547 и
	//   1231-1233 / 1237-1239 (закрытие §16 п.7: привязки «тайлы ⇔ resourceType 2/3» у эталона НЕТ).
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 BehaviorType = 0;           // +0x8B u8
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 ElementAttr = 0;            // +0x8C u8
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 SrCostDivisor = 1;          // +0xB6 u8, делитель [sr_c]
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 SpCostBase = 0;      // +0xBB **КОЛОНКА узла (0..4)**
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 SpCostPerTier = 0;   // +0xBC **РЯД узла**
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Grade = 0;           // +0xBD **ДЕРЕВО (1..12)**
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 GradeNum = 0;        // +0xBE
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 EffectSkillId0 = -1; // +0xC8 i16 (только показ в подсказке)
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 EffectLevel0 = 0;    // +0xCA
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 EffectSkillId1 = -1; // +0xCB i16
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 EffectLevel1 = 0;    // +0xCD

	// ★★★ ГРУППА ОБЩЕГО ОТКАТА (+0xA7, 6 × i16, незаполненные = −1).
	//   Эталон `skillStartGroupCooldown@0xE57860` (`skill_buff.c:33915-33940`): если применённый код
	//   ВСТРЕЧАЕТСЯ В СВОЁМ ЖЕ массиве, откат ставится ВСЕМ ненулевым (≠ −1) элементам массива
	//   одной и той же длительностью (из выученной записи ПРИМЕНЁННОГО навыка); иначе — только себе.
	//   На реальных данных (`skill_info.sif`, и клиентский, и файл порта — массивы совпадают
	//   ПОБАЙТНО) непустая цепочка есть ровно у 30 записей, и все 30 содержат себя:
	//     {503,511} {512,513,515} {706,707,708} {711,714} {767,768} {791,803} {927,928,929}
	//     {933,934,1002} {936,937} {971,972,976,989,1017,1019} {994,1015} {995,996} {930,999,1000}
	//   (коды 927/933/930/976/989 сами цепочки не имеют — им откат прилетает от «соседа»).
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") TArray<int32> ChainSkillIds;   // +0xA7 i16[6]

	// ★ Ступень профессии, с которой навык доступен: `Skilltooltip.BIN` +500 (i32, значения 1..5).
	//   Тултип печатает «Профессия (нотис 2297) : {8427 + значение}» («Не ниже первой/второй/…»).
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 JobStepReq = 0;

	// Три числа под иконкой слота (SkillCoinTable): эталон печатает rec[1..3] в контролы +48/+52/+56.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Coin0 = 0;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Coin1 = 0;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Coin2 = 0;

	// Позиция узла в СЕТКЕ дерева (эталон `Skill_BuildTreeUI@0xE532E0`): абсолютный слот до прокрутки.
	//   slotAbs = 5·SpCostPerTier + SpCostBase; видимый = slotAbs − 5·scrollRow, годен при 0..44.
	int32 AbsSlot() const { return 5 * SpCostPerTier + SpCostBase; }
	bool IsValid() const { return SkillCode >= 0; }

	// ★ В КАКОМ буфф-слоте записи лежит состояние `StateId`: 0/1/2, либо INDEX_NONE (нет такого).
	//   −1 в поле слота = «слот пуст», поэтому его никогда не считаем совпадением.
	int32 BuffSlotOfState(int32 StateId) const
	{
		if (StateId < 0) { return INDEX_NONE; }
		if (Buff0StateId == StateId) { return 0; }
		if (Buff1StateId == StateId) { return 1; }
		if (Buff2StateId == StateId) { return 2; }
		return INDEX_NONE;
	}

	// ★ Пара «база / за уровень» указанного буфф-слота (S1/S2/S3). Слот вне 0..2 → пара слота 0
	//   (ровно то, что читает оригинал: он всегда берёт S1, см. `GetAttackTempoMul`).
	void BuffSlotScale(int32 SlotIdx, int32& OutBase, int32& OutPerLevel) const
	{
		switch (SlotIdx)
		{
		case 1:  OutBase = S2Base; OutPerLevel = S2PerLevel; break;
		case 2:  OutBase = S3Base; OutPerLevel = S3PerLevel; break;
		default: OutBase = S1Base; OutPerLevel = S1PerLevel; break;
		}
	}
	// Узел попадает в сетку только если координаты реальные. У навыков 4-й профессии (1202..1254)
	//   оба байта = 0xFF — эталон такие в сетку не кладёт (5·255+255 = 1530 > 44 при любой прокрутке).
	bool HasGridSlot() const { return SpCostBase <= 4 && SpCostPerTier <= 60; }
};

// Запись `JobType.BIN` (64 б): имя класса + имя родительского дерева.
USTRUCT(BlueprintType)
struct FAsdaJobDef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Asda|Классы") int32 JobId = -1;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Классы") FString JobName;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Классы") FString ParentJob;
};

UCLASS()
class ASDAREBORN_API UAsdaSkillDatabase : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ★ Таблицы читаются сразу при подъёме подсистемы: данные статические, читателей много, и любой
	//   из них вправе спросить навык до того, как игрок откроет книгу. Зависимость от таблиц предметов
	//   объявлена явно — оттуда берётся `SkillCoin`.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// ★ 10.10.2026 (ревизия МЕЛК-4): отписка от `FAsdaLang::OnLanguageChanged` (статический делегат переживает сеансы PIE).
	virtual void Deinitialize() override;

	// Загрузить все таблицы (идемпотентно). Возвращает число разобранных навыков.
	//   Оставлен публичным: окно навыков вызывает его повторно после пересборки таблиц в редакторе.
	int32 Load(UGameInstance* GI);
	bool IsLoaded() const { return bLoaded; }

	const FAsdaSkillDef* Get(int32 SkillCode) const { return Defs.Find(SkillCode); }
	const TMap<int32, FAsdaSkillDef>& GetAll() const { return Defs; }
	const FAsdaJobDef* GetJob(int32 JobId) const { return Jobs.Find(JobId); }
	FString GetJobName(int32 JobId) const;

	// ── Правила «класс → деревья», всё подтверждено кодом эталона ──

	// `itemGradeBandFromTypeIndex@0xEC97A0`: 1..11 → 1 (Fighter), 12..22 → 2 (Archer), 23..33 → 3 (Mage),
	//   иначе 0 (Beginner/крафт — окно навыков не открывается, нотис 9421).
	static int32 BandFromJob(int32 JobId);

	// `skillCodeToElementGroup@0xE52C40`: grade 1/4/7 → 0, 2/5/8 → 1, 3/6/9 → 2, 10/11/12 → 3, иначе −1.
	static int32 TreeFromGrade(int32 Grade);

	// Обратное правило (`skillRebuildPageLists@0xE541E0`): band 1..3 × вкладка 0..3 → grade.
	//   band 1 → {1,2,3,10}, band 2 → {4,5,6,11}, band 3 → {7,8,9,12}.
	static int32 GradeForBandTree(int32 Band, int32 TreeIdx);

	// Все навыки указанного дерева (grade), отсортированы по абсолютному слоту сетки.
	//   Аналог `skillRebuildPageLists`: он перебирает id 501..1254 и отбирает по grade полосы.
	void GetTreeSkills(int32 Band, int32 TreeIdx, TArray<int32>& OutCodes) const;

	// Максимальный ряд сетки среди навыков дерева (для диапазона прокрутки колеса).
	int32 GetTreeMaxRow(int32 Band, int32 TreeIdx) const;

	// Нотис-подпись вкладки: band 1 → 9400/9401/9402, band 2 → 9403.., band 3 → 9406..
	//   (`uiPopulateUpgradeOptionList@0xE54900`). Вкладка 3 идёт по SubJob — см. Job4TabLocId.
	static int32 TreeTabLocId(int32 Band, int32 TreeIdx);
	// Подпись вкладки 4-й профессии по SubJob (там же): band1 SubJob 6/7 → 9422, 4/5 → 9423;
	//   band2 15/16 → 9424, 17/18 → 9425; band3 26/27 → 9426, 28/29 → 9427. 0 = подписи нет.
	static int32 Job4TabLocId(int32 Band, int32 SubJobId);

	// Строка-дамп для консоли.
	FString DumpSummary() const;
	FString DumpSkill(int32 SkillCode) const;

private:
	bool bLoaded = false;

	TMap<int32, FAsdaSkillDef> Defs;
	TMap<int32, FAsdaJobDef>   Jobs;
};
