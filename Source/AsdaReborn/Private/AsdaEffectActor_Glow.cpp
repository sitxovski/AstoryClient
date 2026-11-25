// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectActor_Glow.cpp — ★ R2 10.11.2026 (разрез AsdaEffectActor.cpp, дословный перенос): сохранение/восстановление свечения эмиттеров, материалы и переопределения свечения, дев-команды свечения.
//   Общее для частей — в AsdaEffectActorInternal.h; шапка и реверс — в AsdaEffectActor.cpp.
#include "AsdaEffectActor.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaView.h"   // ★ волна 3 п.11 (15.09.2026): общая точка наблюдателя (E7)
#include "AsdaSelfTestBatch.h"   // ★ ревизия В2 дедупа №2 (11.10.2026): Quit — выход самотеста по итогу (перехватывается пакетом)
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10): имя параметра EmissiveBoost
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "World/AsdaCoord.h"                    // ★ Аудит №3 (B1): единый своп осей игра→UE
#include "ProceduralMeshComponent.h"
#include "AsdaFxNames.h"   // теги свечения заточки (цвет вершин)
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "ScopedTransaction.h"   // ★ Ф17 (С-1): уборка сирот обязана отменяться Ctrl+Z — разбор у DropOldestGlowOrphans
#endif
#include "Kismet/GameplayStatics.h"
// ★ Для консольной проверки яркости `asda.Fx.MatDump` (конец файла): чтение скалярного параметра
//   с ЭКЗЕМПЛЯРА материала на секции, отложенные шаги сценария и выполнение консольных команд.
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"   // FMaterialParameterInfo (MaterialTypes.h объявлен устаревшим)
#include "Materials/MaterialInstanceDynamic.h"  // ★ Ф5: MID «силы свечения» поверх материала эмиттера
#include "TimerManager.h"
#include "EngineUtils.h"   // TActorIterator — чтение живых эффектов мира (`asda.Fx.SectionDump`)
#include "UObject/UObjectIterator.h"   // ★ Ф16 (МЕЛК-4): TObjectIterator — CDO запечённых BP_Fx_* (в мире их нет)
#include "World/AsdaFxLightSettings.h"    // ★ 13.09.2026: переопределения свечения предмета/эффекта и сила свечения семейств
#include "World/AsdaFxLightSubsystem.h"   // ★ 13.09.2026: ключ отката asda.Fx.Glow.Overrides, имя .jeb, перезаведение света
#include "Data/AsdaFxLoopRows.h"          // ★ 08.10.2026: ЦИКЛ 1:1 — endMode диска, контейнер, сид-узлы карт (DT_FxLoop)
#include "Engine/DataTable.h"             // ★ 08.10.2026: чтение DT_FxLoop
#include "AsdaDataTableCache.h"            // ★ дедуп №2 В2-C (F3): AsdaDt::Cached — общая загрузка DT_FxLoop
#include "UI/AsdaEffectsSubsystem.h"        // ★ дедуп №2 В2-C (F3): удержание DT_FxLoop на сессию (FAsdaHeldTables)
#include "Engine/GameInstance.h"           // ★ дедуп №2 В2-C (F3): GameInstance эффекта — владелец удержания
#include "Components/SkeletalMeshComponent.h"   // ★ 08.10.2026: холдер режима 2 — порог анимации носителя («≥ порога» с защёлкой)
#include "World/AsdaWorldKind.h"              // ★ F7 (дедуп №2, В3): общий признак «сцена логина»
#include "World/AsdaFxLightComponent.h"        // ★ 15.10.2026: RestVerts строятся только при свете эффекта (IsCarrierAnchorOn)
#if WITH_EDITOR
#include "HAL/PlatformApplicationMisc.h"      // ★ 27.10.2026: буфер обмена кнопок «Копировать/Вставить свет»
#endif
#include "AsdaEffectActorInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaEffectActor*.cpp

// ═══════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ВОЛНА Ф8: ДОЛГОВЕЧНАЯ КОПИЯ ПОЭМИТТЕРНОГО СВЕЧЕНИЯ (переживает перепечку из .jeb).
// ═══════════════════════════════════════════════════════════════════════════════════════════════════
// Настройки «как из коробки»? Такие в копию не пишем и такими не затираем чужие.
static FORCEINLINE bool AsdaGlowIsDefault(const FAsdaFxEmitterGlow& G)
{
	return G.Source == EAsdaFxEmitterGlowSource::FromEffect && !G.bEmitsFlare;
}

// ═══ ★★★ ВОЛНА Ф13 (С1, 20.09.2026): КОМУ ПРИНАДЛЕЖИТ ЗАПИСЬ КОПИИ ════════════════════════════════
//   Сопоставление записей копии с текущими эмиттерами — ТЕМИ ЖЕ ТРЕМЯ КЛЮЧАМИ по убыванию строгости,
//   что и у RestoreEmitterGlow, но БЕЗ отбора «только ненастроенные эмиттеры»: здесь вопрос не
//   «кому раздать числа», а «чья это запись». Эмиттер, уже настроенный в массиве, свою запись всё
//   равно представляет — и дублировать её не нужно.
//   OutSavedToEmitter[k] — номер эмиттера, которому принадлежит запись k, либо INDEX_NONE, если
//   такого эмиттера в массиве БОЛЬШЕ НЕТ (перепечка переименовала или укоротила список).
// ═══ ★★★ ВОЛНА Ф14 (МЕЛК-1, 20.09.2026): ЭТО ТЕПЕРЬ ЕДИНСТВЕННЫЙ РАЗБОР ════════════════════════
//   Второй его экземпляр жил в RestoreEmitterGlow (тот же switch, но со своим отбором эмиттеров).
//   Две копии одного закона расходятся молча, и тогда запись при СОХРАНЕНИИ принадлежит одному
//   эмиттеру, а при ВОССТАНОВЛЕНИИ — другому; именно этим и пахла жалоба «проход 3 может отдать
//   чужие числа другому эмиттеру». Теперь разбор один, а «раздавать ли» решается по его итогу.
//   ★ СЛАБЕЙШИЙ КЛЮЧ (голый номер) ОГРАНИЧЕН. Прежде `R.Index == i` хватало, и запись
//   «CFA_01 / тип 0xE», чей эмиттер сменил тип .jeb, доставалась СОСЕДУ с тем же номером — то есть
//   чужие числа. Теперь по голому номеру сходятся только те, кого по имени сопоставить НЕЛЬЗЯ:
//   запись, чьё имя носит кто-то из нынешних эмиттеров, никому другому по номеру не отдаётся, и
//   эмиттер, чьё имя есть в какой-то записи, чужую запись по номеру не берёт. Имена пустые
//   (в архиве такие эмиттеры есть) ограничением не задеты — им голый номер как был последней
//   линией обороны, так и остался.
static void AsdaGlowMatchSavedToEmitters(const TArray<FAsdaFxEmitter>& InEmitters,
	const TArray<FAsdaFxEmitterGlowSaved>& InSaved, TArray<int32>& OutSavedToEmitter)
{
	OutSavedToEmitter.Init(INDEX_NONE, InSaved.Num());
	TBitArray<> EmitterTaken(false, InEmitters.Num());

	// Имена, по которым сопоставление В ПРИНЦИПЕ возможно, — для ограничения слабейшего ключа
	TSet<FString> NamesInEmitters;   // непустые имена нынешних эмиттеров
	TSet<FString> NamesInSaved;      // непустые имена записей копии
	for (const FAsdaFxEmitter& E : InEmitters) { if (!E.Name.IsEmpty()) { NamesInEmitters.Add(E.Name); } }
	for (const FAsdaFxEmitterGlowSaved& R : InSaved) { if (!R.Name.IsEmpty()) { NamesInSaved.Add(R.Name); } }

	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		for (int32 i = 0; i < InEmitters.Num(); ++i)
		{
			if (EmitterTaken[i]) { continue; }
			for (int32 k = 0; k < InSaved.Num(); ++k)
			{
				if (OutSavedToEmitter[k] != INDEX_NONE) { continue; }
				const FAsdaFxEmitterGlowSaved& R = InSaved[k];
				bool bHit = false;
				switch (Pass)
				{
				case 0: bHit = !R.Name.IsEmpty() && R.Name == InEmitters[i].Name && R.JebType == InEmitters[i].JebType; break;
				case 1:
					// ═══ ★★★ ВОЛНА Ф15 (МЕЛК-1, 20.09.2026): ОГРАНИЧЕНИЕ ИМЁН НУЖНО И ЗДЕСЬ ═══════
					//   Ф14 ограничила только проход 2 (голый номер), а проход 2 — это тот же номер
					//   БЕЗ проверки типа .jeb. Проход 1 (номер + тип) остался без ограничения и
					//   потому продолжал раздавать чужие числа. Контрпример ревизии:
					//     эмиттеры [0]{"BG",0x5} [1]{"X",0xE} [2]{"CFA_01",0x4E},
					//     запись   {Name "CFA_01", тип 0xE, номер 1}
					//   Проход 0 её не берёт (у «CFA_01» теперь тип 0x4E), а проход 1 видит
					//   номер 1 и тип 0xE у эмиттера «X» — и отдаёт записи «CFA_01» ЧУЖИЕ числа
					//   эмиттеру «X». Совпадение типа тут ничего не доказывает: типов .jeb мало,
					//   и у соседей они повторяются.
					//   Ограничение — ДОСЛОВНО ТО ЖЕ, что у прохода 2: по номеру сходятся только те,
					//   кого по имени сопоставить НЕЛЬЗЯ. Перепечку с переименованием оно не задевает
					//   (старых имён в новом списке нет, новых — нет в копии), а имена пустые
					//   (такие эмиттеры в архиве есть) не задеты вовсе.
					bHit = R.Index == i && R.JebType == InEmitters[i].JebType
						&& (R.Name.IsEmpty() || !NamesInEmitters.Contains(R.Name))
						&& (InEmitters[i].Name.IsEmpty() || !NamesInSaved.Contains(InEmitters[i].Name));
					break;
				default:
					// ★ Ф14: голый номер — только для пары, которую по имени не свести ни с кем
					bHit = R.Index == i
						&& (R.Name.IsEmpty() || !NamesInEmitters.Contains(R.Name))
						&& (InEmitters[i].Name.IsEmpty() || !NamesInSaved.Contains(InEmitters[i].Name));
					break;
				}
				if (!bHit) { continue; }
				OutSavedToEmitter[k] = i;
				EmitterTaken[i] = true;
				break;
			}
		}
	}
}

