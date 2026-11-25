// Copyright (c) 2026 @sitxovski (Discord)
// AAsdaSoulGuardianActor — ВИЗУАЛЬНЫЙ КОМПАНЬОН «ХРАНИТЕЛЬ» (Soul Guardian; в оригинале — «ангел»,
//   глобали `g_bFxShowSelfAngel` / `g_bFxShowOtherAngel`, локальная `angelObj` в `UpdateList_ModelAnim`).
//   Полный реверс: `Reports/soul_guardian_companion_re.md`. Каждое число ниже имеет там адрес.
//
// ═════════════════════════════════ ЧТО ЭТО В ОРИГИНАЛЕ ══════════════════════════════════════════
//   Отдельный `CCharActor` (0x768 б) в собственном менеджере `g_CharManager_0` — НЕ часть персонажа.
//   Появляется, когда сервер прислал 6156 с SoulGuardIndex 0..26 (уровень хранителя = число подряд
//   заряженных рядов монет, порог РОВНО 8; порог 5 — только доступность хоткея, компаньона не даёт).
//   6156 приходит и про СВОЕГО игрока, и про КАЖДОГО СОСЕДА при входе в AoI/пересчёте сектора —
//   у чужого игрока хранитель показывается ПОЛНОСТЬЮ так же (та же модель, анимации, эффекты, звуки).
//
// ── ПРИВЯЗКА (charPreviewUpdateRotation@0xFAA520, дизасм 0xFAA520..0xFAA626) ────────────────────
//   world = Translate(0, +30 + высотаТранспорта, 0) × ownerWorldMatrix   (row-vector D3D)
//   ⇒ смещение задано в ЛОКАЛЬНЫХ осях владельца, компаньон ВРАЩАЕТСЯ ВМЕСТЕ С ИГРОКОМ (не биллборд),
//     МАСШТАБА НЕТ (матрица = identity + трансляция), «высота над головой» ЗАПЕЧЕНА В КЛИПАХ.
//   Оси: game (x,y,z) → UE (−z, x, y) ⇒ game (0,+30,0) = UE (0,0,+30).
//   ★ Обе ветки эталона (`state==8` → `fsub -30.0`, иначе `fadd +30.0`) дают ОДНО И ТО ЖЕ +30.
//
// ── МОДЕЛИ (§4/§6.4) ───────────────────────────────────────────────────────────────────────────
//   Разных моделей 9 = 3 семейства × 3 уровня; SkillType 1/2/3 → `oh`, 4/5/6 → `cb`, 7/8/9 → `am`.
//   Меши скиннутые (meshType 0x80, двух-позиционный bind) ⇒ собраны ТОЛЬКО fixed-путём
//   (`Tools/asda_build_soulguard_models.py`, закон [[skin-pipeline-fixed-path]]). Скелет у каждой
//   модели СВОЙ (46/48/48/52/52/52/59/67/87 костей до прунинга) — шарить нельзя.
//
// ── 12 МОТИОНОВ (§3.2: слот switch `charSlotFieldPtrByType@0xEF6DD0` → поле записи) ─────────────
//   0 IDLE A | 1 IDLE B | 2 — НЕ мотион (этот слот = МЕШ) | 3 ATTACK A | 4 ATTACK B | 5 SKILL 1
//   6 BATTLE-IDLE | 7 MOVE | 8 CALL (призыв) | 9 OFF (уход) | 10 SKILL 2 | 11 SKILL 3
//   Звук мотиона — слот `100 + мотион` (см. ★ДЕФЕКТ в .cpp), эффект скилла — слот `30 + вариант`.
//
// ── ЗАКОНЫ ─────────────────────────────────────────────────────────────────────────────────────
//   ue-editor-friendly    — каждый параметр UPROPERTY, правится в редакторе.
//   fix-original-defects  — исправленные дефекты помечены ★ДЕФЕКТ и вынесены во флаг bLegacy*.
//   never-invent-only-original — числа только из отчёта/таблицы, ничего «по аналогии».
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Data/AsdaSoulGuardRows.h"
#include "AsdaSoulGuardianActor.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;
class USceneComponent;