void AAsdaEffectActor::SaveEmitterGlow(bool bAuthoritative)
{
	// ═══ ★★★ ВОЛНА Ф13 (С1): НЕСОПОСТАВЛЕННЫЕ ЗАПИСИ КОПИИ ПЕРЕЖИВАЮТ ЛЮБУЮ ЗАПИСЬ ════════════════
	//   ЧТО БЫЛО НЕ ТАК. Свежий снимок собирался ТОЛЬКО из `Emitters`, и присваивание выбрасывало
	//   всё, чему в текущем списке эмиттеров пары не нашлось. А именно такие записи — единственное,
	//   что остаётся от настроек владельца, если сборщик перепечатал список из .jeb и переименовал
	//   (или укоротил) эмиттеры: RestoreEmitterGlow их не разобрал, они лежат в копии и ждут
	//   эмиттера с прежним именем. Сторож «пустой снимок не затирает копию» этот случай не ловил
	//   вовсе: стоило хоть одному эмиттеру быть настроенным, снимок переставал быть пустым, и
	//   уцелевшие записи стирались. В авторитетном вызове (любая правка в конфигураторе, раздача в
	//   экземпляры) это происходило безусловно — то есть конфигуратор чистил ровно то, что сторож
	//   и заводился беречь.
	//   ЛЕЧЕНИЕ. Копия пересобирается ПО СТАРОЙ КОПИИ: запись, у которой хозяин в списке эмиттеров
	//   есть, заменяется его нынешним состоянием (или уходит, если владелец вернул эмиттер к
	//   умолчанию); запись, у которой хозяина НЕТ, переносится как есть. Настроенные эмиттеры, за
	//   которыми записи не было, дописываются в конец. Порядок записей при этом сохраняется — от
	//   него зависит сверка «ассет вернулся к исходному» в конфигураторе.
	TArray<int32> SavedToEmitter;
	AsdaGlowMatchSavedToEmitters(Emitters, EmitterGlowSaved, SavedToEmitter);

	// Снять запись с эмиттера i (его нынешнее состояние)
	auto MakeRecord = [this](int32 i)
	{
		FAsdaFxEmitterGlowSaved R;
		R.Name = Emitters[i].Name;
		R.JebType = Emitters[i].JebType;
		R.Index = i;
		R.Glow = Emitters[i].Glow;
		return R;
	};

	// ★ Ф14 (МЕЛК-1): сколько записей копии осталось без эмиттера-хозяина. Считаем ДО переноса
	int32 OrphansTotal = 0;
	for (int32 k = 0; k < EmitterGlowSaved.Num(); ++k) { if (SavedToEmitter[k] == INDEX_NONE) { ++OrphansTotal; } }
	// ═══ ★★★ ВОЛНА Ф16 (МЕЛК-4, 20.09.2026): ЗДЕСЬ БОЛЬШЕ НЕ ВЫБРАСЫВАЕТСЯ НИЧЕГО ═════════════════
	//   Ф15 свела выброс к «только в авторитетном вызове», но авторитетный вызов — это и есть самый
	//   частый жест: правка любого поля эмиттера в панели деталей и КАЖДЫЙ ТИК перетаскивания
	//   ползунка (Interactive ≠ Unspecified ⇒ «панель» ⇒ авторитетно). То есть возражение
	//   «выбрасывать данные нельзя» просто переехало с сохранения на правку, где срабатывает чаще.
	//   Теперь потолок — ПОРОГ ПРЕДУПРЕЖДЕНИЯ, а не ножницы: превышение говорится в лог и уходит
	//   событием владельцу (ОДИН раз на актор за сеанс — иначе ползунок дал бы поток сообщений).
	//   Убрать лишнее можно только ЯВНОЙ командой `asda.Fx.Glow.DropOrphans` (DropOldestGlowOrphans).
	//   Разбор цены и обоснование — у объявления kEmitterGlowOrphanCap в AsdaEffectActor.h.
	if (OrphansTotal > kEmitterGlowOrphanCap && !bGlowOrphanOverflowReported)
	{
		bGlowOrphanOverflowReported = true;
		UE_LOG(LogAsdaFxManualLock, Warning, TEXT("[поэмиттерное свечение] %s: записей копии без эмиттера-хозяина %d при пороге %d — НЕ отброшена ни одна. Убрать самые старые: консольная команда `asda.Fx.Glow.DropOrphans %s`"),
			*GetName(), OrphansTotal, kEmitterGlowOrphanCap, *GetName());
		OnEmitterGlowOrphanOverflow().Broadcast(this, OrphansTotal, kEmitterGlowOrphanCap);
	}

	TArray<FAsdaFxEmitterGlowSaved> Fresh;
	TBitArray<> EmitterCovered(false, Emitters.Num());
	int32 Kept = 0;   // сколько записей уцелело без хозяина — для лога
	for (int32 k = 0; k < EmitterGlowSaved.Num(); ++k)
	{
		const int32 i = SavedToEmitter[k];
		if (i == INDEX_NONE)
		{
			// ★ Хозяина нет — запись НЕ НАША, чтобы её выбрасывать. Переносим дословно.
			// ★ Ф16 (МЕЛК-4): БЕЗ ИСКЛЮЧЕНИЙ. Прежде здесь стояла чистка по потолку, и она
			//   срабатывала на обычной правке поля в панели и на каждом тике ползунка. Уборка
			//   переехала в DropOldestGlowOrphans — только по явной команде владельца.
			Fresh.Add(EmitterGlowSaved[k]);
			++Kept;
			continue;
		}
		EmitterCovered[i] = true;
		// Пустые (полностью дефолтные) настройки в копию не пишутся — иначе массив вырос бы на
		// всех 1274 ассетах ради нулей; и именно так отменённая правка уходит из копии
		if (!AsdaGlowIsDefault(Emitters[i].Glow)) { Fresh.Add(MakeRecord(i)); }
	}
	for (int32 i = 0; i < Emitters.Num(); ++i)
	{
		if (EmitterCovered[i] || AsdaGlowIsDefault(Emitters[i].Glow)) { continue; }
		Fresh.Add(MakeRecord(i));
	}
	if (Kept > 0)
	{
		UE_LOG(LogAsdaFxManualLock, Verbose, TEXT("[поэмиттерное свечение] %s: %d запись(ей) копии без эмиттера-хозяина перенесены как есть (перепечка переименовала список)"),
			*GetName(), Kept);
	}

	// ★ КОПИЮ НЕ ЧИСТИМ ДОЧИСТА. Если сборщик уже перепечатал эмиттеры, а настройки ещё НЕ
	//   восстановлены (сохранение случилось раньше первого RestoreEmitterGlow), «свежий» снимок пуст —
	//   и голое присваивание стёрло бы единственную уцелевшую копию. Поэтому пустой снимок при
	//   непустой копии ИГНОРИРУЕТСЯ: терять данные молча нельзя, а лишняя запись безвредна.
	// ★★★ РЕВИЗИЯ Ф12 (К1): СТОРОЖ РАБОТАЕТ ТОЛЬКО В «НЕАВТОРИТЕТНОМ» ВЫЗОВЕ. Смысл его сохранён
	//   целиком — он по-прежнему прикрывает единственный опасный случай (перепечка списка эмиттеров
	//   сборщиком до восстановления). Но когда массив только что правил САМ ВЛАДЕЛЕЦ через
	//   конфигуратор (bAuthoritative), пустой снимок означает «настроенных эмиттеров больше нет» —
	//   и копию надо чистить, иначе отменённая правка вернётся при следующей загрузке ассета.
	//   ★ Ф13: сторож смотрит на ИТОГОВЫЙ снимок, а не на «собранный из эмиттеров»: после перепечки
	//     записи сопоставляются по номеру, глоу у них дефолтный, и итог законно выходит пустым —
	//     ровно тот случай, ради которого сторож и стоит.
	//   См. развёрнутое обоснование у объявления в AsdaEffectActor.h.
	//   ★★★ Ф14 (К-1): СТОРОЖ ОСТАВЛЕН КАК ЕСТЬ — ЧИНИЛОСЬ МЕСТО ВЫЗОВА, А НЕ ОН. Потеря правки
	//     владельца шла не отсюда: сюда просто приходил неавторитетный вызов от панели деталей.
	//     Признак «по данным» (отпечаток массива эмиттеров) пробовался и отвергнут ЗАМЕРОМ —
	//     обоснование и цифры у объявления в AsdaEffectActor.h. Трогать верную защиту ради
	//     второго, ничего не различающего механизма нельзя (owner-asset-edits-never-lose).
	if (!bAuthoritative && Fresh.Num() == 0 && EmitterGlowSaved.Num() > 0)
	{
		// Молчащая защита неотличима от потери данных — поэтому срабатывание видно в логе
		UE_LOG(LogAsdaFxManualLock, Verbose, TEXT("[поэмиттерное свечение] %s: свежий снимок пуст при копии из %d записей — копия СОХРАНЕНА (страховка от перепечки)"),
			*GetName(), EmitterGlowSaved.Num());
		return;
	}
	EmitterGlowSaved = MoveTemp(Fresh);
}

// ═══ ★★★ ВОЛНА Ф16 (МЕЛК-4, 20.09.2026): УБОРКА СИРОТ — ТОЛЬКО ПО ЯВНОЙ КОМАНДЕ ══════════════════
//   Обоснование целиком — у объявления kEmitterGlowOrphanCap в AsdaEffectActor.h. Здесь важно
//   одно: эту функцию НЕ зовёт ни сохранение, ни правка — только консольная команда
//   `asda.Fx.Glow.DropOrphans`, то есть прямое согласие владельца.
// ★ Ф16 (ДИСЦИПЛИНА): метка компиляции ЭТОЙ единицы трансляции. Обоснование — у объявления.
FString AAsdaEffectActor::BuildStamp()
{
	return FString(TEXT(__DATE__)) + TEXT(" ") + TEXT(__TIME__);
}

AAsdaEffectActor::FAsdaFxOnGlowOrphanOverflow& AAsdaEffectActor::OnEmitterGlowOrphanOverflow()
{
	static FAsdaFxOnGlowOrphanOverflow Event;   // один на модуль: подписчик — редакторский плагин
	return Event;
}

// ★ Ф17 (С-2): событие «ключ гейта поднят, а правка похожа на человеческую». Обоснование — у объявления.
AAsdaEffectActor::FAsdaFxOnGlowWriterGateSuspect& AAsdaEffectActor::OnEmittersWriterGateSuspect()
{
	static FAsdaFxOnGlowWriterGateSuspect Event;   // один на модуль: подписчик — редакторский плагин
	return Event;
}