// ═══════════════════════════════════════════════════════════════════════════════════════════════
// Прокси нативного AnimInstance компаньона: анимграфа нет, поза целиком берётся из СЛОТА монтажей.
//   Зачем: эталон делает КРОСС-ФЕЙД мотионов ровно 300 мс (`UpdateList_ModelAnim`, §7.4:
//   `d = трек.time/300.0; Anim_BlendKeyframes(cur, prev, 1−d)`), а штатный путь порта
//   (`SkeletalMeshComponent::PlayAnimation`, режим AnimationSingleNode) блендинга НЕ умеет — смена
//   клипа рвётся в один кадр. Слот-монтаж даёт настоящий межпозный бленд с заданным BlendIn.
//   Класс НЕ привязан к скелету (это не AnimBlueprint) ⇒ один на все 9 моделей.
// ═══════════════════════════════════════════════════════════════════════════════════════════════
struct FAsdaSoulGuardAnimProxy : public FAnimInstanceProxy
{
	FAsdaSoulGuardAnimProxy() = default;
	explicit FAsdaSoulGuardAnimProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual bool Evaluate(FPoseContext& Output) override;
};

UCLASS()
class ASDAREBORN_API UAsdaSoulGuardAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FAsdaSoulGuardAnimProxy(this); }
};

UCLASS()
class ASDAREBORN_API AAsdaSoulGuardianActor : public AActor
{
	GENERATED_BODY()

public:
	AAsdaSoulGuardianActor();

	// ★ Объектный путь `DT_SkillFx` («h»-эффект удара) — одно объявление (15.09.2026, dt_gc_hold): читает
	//   `FindSkillHitJeb`, удерживает на сессию `UAsdaWorldSubsystem::HoldSpawnTables(SoulGuardian)`.
	static constexpr const TCHAR* kSkillFxPath = TEXT("/Game/AsdaAssets/Data/DT_SkillFx.DT_SkillFx");

	// ── компоненты ──
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SoulGuardian")
	TObjectPtr<USceneComponent> Root;
	// Узел масштаба/зеркала (glTF-метры → см + компенсация хиральности конвертера) — как у пета.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SoulGuardian")
	TObjectPtr<USceneComponent> MeshRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SoulGuardian")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	// ── данные ──
	// SoulGuardIndex из 6156 (0..26). Смена индекса = ПЕРЕСОЗДАНИЕ актора (эталон: hard remove + create).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SoulGuardian")
	int32 SoulGuardIndex = -1;
	// userIdx владельца (ключ поиска, поле актёра [189] эталона).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SoulGuardian")
	int32 OwnerUserIdx = -1;
	// База имени модели в нижнем регистре («sb_md_oh_01»); в таблице встречается «sb_md_CB_01».
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SoulGuardian")
	FString ModelName;
	// Владелец, за которым жёстко следует компаньон (без AI и физики — как в эталоне).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SoulGuardian")
	TWeakObjectPtr<AActor> OwnerChar;