// ★ Ф18 (МЕЛК-4): ЗДЕСЬ СТОЯЛО ОПРЕДЕЛЕНИЕ СТАТИКА bLastOrphanDropUndoable. Признак по смыслу
//   относится к АКТОРУ (как прошла уборка у ЭТОГО эффекта), поэтому он стал обычным полем с
//   инициализацией у объявления — разбор в AsdaEffectActor.h.
int32 AAsdaEffectActor::DropOldestGlowOrphans(bool bDryRun, bool bForceWithoutUndo)
{
	if (EmitterGlowSaved.Num() == 0) { return 0; }
	// Кто из записей остался без эмиттера-хозяина — тем же разбором, что и при сохранении
	TArray<int32> SavedToEmitter;
	AsdaGlowMatchSavedToEmitters(Emitters, EmitterGlowSaved, SavedToEmitter);
	int32 OrphansTotal = 0;
	for (int32 k = 0; k < EmitterGlowSaved.Num(); ++k) { if (SavedToEmitter[k] == INDEX_NONE) { ++OrphansTotal; } }
	int32 ToDrop = FMath::Max(0, OrphansTotal - kEmitterGlowOrphanCap);
	if (ToDrop == 0)
	{
		UE_LOG(LogAsdaFxManualLock, Log, TEXT("[поэмиттерное свечение] %s: сирот %d при пороге %d — убирать нечего"),
			*GetName(), OrphansTotal, kEmitterGlowOrphanCap);
		return 0;
	}
	if (bDryRun)
	{
		UE_LOG(LogAsdaFxManualLock, Log, TEXT("[поэмиттерное свечение] %s: ПРОБА — отброшено было бы %d из %d сирот (порог %d)"),
			*GetName(), ToDrop, OrphansTotal, kEmitterGlowOrphanCap);
		return ToDrop;
	}
#if !WITH_EDITOR
	// Вне редактора буфера отмены не существует вовсе, поэтому согласие `force` здесь ничего не
	//   решает и параметр не читается. Гасим предупреждение о неиспользованном параметре явно.
	(void)bForceWithoutUndo;
#endif
#if WITH_EDITOR
	// ═══ ★★★ ВОЛНА Ф18 (МЕЛК-3): ОТКАТА НЕТ — НЕ ОТБРАСЫВАЕМ ВОВСЕ ══════════════════════════════
	//   Разбор целиком — у объявления DropOldestGlowOrphans в AsdaEffectActor.h. Коротко: волна
	//   Ф17 научилась ЗАМЕЧАТЬ недоступный откат, но записи всё равно отбрасывала — то есть в
	//   единственном опасном случае теряла настройки владельца безвозвратно. Теперь нужно ЯВНОЕ
	//   второе согласие (`force` у консольной команды).
	// ═══ ★★★ ВОЛНА Ф19 (МЕЛК-1): ПРОВЕРКА ПОДНЯТА ВЫШЕ ТРАНЗАКЦИИ И ВЫШЕ ФЛАГА ═══════════════════
	//   ЧТО БЫЛО НЕ ТАК ПОСЛЕ Ф18. Проверка стояла ниже `FScopedTransaction` и `SetFlags`, а текст
	//   уверял, что отказ «не оставляет следов вовсе». Следы были:
	//     (1) открытая транзакция. Её Begin ВЫНИМАЕТ стек повтора (UTransBuffer::BeginInternal,
	//         TransBuffer.h:77-90, при `UndoCount > 0`). ★ Потери Redo из этого НЕ выходило: пустую
	//         транзакцию End признаёт транзиентной и возвращает вынутое обратно
	//         (FTransaction::IsTransient, EditorTransaction.cpp:515-527; End — там же, 1332-1353).
	//         Довод ревизии проверен замером с обеих сторон и в этой части не подтвердился. Но сам
	//         ход буфера «вынули-вернули» остаётся и держится на том, что в транзакцию никто ничего
	//         не положил — на такое опираться нельзя;
	//     (2) SetFlags(RF_Transactional) — правка объекта, а отказ обязан быть пустым.
	//   ЛЕЧЕНИЕ. Сначала решаем, доступен ли откат, и только потом открываем транзакцию.
	//   ★ ПРОВЕРКА ИДЁТ ДО Modify (и теперь до Begin) НАМЕРЕННО: Modify помечает пакет изменённым
	//     ДАЖЕ когда в буфер ничего не положил (Obj.cpp:1666-1673).
	//   Условие — то же, по которому решает движок, но взятое ДО открытия транзакции: либо
	//   объемлющая транзакция уже открыта (GUndo не нулевой, CoreGlobals.h:97 — так бывает при
	//   `asda.Fx.Glow.DropOrphans all`), либо редактор готов её открыть — `GEditor->CanTransact()`
	//   (EditorServer.cpp:1104-1110: буфер Trans есть, пакеты не грузятся, PostLoad не идёт) и не
	//   выполняется сам откат (`!GIsTransacting`). Ровно этими условиями руководствуется
	//   FScopedTransaction (ScopedTransaction.cpp:20), поэтому предсказание точное, а не похожее.
	//   Плюс объект в состоянии, допускающем правку (UObject::CanModify, Obj.cpp:1641-1650), и
	//   пакет не скриптовый. Флага RF_Transactional в условии нет: он ставится НИЖЕ, после проверки.
	// ═══ ★★★ ВОЛНА Ф20 (МЕЛК-1): «ПАКЕТ НЕ СКРИПТОВЫЙ» БЫЛО ВЗЯТО НЕ ОТТУДА ══════════════════════
	//   ЧТО БЫЛО НЕ ТАК. Здесь стояло `!HasAnyPackageFlags(PKG_ContainsScript)` — предикат,
	//   списанный с SaveToTransactionBuffer (UObjectGlobals.cpp:3216), — и текст уверял, что им
	//   закрывается нативный CDO из /Script/. ЭТО НЕВЕРНО, и проверено по движку 5.8 дважды:
	//     (1) PKG_ContainsScript НЕ ВЫСТАВЛЯЕТСЯ НИГДЕ. По всему Engine/Source флаг только
	//         ЧИТАЕТСЯ (HasAnyPackageFlags); ни одного SetPackageFlags(PKG_ContainsScript) нет.
	//         Пакеты /Script/ получают PKG_CompiledIn — UObjectBase::DeferredRegister
	//         (UObjectBase.cpp:215 `Package->SetPackageFlags(PKG_CompiledIn)`), а также
	//         Enum.cpp:1328 и UObjectGlobals.cpp:1313 для тех же compiled-in пакетов.
	//     (2) РЕШАЕТ ЗДЕСЬ НЕ SaveToTransactionBuffer, А UObject::Modify. Наш вызов до него и не
	//         доходит: Modify отсекает такие объекты РАНЬШЕ, своим собственным условием
	//         (Obj.cpp:1661) — `HasAnyPackageFlags(PKG_ContainsScript | PKG_CompiledIn) == false
	//         || GetClass()->HasAnyClassFlags(CLASS_DefaultConfig | CLASS_Config)`.
	//   ПОСЛЕДСТВИЕ НЕВЕРНОГО ПРЕДИКАТА. Для объекта в compiled-in пакете старое условие давало
	//   bUndoReady = «да»: уборка шла дальше, ставила RF_Transactional, Modify() возвращал false,
	//   и в лог уходило «выполнено по явному согласию `force`», которого никто не давал, после
	//   чего записи отбрасывались БЕЗ отката. На практике недостижимо (у нативного CDO
	//   EmitterGlowSaved пуст ⇒ ранний выход в начале функции), но и предикат, и обоснование были
	//   неверны, а «недостижимо сегодня» — не свойство кода, а свойство данных.
	//   ЛЕЧЕНИЕ. Условие приведено к тому, по которому и вправду решает Modify (Obj.cpp:1661),
	//   ВМЕСТЕ С ИСКЛЮЧЕНИЕМ: класс с CLASS_DefaultConfig | CLASS_Config Modify пропускает даже
	//   в compiled-in пакете (настройки живут в .ini и правятся законно). Без этого исключения мы
	//   отказывали бы там, где движок работает.
	const bool bTransactionPossible = (GUndo != nullptr)
		|| (GEditor != nullptr && GEditor->CanTransact() && !GIsTransacting);
	// Тот же предикат, что у UObject::Modify (Obj.cpp:1661), слово в слово и в том же порядке.
	const bool bPackageAllowsModify =
		!GetOutermost()->HasAnyPackageFlags(PKG_ContainsScript | PKG_CompiledIn)
		|| GetClass()->HasAnyClassFlags(CLASS_DefaultConfig | CLASS_Config);
	const bool bUndoReady = bTransactionPossible && CanModify() && bPackageAllowsModify;
	if (!bUndoReady && !bForceWithoutUndo)
	{
		bLastOrphanDropUndoable = false;   // уборки не было — отменять нечего
		UE_LOG(LogAsdaFxManualLock, Warning, TEXT("[поэмиттерное свечение] %s: уборка сирот ОТМЕНЕНА — буфер отмены недоступен, отбросить %d записей было бы безвозвратно. Ничего не изменено: пакет НЕ помечен, флаг не ставился, транзакция НЕ открывалась, стек повтора не тронут. Если записи и вправду не нужны, повторите с согласием: asda.Fx.Glow.DropOrphans %s force"),
			*GetName(), ToDrop, *GetName());
		return 0;
	}
	// ═══ ★★★ ВОЛНА Ф17 (С-1): ТРАНЗАКЦИЯ ОТКРЫВАЕТСЯ ЗДЕСЬ, А НЕ «КЕМ-НИБУДЬ» ═══════════════════
	//   Разбор целиком — у объявления DropOldestGlowOrphans в AsdaEffectActor.h. Коротко: прежде
	//   здесь стоял голый Modify() с обещанием «попадёт в транзакцию», но открыть её было некому,
	//   и SaveToTransactionBuffer уходил по `GUndo == nullptr` (UObjectGlobals.cpp:3218), делая
	//   одно только MarkPackageDirty. Отброшенные настройки владельца Ctrl+Z не возвращал.
	//   Транзакция ставится ДО Modify: буфер копирует объект в момент вызова Modify, а не потом.
	FScopedTransaction Tx(FText::Format(
		NSLOCTEXT("AsdaFx", "DropGlowOrphans", "Свечение эмиттеров: уборка записей-сирот у «{0}»"),
		FText::FromString(GetName())));
	// ★ БЕЗ ЭТОГО ФЛАГА ТРАНЗАКЦИЯ ОСТАЛАСЬ БЫ ПУСТОЙ. Уборка идёт по CDO запечённых BP_Fx_*, а
	//   CDO блюпринта создаётся без RF_Transactional (Class.cpp:5091) — SaveToTransactionBuffer
	//   вышел бы по `bIsTransactional == false` (UObjectGlobals.cpp:3215-3218). Движок в таких
	//   случаях ставит флаг руками (WidgetBlueprintEditorUtils.cpp:132-134); делаем то же.
	//   Флаг сериализуется (RF_Transactional входит в RF_Load, ObjectMacros.h), поэтому после
	//   «Save All» он остаётся в ЗАПИСАННОМ ассете и переживает перезагрузку редактора.
	// ★★★ Ф18 (МЕЛК-1): ЗДЕСЬ СТОЯЛО «остаётся у CDO навсегда» — ЭТО НЕВЕРНО. При ПЕРЕКОМПИЛЯЦИИ
	//   блюпринта прежний CDO уезжает в TRASH_* (KismetCompiler.cpp:666-677), а новый создаётся
	//   заново со строго заданным набором флагов RF_Public|RF_ClassDefaultObject|RF_ArchetypeObject
	//   (Class.cpp:5091) — RF_Transactional среди них нет. Свойства переносит
	//   CopyPropertiesForUnrelatedObjects, а он переносит ЗНАЧЕНИЯ, не флаги объекта. То есть после
	//   любой перекомпиляции BP_Fx_* флаг снова отсутствует.
	//   НА РАБОТОСПОСОБНОСТЬ ЭТО НЕ ВЛИЯЕТ: оба места, которым флаг нужен (здесь и у конфигуратора,
	//   AsdaFxGlowConfigModel.cpp), ставят его НЕПОСРЕДСТВЕННО перед Modify — то есть не полагаются
	//   на то, что он уцелел. Неверным было только утверждение в тексте.
	// ★ Ф19 (МЕЛК-1): флаг ставится ПОСЛЕ проверки отката — на ветке отказа объект не трогается.
	SetFlags(RF_Transactional);
	// ★ ПРОВЕРКА, А НЕ ВЕРА: Modify возвращает, попал ли объект в буфер отмены. Молчаливое «нет»
	//   здесь и было всей бедой — теперь оно говорит о себе вслух и видно самопроверке.
	bLastOrphanDropUndoable = Modify();
	if (!bLastOrphanDropUndoable)
	{
		// Сюда попадаем только с явным `force`: без него мы вышли выше и ничего не тронули
		UE_LOG(LogAsdaFxManualLock, Warning, TEXT("[поэмиттерное свечение] %s: уборка сирот НЕ попала в буфер отмены — Ctrl+Z её не вернёт (выполнено по явному согласию `force`). Пакет помечен изменённым; не записывайте ассет, пока не проверите копию"),
			*GetName());
	}
#endif
	TArray<FAsdaFxEmitterGlowSaved> Fresh;
	Fresh.Reserve(EmitterGlowSaved.Num() - ToDrop);
	int32 Left = ToDrop;
	for (int32 k = 0; k < EmitterGlowSaved.Num(); ++k)
	{
		// Отбрасываем САМЫЕ СТАРЫЕ: новые записи дописываются в конец, значит старые — в начале
		if (SavedToEmitter[k] == INDEX_NONE && Left > 0)
		{
			--Left;
			UE_LOG(LogAsdaFxManualLock, Warning, TEXT("[поэмиттерное свечение] %s: ОТБРОШЕНА запись копии без эмиттера-хозяина «%s» (тип 0x%X, номер %d)"),
				*GetName(), *EmitterGlowSaved[k].Name, EmitterGlowSaved[k].JebType, EmitterGlowSaved[k].Index);
			continue;
		}
		Fresh.Add(EmitterGlowSaved[k]);
	}
	EmitterGlowSaved = MoveTemp(Fresh);
	bGlowOrphanOverflowReported = false;   // порог снова свободен — следующее превышение скажем заново
	// ★ Ф17 (С-1): в тексте теперь сказано и про откат — раз он наконец работает, владелец должен
	//   знать, что передумать можно ДО записи ассета, а не только «проверьте и не сохраняйте».
	//   Директиве препроцессора внутри списка аргументов UE_LOG не место (макрос её не переживёт),
	//   поэтому слово об откате готовится отдельной строкой.
#if WITH_EDITOR
	const TCHAR* const UndoWord = bLastOrphanDropUndoable ? TEXT("доступен") : TEXT("НЕДОСТУПЕН");
#else
	const TCHAR* const UndoWord = TEXT("недоступен вне редактора");
#endif
	UE_LOG(LogAsdaFxManualLock, Warning, TEXT("[поэмиттерное свечение] %s: по команде владельца отброшено %d записей-сирот, осталось %d записей копии. Изменение живёт в памяти — запишите ассет (Ctrl+S) или отмените (Ctrl+Z, откат %s)"),
		*GetName(), ToDrop, EmitterGlowSaved.Num(), UndoWord);
	return ToDrop;
}

int32 AAsdaEffectActor::RestoreEmitterGlow()
{
	if (EmitterGlowSaved.Num() == 0 || Emitters.Num() == 0) { return 0; }
	// ★★★ Ф14 (МЕЛК-1): РАЗБОР ОДИН НА ДВОИХ — тот же, что решает «чья это запись» при сохранении.
	//   Своей копии трёх проходов здесь больше нет: обоснование у объявления в заголовке.
	TArray<int32> SavedToEmitter;
	AsdaGlowMatchSavedToEmitters(Emitters, EmitterGlowSaved, SavedToEmitter);
	int32 Done = 0;
	for (int32 k = 0; k < EmitterGlowSaved.Num(); ++k)
	{
		const int32 i = SavedToEmitter[k];
		if (i == INDEX_NONE) { continue; }                        // хозяина нет — запись ждёт своего эмиттера
		if (!AsdaGlowIsDefault(Emitters[i].Glow)) { continue; }   // уже настроен — копией не трогаем
		Emitters[i].Glow = EmitterGlowSaved[k].Glow;
		++Done;
	}
	return Done;
}

void AAsdaEffectActor::PostLoad()
{
	Super::PostLoad();
	// ★ Восстановление ДО первого тика и ДО открытия панели деталей: владелец, открыв перепечатанный
	//   ассет, обязан увидеть свои числа на месте.
	RestoreEmitterGlow();
}

void AAsdaEffectActor::PreSave(FObjectPreSaveContext Context)
{
	// Снимок ПЕРЕД записью — единственная точка, через которую проходит любой путь правки
	// (панель деталей, скрипт, копипаст актора).
	SaveEmitterGlow();
	Super::PreSave(Context);
}

// ═══ ★★★ ВОЛНА Ф5 (10.09.2026): «СИЛА СВЕЧЕНИЯ» — ЭМИССИЯ МАТЕРИАЛА С ПОЛЯ АКТОРА ══════════════════
//   Полное обоснование — в заголовке (AsdaEffectActor.h, объявление GlowMaterialFor). Коротко:
//   ветка Create ставит на секцию `E.Material` и снимает любой ранее назначенный MID, поэтому
//   значение применяется НЕ ОДНАЖДЫ, А В КАЖДОЙ ТОЧКЕ `SetMaterial` — через эту функцию.
static int32 GAsdaFxGlowActorOverride = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaFxGlowActorOverride,
	TEXT("asda.Fx.Glow.ActorOverride"),
	GAsdaFxGlowActorOverride,
	TEXT("1 (умолчание) = поле «Сила свечения» эффекта перебивает EmissiveBoost его материала; 0 = механизм спит, "
	     "рисуется экземпляр материала из таблицы Tools/asda_fx_boost.json. Пока в ассете не взведено "
	     "«Сила свечения: задать вручную», обе позиции ключа дают ОДНО И ТО ЖЕ."),
	ECVF_Default);

UMaterialInterface* AAsdaEffectActor::GlowMaterialFor(int32 Slot, UMaterialInterface* Base)
{
	// ★ ДЕФОЛТ «КАК БЫЛО»: пока ни один уровень цепочки силу свечения не задаёт (и при выключенном ключе)
	//   возвращаем РОВНО материал эмиттера. Заодно отпускаем ранее созданный MID — снятие галочки должно
	//   возвращать картинку к табличному значению немедленно, а не «до следующей загрузки уровня».
	// ★ 13.09.2026: значение берётся не только с поля актора, а по цепочке ResolveEmissiveOverride
	//   (переопределение предмета/эффекта из конфигуратора → поле ассета → семейство). Без записей в таблицах
	//   и без галочки на ассете цепочка возвращает false — ровно прежнее поведение.
	float Boost = 1.f;
	if (!Base || GAsdaFxGlowActorOverride == 0 || !ResolveEmissiveOverride(Boost))
	{
		if (GlowMIDs.IsValidIndex(Slot)) { GlowMIDs[Slot] = nullptr; }
		return Base;
	}
	if (GlowMIDs.Num() <= Slot) { GlowMIDs.SetNum(Slot + 1); }
	UMaterialInstanceDynamic* MID = GlowMIDs[Slot];
	// ★ Пересоздаём MID, только если РОДИТЕЛЬ сменился: пересборка секции происходит десятки раз в
	//   секунду, и создание экземпляра на каждой из них было бы мусором в GC на ровном месте.
	if (!MID || MID->Parent != Base)
	{
		MID = UMaterialInstanceDynamic::Create(Base, this);
		GlowMIDs[Slot] = MID;
	}
	if (!MID) { return Base; }
	// ★ У BLEND_MODULATE (мастер M_AsdaEffectMod) параметра EmissiveBoost нет вовсе — запись просто
	//   ничего не делает, ошибки не будет. Это ожидаемо и описано в подсказке поля.
	MID->SetScalarParameterValue(AsdaMat::kParamEmissiveBoost, Boost);   // ★ В1-C F10: имя параметра — общая константа
	return MID;
}

// ═══ ★★★ 13.09.2026: КОНФИГУРАТОР СВЕЧЕНИЯ — ПРЕДМЕТ ЭФФЕКТА И ЦЕПОЧКА ПЕРЕОПРЕДЕЛЕНИЙ ════════════════
void AAsdaEffectActor::SetSourceItemId(int32 ItemId)
{
	if (SourceItemId == ItemId) { return; }
	SourceItemId = ItemId;
	GlowOvRevision = -1;   // кэш поиска ключуется кодом предмета — сбросить явно
	// Эмиссия: секции могли уже собраться с материалом без переопределения — переставить сразу
	RefreshGlowMaterials();
	// Свет: набор компонентов зависит от ДЕЙСТВУЮЩИХ чисел, а переопределение предмета могло их сменить
	//   (включить/выключить свет, «свой у каждого эмиттера»). Заведение идемпотентно — приводит набор к нужному.
	if (UAsdaFxLightSubsystem* S = UAsdaFxLightSubsystem::Get(this)) { S->RegisterEffect(this); }
}

const FAsdaFxGlowOverride* AAsdaEffectActor::FindGlowOverride() const
{
	if (!UAsdaFxLightSubsystem::GlowOverridesEnabled()) { return nullptr; }
	// ★ Ревизия 13.09 (п.6): имя .jeb — часть ключа обоих кэшей (переименованный актор не должен держать чужую запись/правило)
	if (!GlowCacheEffectName.Equals(EffectName, ESearchCase::CaseSensitive))
	{
		GlowCacheEffectName = EffectName;
		GlowOvRevision = -1;
		GlowRuleRevision = -1;
	}
	const UAsdaFxLightSettings* S = UAsdaFxLightSettings::Get();
	if (S->Overrides.Num() == 0) { return nullptr; }   // быстрый путь: записей нет — как до конфигуратора
	const int32 Rev = UAsdaFxLightSettings::GetTablesRevision();
	if (GlowOvRevision != Rev || GlowOvItem != SourceItemId || GlowOvNum != S->Overrides.Num())
	{
		const FAsdaFxGlowOverride* Ov = S->FindOverride(UAsdaFxLightSubsystem::ResolveJebName(this), SourceItemId);
		GlowOvIndex = Ov ? (int32)(Ov - S->Overrides.GetData()) : INDEX_NONE;
		GlowOvRevision = Rev;
		GlowOvItem = SourceItemId;
		GlowOvNum = S->Overrides.Num();
	}
	return S->Overrides.IsValidIndex(GlowOvIndex) ? &S->Overrides[GlowOvIndex] : nullptr;
}