	// ── параметры (эталонные значения; правятся в редакторе) ──
	// ★ Смещение в ЛОКАЛЬНЫХ осях владельца. Эталон: game (0,+30,0) → UE (0,0,+30). Масштаба нет.
	//   Эталонность +30 подтверждена дважды: константы `.rdata@0x1398260`/`+0x14347DC` (прочитаны из
	//   EXE) и дизасм обеих веток `0xFAA520`; 1 игровая ед. = 1 см UE, origin владельца = ноги.
	// ★ ОТСТУПЛЕНИЕ ПО РЕШЕНИЮ ЮЗЕРА (2026-08-07): высота подобрана ГЛАЗАМИ в игре и равна **95**
	//   (эталон 30 → сначала 33 «+10 %» → итог 95: «идеальная высота хранителей — 95»).
	//   НЕ «возвращать к эталону» молча: эталонное значение = 30, доступно как `asda.SoulFx.Height 30`.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SoulGuardian|Привязка")
	FVector FollowOffset = FVector(0.f, 0.f, 95.f);
	// Компаньон держится горизонтально и смотрит туда же, куда владелец (матрица владельца целиком).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SoulGuardian|Привязка")
	bool bWorldRotationFollowsOwner = true;
	// Кросс-фейд смены мотиона. Эталон: РОВНО 300 мс (§7.4).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SoulGuardian|Анимация")
	float CrossFadeSec = 0.300f;
	// Появление: эталон `alpha += 0.40 * g_FrameTimeScale` (шаг «на кадр при 30 FPS») ⇒ 12 α/с ⇒ 1/12 с.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SoulGuardian|Фейд")
	float AppearSec = 1.f / 12.f;
	// ★ДЕФЕКТ ОРИГИНАЛА №1 (§1.4): исчезновение идёт `alpha -= 0.013 * g_FrameTimeScale` ⇒ 0.39 α/с,
	//   от 1.0 до порога 0.2 — 2.05 с, а клип «off» длится 3.00–3.33 с ⇒ АНИМАЦИЯ УХОДА НЕ ДОИГРЫВАЕТ.
	//   ИСПРАВЛЕНО: длительность гашения = длине клипа «off» (плюс запас). bLegacyFadeOut = вернуть 2.05 с.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SoulGuardian|Фейд")
	bool bLegacyFadeOut = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SoulGuardian|Фейд")
	float LegacyFadeOutSec = 2.05f;
	// ★★★ДЕФЕКТ ОРИГИНАЛА №2 — «хранитель испаряется, клипа ухода нет ВООБЩЕ» (живой тест 2026-08-10).
	//   ДОКАЗАТЕЛЬСТВО (эталон, не рассуждение). Мягкая ветка `charMgr_RemoveSummonById@0xFA7260`
	//   (`entity_char.c:61093-61094`) зовёт `Char_SetMotion(mgr, summon[189], 9, 0, 0)` БЕЗ обхода
	//   гейтов, а сам `Char_SetMotion@0xFA7560` (`entity_char.c:61113-61120`) начинается с
	//       if (obj[1764] != 8 || pChar->activeId >= 0x4B1) { if (obj[1764] != 9) { if (obj[1764] != 5) {…}}}
	//   ⇒ при `cur == 5` («СКИЛЛ 1») мотион 9 НЕ ставится ВООБЩЕ: клип «off» не играет ни кадра,
	//   актёра снимает только альфа-фейд, а поверх крутится зациклённый клип удара.
	//   КОГО БЬЁТ: вариант удара = `SkillCoinTable[skillId].Field16` = `((skillId−1201)%3)+1`, т.е.
	//   РЯД 1 любого семейства (скиллы 1228/1231/1234/1237/1240/1243/1246/1249/1252) → мотион 5 → гейт;
	//   ряды 2 и 3 → мотионы 10/11, гейта нет — но там «off» СРЕЗАЕТ клип удара через ~60 мс.
	//   Обе половины дают РАЗНОЕ поведение у разных рядов — этого в порту быть не должно.
	//   ИСПРАВЛЕНО (закон fix-original-defects): если в момент роспуска играет ОДНОКРАТНЫЙ мотион
	//   (8 призыв / 5 / 10 / 11 удар), уход ОТКЛАДЫВАЕТСЯ до конца этого клипа, затем мотион 9
	//   проигрывается ЦЕЛИКОМ и только под него идёт гашение. Одинаково для всех 27 записей.
	//   Откат к оригиналу: `asda.SoulFx.LegacyOff 1` (или галка ниже).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SoulGuardian|Фейд")
	bool bLegacyOffGate = false;
	// ★ДЕФЕКТ ОРИГИНАЛА (§3.2): последний звуковой case в бинаре — 102 вместо 107, из-за чего звук
	//   БЕГА хранителя не играет никогда (мотион 2 не выставляется — слот 2 это меш).
	//   ИСПРАВЛЕНО: мотион 7 читает поле SndMove. bLegacyMoveSoundBug = воспроизвести дефект.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SoulGuardian|Звук")
	bool bLegacyMoveSoundBug = false;
	// Радиус 3D-звука (min/max), как у прочих 3D-звуков порта.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SoulGuardian|Звук")
	float SoundMinDist = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SoulGuardian|Звук")
	float SoundMaxDist = 10000.f;
	// Вероятность выбора мотиона 0 (IDLE A) на конце клипа в покое: эталон `rand()%10 >= 3` = 70 %.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SoulGuardian|Анимация")
	int32 IdleAThreshold = 3;
	// Вероятность выбора мотиона 4 (ATTACK B): эталон `rand()%10 >= 5` = 50 %.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SoulGuardian|Анимация")
	int32 AttackBThreshold = 5;

	// ── ★ СЛОЙ 2 УДАРА: собственный эффект скилла («…_h_…», skill_info.sif +0xD1) ─────────────────
	//   Данные есть у ВСЕХ 27 активных скиллов хранителя (id 1228..1254), ЧИТАТЕЛЯ поля в клиенте
	//   пользователя не найдено (обоснование — шапка Public/Data/AsdaSkillFxRows.h). Порт спавнит
	//   слой сам: имя берётся ТОЛЬКО из данных (GuardianSkillId → DT_SkillFx.HitFx), потому что
	//   формула по индексу врёт на 6 записях из 27 (у `am` SkillType 7 и 8 переставлены).
	//   Выключить целиком: `asda.SoulFx.HitFx 0`.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SoulGuardian|Эффекты")
	bool bSpawnSkillHitFx = true;
	// Содержимое «h»-эффектов — импакт-вспышка/кольца/искры, центр (0, 0..15, 0) = ЗЕМЛЯ под целью
	//   (проверено дампом всех 27: pos-каналы у 26 из 27 нулевые, у одного (0,0,−200)).
	//   ⇒ по умолчанию сажаем на ВЫБРАННУЮ ЦЕЛЬ игрока. Цели нет (тест `asda.SoulFx.Skill`) —
	//   фолбэк на владельца, как слой 1. `asda.SoulFx.HitOnOwner 1` — всегда на владельце.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SoulGuardian|Эффекты")
	bool bHitFxOnOwner = false;

	// ── состояние (видно в редакторе) ──
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SoulGuardian|Состояние")
	int32 CurMotion = -1;           // актёр [441]
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SoulGuardian|Состояние")
	int32 PrevMotion = -1;          // актёр [442]
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SoulGuardian|Состояние")
	bool bRemoving = false;         // актёр +1744 == 0: идёт мягкое удаление (клип 9 + гашение)
	// Роспуск принят, но однократный мотион (призыв/удар) ещё доигрывает — «off» и гашение отложены.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SoulGuardian|Состояние")
	bool bOffPending = false;

	// ── API ──
	// Собрать компаньона: модель/клипы из записи SoulGuarderTable, призыв (мотион 8) и эффект смены.
	void InitGuardian(int32 InSoulGuardIndex, int32 InOwnerUserIdx, AActor* InOwner);
	// Мягкое удаление (эталон `charMgr_RemoveSummonById(..., bHard=0)`): мотион 9 «off» + гашение.
	void BeginSoftRemove();
	// Игрок применил АКТИВНЫЙ скилл хранителя (1228..1254). Вариант 1/2/3 из SkillCoinTable.Field16
	//   → мотион 5/10/11 и эффект слота 31/32/33 (§7.2). Вариант вне 1..3 = обычный скилл → мотион 5 без эффекта.
	void NotifyGuardianSkill(int32 Variant);