bool AAsdaEffectActor::ResolveEmissiveOverride(float& OutBoost) const
{
	// 1–2. Конфигуратор: «этот предмет + эффект», затем «эффект у всех предметов». Генератор взводит
	//      bOverrideEmissive у записи, ТОЛЬКО если силу свечения задала сама цепочка записей
	if (const FAsdaFxGlowOverride* Ov = FindGlowOverride())
	{
		if (Ov->Spec.bOverrideEmissive) { OutBoost = Ov->Spec.EmissiveBoost; return true; }
	}
	// 3. Поле ассета/актора (волна Ф5, галочка «Сила свечения: задать вручную»)
	if (FxLight.bOverrideEmissive) { OutBoost = FxLight.EmissiveBoost; return true; }
	// 4. Дефолты семейства (таблица asda_fx_light.json → override_emissive/emissive_boost). Ниже ассета: поштучная
	//    правка ассета владельцем важнее массовой. Правило ищется регуляркой — кэш по ревизии таблиц
	if (!UAsdaFxLightSubsystem::GlowOverridesEnabled()) { return false; }
	const UAsdaFxLightSettings* S = UAsdaFxLightSettings::Get();
	const int32 Rev = UAsdaFxLightSettings::GetTablesRevision();
	if (GlowRuleRevision != Rev)
	{
		const FString Jeb = UAsdaFxLightSubsystem::ResolveJebName(this);
		const FAsdaFxLightRule* R = Jeb.IsEmpty() ? nullptr : S->FindRule(Jeb);
		GlowRuleIndex = R ? (int32)(R - S->Families.GetData()) : INDEX_NONE;
		GlowRuleRevision = Rev;
	}
	if (S->Families.IsValidIndex(GlowRuleIndex) && S->Families[GlowRuleIndex].Spec.bOverrideEmissive)
	{
		OutBoost = S->Families[GlowRuleIndex].Spec.EmissiveBoost;
		return true;
	}
	return false;
}

void AAsdaEffectActor::RefreshGlowMaterials()
{
	if (!Mesh) { return; }
	// Правка ползунка в панели деталей не пересобирает секции — значение надо донести до уже
	// стоящих на них материалов руками. Пересборка донесёт его сама, через GlowMaterialFor.
	for (int32 i = 0; i < Emitters.Num(); ++i)
	{
		const FAsdaFxEmitter& E = Emitters[i];
		if (E.Material)  { Mesh->SetMaterial(i, GlowMaterialFor(i, E.Material)); }
		if (E.MaterialB) { Mesh->SetMaterial(Emitters.Num() + i, GlowMaterialFor(Emitters.Num() + i, E.MaterialB)); }
	}
}

#if WITH_EDITOR
// ═══ ★★★ ВОЛНА Ф5 (10.09.2026): ПРОВЕРКА КАТЕГОРИИ `Effect Glow` НАСТОЯЩИМ ПУТЁМ ПАНЕЛИ ДЕТАЛЕЙ ═══
//
//   ЗАЧЕМ ОТДЕЛЬНЫЕ КОМАНДЫ, А НЕ «ПРИСВОИЛ ПОЛЕ И ПОСМОТРЕЛ». Присвоение `Fx->FxLight.IntensityCd = X`
//   из скрипта НЕ ПРОХОДИТ через `PostEditChangeProperty`, а значит не запускает НИ ОДИН из трёх
//   механизмов, которые владелец видит в редакторе: замок «переопределено вручную», автоперевод цвета
//   в режим «Явный» и перестановку материалов «силы свечения». Проверка на таком пути подтверждала бы
//   несуществующее (закон diagnostic-must-use-real-path). Поэтому здесь строится РОВНО ТО ЖЕ СОБЫТИЕ,
//   что строит панель деталей при правке ЛИСТА структуры: MemberProperty = FxLight, Property = поле.
//   ★ То же и с UE-Python: `set_editor_property("fx_light", …)` пишет структуру ЦЕЛИКОМ (Leaf == Member),
//     то есть путь СКРИПТА ПРОСТАНОВКИ ДЕФОЛТОВ, а не путь руки владельца. Их нельзя путать.
static void AsdaFxGlowEditLeaf(AAsdaEffectActor* Fx, const TCHAR* LeafName)
{
	if (!Fx) { return; }
	FProperty* Member = FindFProperty<FProperty>(AAsdaEffectActor::StaticClass(), TEXT("FxLight"));
	FProperty* Leaf = FindFProperty<FProperty>(FAsdaFxLightSpec::StaticStruct(), LeafName);
	if (!Member || !Leaf)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FxGlow] поле «%s» не найдено в FAsdaFxLightSpec — правка не имитирована"), LeafName);
		return;
	}
	FPropertyChangedEvent Ev(Leaf, EPropertyChangeType::ValueSet);
	Ev.SetActiveMemberProperty(Member);
	Fx->PostEditChangeProperty(Ev);
}

// Короткая сводка полей категории (для лога «до/после»)
static FString AsdaFxGlowLine(const AAsdaEffectActor* Fx)
{
	const FAsdaFxLightSpec& S = Fx->FxLight;
	return FString::Printf(
		TEXT("вкл=%d замок=%d цвет_режим=%d цвет=(%d,%d,%d) сила_света=%.2f кд радиус=[%.0f..%.0f] "
		     "свечение_вручную=%d свечение=%.3f семейство=%s"),
		S.bEnabled ? 1 : 0, S.bManual ? 1 : 0, (int32)S.ColorMode,
		S.ColorSRGB.R, S.ColorSRGB.G, S.ColorSRGB.B, S.IntensityCd, S.RadiusMin, S.RadiusMax,
		S.bOverrideEmissive ? 1 : 0, S.EmissiveBoost, *S.FamilyId);
}

static AAsdaEffectActor* AsdaFxGlowFind(UWorld* W, const FString& Filter)
{
	if (!W) { return nullptr; }
	TArray<AActor*> Acts;
	UGameplayStatics::GetAllActorsOfClass(W, AAsdaEffectActor::StaticClass(), Acts);
	for (AActor* A : Acts)
	{
		AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(A);
		if (Fx && (Filter.IsEmpty() || Fx->EffectName.Contains(Filter))) { return Fx; }
	}
	UE_LOG(LogTemp, Warning, TEXT("[FxGlow] эффект-актор по фильтру «%s» не найден (всего в мире %d)"), *Filter, Acts.Num());
	return nullptr;
}

// ═══ ★★★ ВОЛНА Ф16 (МЕЛК-4, 20.09.2026): ЯВНАЯ УБОРКА ЗАПИСЕЙ-СИРОТ ══════════════════════════════
//   `asda.Fx.Glow.DropOrphans <фильтр|all> [dry]`. Единственный способ убрать лишние записи
//   долговечной копии: сами по себе они больше не отбрасываются НИКОГДА (разбор — у объявления
//   kEmitterGlowOrphanCap). Без `dry` команда правит объекты в памяти и помечает пакеты
//   изменёнными — записать их владелец должен сам (Ctrl+S / Save All), как и всякую свою правку.
//   ★ Обходим TObjectIterator, а не акторов мира: настройки свечения живут в CDO запечённых
//     BP_Fx_* (их и правит конфигуратор), а таких объектов в мире нет вовсе.
static FAutoConsoleCommand GAsdaFxGlowDropOrphans(
	TEXT("asda.Fx.Glow.DropOrphans"),
	TEXT("asda.Fx.Glow.DropOrphans <часть имени эффекта|all> [dry] [force] — убрать САМЫЕ СТАРЫЕ записи "
	     "«свечение эмиттеров: сохранённая копия», у которых в нынешнем списке эмиттеров хозяина нет, "
	     "до порога 32. Сама по себе копия не чистится никогда; `dry` — только посчитать; "
	     "`force` — согласие отбросить даже когда откат (Ctrl+Z) недоступен"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& A)
	{
		if (A.Num() < 1)
		{
			UE_LOG(LogAsdaFxManualLock, Warning, TEXT("[поэмиттерное свечение] нужно: asda.Fx.Glow.DropOrphans <часть имени эффекта|all> [dry] [force]"));
			return;
		}
		const FString Filter = (A[0] == TEXT("all")) ? FString() : A[0];
		// ★ Ф18 (МЕЛК-3): ключевые слова ищем ПО ВСЕМ аргументам, а не только во втором — иначе
		//   `DropOrphans all force` (без `dry`) читался бы как «слово на месте `dry`» и терялся.
		bool bDry = false, bForce = false;
		for (int32 i = 1; i < A.Num(); ++i)
		{
			if (A[i].Equals(TEXT("dry"), ESearchCase::IgnoreCase))        { bDry = true; }
			else if (A[i].Equals(TEXT("force"), ESearchCase::IgnoreCase)) { bForce = true; }
			else { UE_LOG(LogAsdaFxManualLock, Warning, TEXT("[поэмиттерное свечение] непонятный аргумент «%s» — ожидается `dry` или `force`"), *A[i]); }
		}
		int32 Touched = 0, Dropped = 0, Refused = 0;
#if WITH_EDITOR
		// ★ Ф17 (С-1): ОБЪЕМЛЮЩАЯ транзакция на всю команду — чтобы `DropOrphans all`, прошедший по
		//   десяткам эффектов, отменялся ОДНИМ Ctrl+Z, а не по разу на эффект. Вложение буфер
		//   поддерживает: запись заводит только внешний Begin (TransBuffer.h:70-77), внутренние
		//   лишь считаются. При `dry` не открываем вовсе — проба ничего не меняет и в буфер отмены
		//   попасть не должна (пустую транзакцию буфер всё равно выбросит, EditorTransaction.cpp).
		TUniquePtr<FScopedTransaction> Tx;
		if (!bDry)
		{
			Tx = MakeUnique<FScopedTransaction>(FText::Format(
				NSLOCTEXT("AsdaFx", "DropGlowOrphansAll", "Свечение эмиттеров: уборка записей-сирот (фильтр «{0}»)"),
				FText::FromString(Filter.IsEmpty() ? TEXT("все") : *Filter)));
		}
#endif
		for (TObjectIterator<AAsdaEffectActor> It; It; ++It)
		{
			AAsdaEffectActor* Fx = *It;
			if (!IsValid(Fx)) { continue; }
			// Фильтр — и по имени .jeb (EffectName), и по имени объекта: у CDO они разные
			if (!Filter.IsEmpty() && !Fx->EffectName.Contains(Filter) && !Fx->GetName().Contains(Filter)) { continue; }
			// ★ Ф18 (МЕЛК-3): сколько записей ПОДЛЕЖАЛО уборке, знаем заранее — иначе «0 отброшено»
			//   не отличить от «отказано, потому что нет отката». Проба ничего не меняет.
			const int32 Would = bDry ? 0 : Fx->DropOldestGlowOrphans(/*bDryRun=*/true);
			const int32 N = Fx->DropOldestGlowOrphans(bDry, bForce);
			if (N > 0) { ++Touched; Dropped += N; }
			else if (Would > 0) { ++Refused; }
		}
		UE_LOG(LogAsdaFxManualLock, Warning, TEXT("[поэмиттерное свечение] %s: затронуто эффектов %d, записей %d, отказано из-за недоступного отката %d (фильтр «%s»)"),
			bDry ? TEXT("ПРОБА, ничего не изменено") : TEXT("уборка выполнена — не забудьте записать ассеты"),
			Touched, Dropped, Refused, Filter.IsEmpty() ? TEXT("все") : *Filter);
		if (Refused > 0)
		{
			UE_LOG(LogAsdaFxManualLock, Warning, TEXT("[поэмиттерное свечение] у %d эффект(ов) записи НЕ тронуты: буфер отмены недоступен, а без отката уборка безвозвратна. Повторите с согласием: asda.Fx.Glow.DropOrphans %s force"),
				Refused, Filter.IsEmpty() ? TEXT("all") : *Filter);
		}
	}));

// `asda.Fx.Glow.Set <фильтр> <поле> <значение...>` — правка ОДНОГО поля У ВСЕХ подошедших акторов
// путём панели деталей. Поля: intensity | boost | override | manual | color <r> <g> <b> | colormode <0..3>.
static FAutoConsoleCommandWithWorldAndArgs GAsdaFxGlowSet(
	TEXT("asda.Fx.Glow.Set"),
	TEXT("asda.Fx.Glow.Set <часть имени .jeb> <intensity|boost|override|manual|color|colormode> <значение...> — "
	     "правит поле категории Effect Glow РОВНО ТЕМ ЖЕ путём, что панель деталей (со всеми её побочными "
	     "эффектами: замок, автоперевод цвета, перестановка материалов)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (!W || A.Num() < 3) { UE_LOG(LogTemp, Warning, TEXT("[FxGlow] нужно: <фильтр> <поле> <значение>")); return; }
		const FString Filter = A[0], Field = A[1].ToLower();
		TArray<AActor*> Acts;
		UGameplayStatics::GetAllActorsOfClass(W, AAsdaEffectActor::StaticClass(), Acts);
		int32 N = 0;
		for (AActor* Act : Acts)
		{
			AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(Act);
			if (!Fx || (!Filter.IsEmpty() && !Fx->EffectName.Contains(Filter))) { continue; }
			if (Field == TEXT("intensity"))      { Fx->FxLight.IntensityCd = FCString::Atof(*A[2]);        AsdaFxGlowEditLeaf(Fx, TEXT("IntensityCd")); }
			else if (Field == TEXT("boost"))     { Fx->FxLight.EmissiveBoost = FCString::Atof(*A[2]);      AsdaFxGlowEditLeaf(Fx, TEXT("EmissiveBoost")); }
			else if (Field == TEXT("override"))  { Fx->FxLight.bOverrideEmissive = FCString::Atoi(*A[2]) != 0; AsdaFxGlowEditLeaf(Fx, TEXT("bOverrideEmissive")); }
			else if (Field == TEXT("manual"))    { Fx->FxLight.bManual = FCString::Atoi(*A[2]) != 0;       AsdaFxGlowEditLeaf(Fx, TEXT("bManual")); }
			else if (Field == TEXT("colormode")) { Fx->FxLight.ColorMode = (EAsdaFxLightColorMode)FCString::Atoi(*A[2]); AsdaFxGlowEditLeaf(Fx, TEXT("ColorMode")); }
			else if (Field == TEXT("color") && A.Num() >= 5)
			{
				Fx->FxLight.ColorSRGB = FColor((uint8)FCString::Atoi(*A[2]), (uint8)FCString::Atoi(*A[3]), (uint8)FCString::Atoi(*A[4]));
				AsdaFxGlowEditLeaf(Fx, TEXT("ColorSRGB"));
			}
			else { UE_LOG(LogTemp, Warning, TEXT("[FxGlow] поле «%s» не поддержано"), *Field); return; }
			++N;
			UE_LOG(LogTemp, Warning, TEXT("[FxGlow] %s (%s): %s"), *Fx->GetName(), *Fx->EffectName, *AsdaFxGlowLine(Fx));
		}
		UE_LOG(LogTemp, Warning, TEXT("[FxGlow] правка «%s» применена к %d эффект-акторам (фильтр «%s»)"), *Field, N, *Filter);
	}));