	// ★ ОТСТУПЛЕНИЕ (юзер 2026-08-08): перед ударом разворачиваем ВЛАДЕЛЬЦА к выбранной цели тем же
	//   эталонным механизмом, что и обычная атака (`AttackFaceTarget` / `objAI_FaceTargetByUserIdx`).
	//   Компаньон и слой 1 эффекта живут в трансформе владельца ⇒ разворот владельца разворачивает
	//   и их. Цель берётся только у СВОЕГО игрока. Откат: `asda.SoulFx.FaceTarget 0`.
	void FaceOwnerToTarget() const;
	// Принудительно проиграть мотион (консоль `asda.SoulFx.Motion`). bIgnoreGates — обойти гейты 8/9/5.
	void PlayMotionForTest(int32 Motion, bool bIgnoreGates);
	// Однострочный дамп состояния (консоль `asda.SoulFx.Dump`).
	FString DumpLine() const;

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

private:
	// Эталонный `Char_SetMotion@0xFA7560`: гейты «не прерывать 8/9/5», смена клипа, звук, эффект скилла.
	void SetMotion(int32 Motion, bool bSpawnFx, int32 FxSlot, bool bIgnoreGates = false);
	// Реакция на СМЕНУ состояния владельца (§7.2).
	void OnOwnerStateChanged(int32 NewState);
	// Реакция по КОНЦУ клипа (§7.3).
	void OnClipFinished();
	// Числовое состояние владельца в кодах эталона (0 покой, 1 движение, 5 атака, 6 смерть, 9 сидит).
	int32 ReadOwnerState(const AActor* Own) const;
	// Клип мотиона: поле записи → путь ассета → загрузка. nullptr, если поле пусто/ассета нет.
	UAnimSequence* LoadMotionClip(int32 Motion) const;
	// Имя звукового файла мотиона (слот 100 + мотион), пусто — звука нет.
	FString MotionSoundName(int32 Motion) const;
	// Проиграть клип через слот-монтаж с кросс-фейдом CrossFadeSec (бесконечный луп; конец клипа
	//   отслеживаем сами — как эталон отслеживает завершение трека).
	void PlayMotionClip(UAnimSequence* Clip);
	// Эффект хранителя на кости ВЛАДЕЛЬЦА M_DUMMY/G_DUMMY (эталон: НЕ на компаньоне), scale 1.0.
	void SpawnOwnerBoundFx(const FString& JebBase) const;
	// Загрузить BP эффекта, заспавнить в точке/повороте и выставить время жизни по эмиттерам.
	//   Общая часть обоих слоёв удара; nullptr — BP не собран (лог уже написан).
	class AAsdaEffectActor* SpawnFxActor(const FString& JebBase, const FVector& Loc, const FRotator& Rot) const;
	// ★ СЛОЙ 2: собственный «h»-эффект скилла хранителя (DT_SkillFx по Row.GuardianSkillId).
	void SpawnSkillHitFx() const;
	// Имя «h»-эффекта из DT_SkillFx (нижний регистр). Пусто — записи нет/таблица не собрана.
	FString FindSkillHitJeb() const;
	// Длина клипа «off» — база длительности исправленного гашения.
	float OffClipLength() const;
	// Запустить гашение (эталонная альфа-ветка `UpdateList_ModelAnim`) и уничтожение по его концу.
	//   bOffPlaying — реально ли играет клип 9 (влияет только на длительность и текст лога).
	void StartFadeOut(bool bOffPlaying);
	// Однократный мотион доиграл (или истёк предохранитель) — ставим «off» и запускаем гашение.
	void FinishPendingOff();

	FAsdaSoulGuarderRow Row;        // запись SoulGuarderTable (копия: таблица живёт в подсистеме)
	bool  bRowValid = false;
	float ClipTimeLeft = 0.f;       // остаток текущего клипа (эталонное «трек доиграл»)
	float CurClipLen = 0.f;
	int32 LastOwnerState = -1;      // актёр [446]
	bool  bFirstFrame = true;
	float OffPendingWait = 0.f;     // предохранитель отложенного «off» (клип не кончился — не зависаем)
	int32 OffBlockedByMotion = -1;  // какой однократный мотион держал уход (для лога/дампа)
};