// `asda.Fx.Glow.SelfTest <фильтр>` — весь сценарий приёмки одной командой, с таймерами (пересборка
// секций происходит в Tick, поэтому «до» и «после» нельзя напечатать одним кадром).
static FAutoConsoleCommandWithWorldAndArgs GAsdaFxGlowSelfTest(
	TEXT("asda.Fx.Glow.SelfTest"),
	TEXT("asda.Fx.Glow.SelfTest <часть имени .jeb> [quit] — приёмка категории Effect Glow: замок «переопределено "
	     "вручную», автоперевод цвета, подхват яркости материала, применение «силы свечения» ПОСЛЕ пересборки секций"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (!W) { return; }
		// ★ Ревизия В1 дедупа №2 (11.10.2026): `quit` — не фильтр, а «выйти по итогу». Пакет `asda.SelfTest.Batch` добавляет
		//   `quit` каждому самотесту и по нему узнаёт конец; прежде `quit` становился фильтром (эффект «quit» не находился)
		//   и пакет стоял до потолка. Фильтр — первый аргумент, кроме `quit`.
		bool bQuit = false;   // выйти из игры после итога
		FString Filter;       // часть имени .jeb (пусто — первый попавшийся эффект)
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
			else if (Filter.IsEmpty()) { Filter = Arg; }
		}
		TWeakObjectPtr<UWorld> WR(W);
		// Конец самотеста: строка итога и (с `quit`) выход — одна точка для обоих выходов.
		auto Finish = [WR, bQuit](const FString& Line)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Line);
			if (bQuit && GEngine && WR.IsValid()) { GEngine->Exec(WR.Get(), TEXT("quit")); }
		};
		auto At = [WR](float Secs, TFunction<void()> Fn)
		{
			if (!WR.IsValid()) { return; }
			FTimerHandle H;
			WR->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([Fn]() { Fn(); }), Secs, false);
		};
		At(4.0f, [WR, Filter, Finish]()
		{
			AAsdaEffectActor* Fx = AsdaFxGlowFind(WR.Get(), Filter);
			// ★ Ревизия В1 дедупа №2 (11.10.2026): конец самотеста несёт и «ИТОГ» — маркер спеки прогона (`ИТОГ|ИТОГО`);
			//   без него успешный прогон считался таймаутом. «ЗАВЕРШЕНО» в строке оставлено.
			if (!Fx) { Finish(FString::Printf(TEXT("FX-GLOW-SELFTEST ЗАВЕРШЕНО — ИТОГ: ОТКАЗ, эффект по фильтру «%s» не найден"), *Filter)); return; }
			UE_LOG(LogTemp, Warning, TEXT("[FxGlow][1 ДО] %s (%s): %s"), *Fx->GetName(), *Fx->EffectName, *AsdaFxGlowLine(Fx));
			AsdaFxMatDumpOnce(WR.Get(), Filter, TEXT("1 ДО"));
		});
		// (2) ЗАМОК: правка листа «Сила света» обязана взвести «Переопределено вручную»
		At(4.4f, [WR, Filter]()
		{
			AAsdaEffectActor* Fx = AsdaFxGlowFind(WR.Get(), Filter);
			if (!Fx) { return; }
			Fx->FxLight.bManual = false;                        // заведомо снят перед правкой
			Fx->FxLight.IntensityCd *= 3.f;
			AsdaFxGlowEditLeaf(Fx, TEXT("IntensityCd"));
			UE_LOG(LogTemp, Warning, TEXT("[FxGlow][2 ЗАМОК] после правки «Сила света»: замок=%d (ожидание 1) | %s"),
				Fx->FxLight.bManual ? 1 : 0, *AsdaFxGlowLine(Fx));
		});
		// (3) ЦВЕТ: правка листа «Цвет (явный)» обязана сама перевести режим в «Явный»
		At(4.8f, [WR, Filter]()
		{
			AAsdaEffectActor* Fx = AsdaFxGlowFind(WR.Get(), Filter);
			if (!Fx) { return; }
			Fx->FxLight.ColorSRGB = FColor(20, 255, 40);
			AsdaFxGlowEditLeaf(Fx, TEXT("ColorSRGB"));
			UE_LOG(LogTemp, Warning, TEXT("[FxGlow][3 ЦВЕТ] после правки цвета: режим=%d (ожидание 1 = Явный) | %s"),
				(int32)Fx->FxLight.ColorMode, *AsdaFxGlowLine(Fx));
		});
		// (4) ПОДХВАТ: включение галочки «Сила свечения вручную» обязано взять число С МАТЕРИАЛА
		At(5.2f, [WR, Filter]()
		{
			AAsdaEffectActor* Fx = AsdaFxGlowFind(WR.Get(), Filter);
			if (!Fx) { return; }
			Fx->FxLight.bOverrideEmissive = true;
			AsdaFxGlowEditLeaf(Fx, TEXT("bOverrideEmissive"));
			UE_LOG(LogTemp, Warning, TEXT("[FxGlow][4 ПОДХВАТ] включена ручная сила свечения: свечение=%.3f (ожидание = табличное, не 1.000) | %s"),
				Fx->FxLight.EmissiveBoost, *AsdaFxGlowLine(Fx));
			AsdaFxMatDumpOnce(WR.Get(), Filter, TEXT("4 ПОСЛЕ ВКЛЮЧЕНИЯ"));
		});
		// (5) ПРАВКА СИЛЫ СВЕЧЕНИЯ — применяется НЕМЕДЛЕННО, без пересборки секций
		At(5.6f, [WR, Filter]()
		{
			AAsdaEffectActor* Fx = AsdaFxGlowFind(WR.Get(), Filter);
			if (!Fx) { return; }
			Fx->FxLight.EmissiveBoost = 8.f;
			AsdaFxGlowEditLeaf(Fx, TEXT("EmissiveBoost"));
			AsdaFxMatDumpOnce(WR.Get(), Filter, TEXT("5 СРАЗУ ПОСЛЕ ПРАВКИ"));
		});
		// (6) ПЕРЕСБОРКА СЕКЦИЙ — та самая, что раньше снимала динамический экземпляр материала
		At(6.4f, [WR, Filter]()
		{
			TArray<AActor*> Acts;
			UGameplayStatics::GetAllActorsOfClass(WR.Get(), AAsdaEffectActor::StaticClass(), Acts);
			int32 Cleared = 0;
			for (AActor* AA : Acts)
			{
				AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(AA);
				if (!Fx || (!Filter.IsEmpty() && !Fx->EffectName.Contains(Filter))) { continue; }
				Fx->DebugClearAllSections();
				++Cleared;
			}
			UE_LOG(LogTemp, Warning, TEXT("[FxGlow][6] СЕКЦИИ СНЕСЕНЫ у %d акторов (эмуляция цикла кулла)"), Cleared);
		});
		At(9.0f, [WR, Filter]() { AsdaFxMatDumpOnce(WR.Get(), Filter, TEXT("6 ПОСЛЕ ПЕРЕСБОРКИ")); });
		// (7) СНЯТИЕ ЗАМКА не должно взводить его обратно (иначе вернуть значения семейства нечем)
		At(9.4f, [WR, Filter]()
		{
			AAsdaEffectActor* Fx = AsdaFxGlowFind(WR.Get(), Filter);
			if (!Fx) { return; }
			Fx->FxLight.bManual = false;
			AsdaFxGlowEditLeaf(Fx, TEXT("bManual"));
			UE_LOG(LogTemp, Warning, TEXT("[FxGlow][7 СНЯТИЕ ЗАМКА] замок=%d (ожидание 0) | %s"),
				Fx->FxLight.bManual ? 1 : 0, *AsdaFxGlowLine(Fx));
		});
		// (8) ВЫКЛЮЧЕНИЕ ручной силы свечения — материал обязан вернуться к табличному экземпляру
		At(9.8f, [WR, Filter, Finish]()
		{
			AAsdaEffectActor* Fx = AsdaFxGlowFind(WR.Get(), Filter);
			// ★ Ревизия В1 дедупа №2: эффект пропал между шагами — итог всё равно печатается (иначе прогон ждёт потолка).
			if (!Fx) { Finish(TEXT("FX-GLOW-SELFTEST ЗАВЕРШЕНО — ИТОГ: ОТКАЗ, эффект пропал до шага 8")); return; }
			Fx->FxLight.bOverrideEmissive = false;
			AsdaFxGlowEditLeaf(Fx, TEXT("bOverrideEmissive"));
			AsdaFxMatDumpOnce(WR.Get(), Filter, TEXT("8 ПОСЛЕ ВЫКЛЮЧЕНИЯ"));
			// ★ Ревизия В1 дедупа №2: «ИТОГ» — маркер прогона (см. шаг 1); проверки этого самотеста — строки [FxGlow] 1–8, чисел ok/fail нет.
			Finish(TEXT("FX-GLOW-SELFTEST ЗАВЕРШЕНО — ИТОГ: шаги 1–8 пройдены (сверка — по строкам [FxGlow])"));
		});
	}));

// ═══ ★★★ ВОЛНА Ф5: ДАМП САМОЙ КАТЕГОРИИ — ТО, ЧТО ЧИТАЕТ ПАНЕЛЬ ДЕТАЛЕЙ ═══════════════════════════
//   ЗАЧЕМ. «Категория называется Effect Glow, поля идут в таком-то порядке, редкое свёрнуто» — это
//   утверждение о МЕТАДАННЫХ КЛАССА, и проверять его пересказом нельзя. Команда печатает ровно те
//   поля, которые панель деталей берёт у структуры: имя свойства, его категорию, подпись, признак
//   «свёрнуто» (CPF_AdvancedDisplay), условие показа (EditCondition) и подсказку — В ПОРЯДКЕ
//   ОБЪЯВЛЕНИЯ, то есть в том же, в каком панель их и рисует.
//   ★ Снимок окна редактора для этого не годится: `-ExecCmds` в редакторе этой сборки не исполняется
//     вовсе (замер 10.09.2026, три способа запуска), а `-ExecutePythonScript` выходит через 4 кадра —
//     Slate не успевает нарисовать панель. Метаданные же читаются ОТТУДА ЖЕ, откуда их берёт панель.
//   Запуск: `asda.Fx.Glow.Layout`
static FAutoConsoleCommand GAsdaFxGlowLayout(
	TEXT("asda.Fx.Glow.Layout"),
	TEXT("asda.Fx.Glow.Layout — напечатать состав категории Effect Glow так, как её видит панель деталей: "
	     "порядок полей, категория, подпись, «свёрнуто», условие показа, подсказка"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		const UStruct* S = FAsdaFxLightSpec::StaticStruct();
		FProperty* Owner = FindFProperty<FProperty>(AAsdaEffectActor::StaticClass(), TEXT("FxLight"));
		UE_LOG(LogTemp, Warning, TEXT("[Layout] свойство-владелец AAsdaEffectActor::FxLight: категория=«%s» ShowOnlyInnerProperties=%d"),
			Owner ? *Owner->GetMetaData(TEXT("Category")) : TEXT("?"),
			(Owner && Owner->HasMetaData(TEXT("ShowOnlyInnerProperties"))) ? 1 : 0);
		int32 N = 0, NAdv = 0;
		for (TFieldIterator<FProperty> It(S); It; ++It)
		{
			FProperty* P = *It;
			const bool bAdv = P->HasAnyPropertyFlags(CPF_AdvancedDisplay);
			NAdv += bAdv ? 1 : 0;
			UE_LOG(LogTemp, Warning,
				TEXT("[Layout] %2d | %-22s | категория=«%s» | подпись=«%s» | свёрнуто=%d | показ=«%s» | подсказка=«%s»"),
				++N, *P->GetName(), *P->GetMetaData(TEXT("Category")), *P->GetMetaData(TEXT("DisplayName")),
				bAdv ? 1 : 0, *P->GetMetaData(TEXT("EditCondition")), *P->GetMetaData(TEXT("ToolTip")));
		}
		UE_LOG(LogTemp, Warning, TEXT("[Layout] полей всего %d, из них свёрнуто под «Advanced» %d"), N, NAdv);
		UE_LOG(LogTemp, Warning, TEXT("FX-GLOW-LAYOUT ЗАВЕРШЕНО"));
	}));
#endif // WITH_EDITOR
