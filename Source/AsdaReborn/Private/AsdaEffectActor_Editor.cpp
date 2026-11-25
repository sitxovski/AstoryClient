// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectActor_Editor.cpp — ★ R2 10.11.2026 (разрез AsdaEffectActor.cpp, дословный перенос): WITH_EDITOR: шлюз записи свечения, буфер обмена, PostEditChangeProperty/Undo.
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

#if WITH_EDITOR
// ★ ГЕЙТ ЗАМКА «переопределено вручную» (волна Ф2-ВСЕ 09.09.2026). 1 = НЕ взводить bManual ни на какую
//   правку полей «Свет эффекта». Поднимает его на время работы генератор дефолтов
//   Tools/asda_apply_fx_light_defaults.py: его собственная запись структуры в CDO приходит сюда как
//   обычная правка, и без гейта генератор запирал бы от себя же все 1274 ассета с первого прогона.
//   Владелец этот ключ не трогает: у него он всегда 0, и любая правка поля в панели взводит замок.
static int32 GAsdaFxLightNoManualLock = 0;
static FAutoConsoleVariableRef CVarAsdaFxLightNoManualLock(
	TEXT("asda.Fx.Light.NoManualLock"),
	GAsdaFxLightNoManualLock,
	TEXT("1 = правки полей «Свет эффекта» НЕ взводят замок «переопределено вручную» (для генератора дефолтов); 0 = обычное поведение."),
	ECVF_Default);

// ★ Ф13: объявление категории LogAsdaFxManualLock переехало ВЫШЕ, за пределы `#if WITH_EDITOR`
//   (в неё пишет и общий код — см. SaveEmitterGlow).
// ★ ВОЛНА Ф5: автоперевод «Цвет: откуда брать» в «Явный цвет» при ручной правке поля цвета.
//   Ключ отката на случай, если владельцу это помешает; поведение по умолчанию — включено,
//   потому что БЕЗ него поле цвета в дефолтном режиме не делает ровно ничего (см. ниже).
static int32 GAsdaFxGlowAutoFixedColor = 1;
static FAutoConsoleVariableRef CVarAsdaFxGlowAutoFixedColor(
	TEXT("asda.Fx.Glow.AutoFixedColor"),
	GAsdaFxGlowAutoFixedColor,
	TEXT("1 (умолчание) = правка поля «Цвет (явный)» в панели деталей сама переводит «Цвет: откуда брать» в режим «Явный цвет»; 0 = не переводить."),
	ECVF_Default);

// ═══ ★★★ ВОЛНА Ф15 (К-1, 20.09.2026): КТО ИМЕННО ПИШЕТ В `Emitters` ═══════════════════════════════
//   ЧТО БЫЛО НЕ ТАК. Волна Ф14 различала панель деталей и сборщик признаком `Leaf != Member`, где
//   Leaf/Member — ИМЕНА (FName) свойств события. Ревизия доказала два контрпримера, и оба ведут к
//   потере работы владельца:
//   (а) ЛЮБАЯ операция панели над САМИМ массивом — «+», вставка, удаление, дублирование, очистка,
//       «Reset to Default» на массиве, вставка текста — присылает Property == MemberProperty ==
//       `Emitters` (PropertyHandleImpl.cpp:1438 ArrayAdd, :1722 вставка, :1911 ArrayRemove,
//       :1621 ArrayClear, :2403 Duplicate, :666 ImportText; цепочка из одного звена собирается в
//       PropertyNode.cpp:3402-3419). То есть `Leaf == Member` ⇒ «сборщик» — и это НЕВЕРНО.
//   (б) «Reset to Default» или вставка на ОДНОМ элементе массива: узел элемента несёт
//       ArrayProperty->Inner, а UHT называет Inner ТЕМ ЖЕ именем, что и сам массив
//       (AsdaEffectActor.gen.cpp:1742 `NewProp_Emitters_Inner = { "Emitters", … }` против :1743
//       `NewProp_Emitters = { "Emitters", … }`). Сравнение по FName снова даёт `Leaf == Member`.
//   Цена: ПКМ по эмиттеру → «Reset to Default» (естественный жест «убрать мою настройку») уходил в
//   ветку сборщика — RestoreEmitterGlow тут же возвращал отменённые числа, а SaveEmitterGlow(false)
//   оставлял запись в EmitterGlowSaved, и она уезжала в .uasset.
//
//   ЛЕЧЕНИЕ — ТРИ ПРИЗНАКА, ОТ СТРОГОГО К ОБЩЕМУ; ни один не опирается на ИМЕНА свойств.
//   (1) ЯВНЫЙ ГЕЙТ `asda.Fx.Glow.EmittersWriter`. Единственный признак, не зависящий от формы
//       события вовсе, — ровно как замок `asda.Fx.Light.NoManualLock` выше по файлу. −1 = решать
//       самим (умолчание), 0 = «пишет панель», 1 = «пишет скрипт». Сборщику (Tools/asda_fx_common.py
//       ::finish_effect_bp, Tools/asda_place_effects.py) достаточно поднять ключ на время прогона;
//       НА СЕГОДНЯ ОН ЕГО НЕ ПОДНИМАЕТ (файлы Tools/ этой волной не правились), и работает п. 2-3.
//   (2) АДРЕС СВОЙСТВА, А НЕ ЕГО ИМЯ. `Event.Property != Event.MemberProperty` сравнивается
//       УКАЗАТЕЛЯМИ: у элемента массива Property — это ArrayProperty->Inner, ОТДЕЛЬНЫЙ объект
//       FProperty с тем же именем. Так закрывается случай (б) и все правки листов структуры.
//   (3) ВИД ПРАВКИ `Event.ChangeType`. Панель деталей НИКОГДА не присылает `Unspecified`:
//       операции над массивом несут ArrayAdd/ArrayRemove/ArrayClear/Duplicate/ArrayMove
//       (PropertyHandleImpl.cpp, строки выше), а запись значения и «Reset to Default» — ValueSet
//       либо ValueSet|ResetToDefault (PropertyHandleImpl.cpp:372-376, флаг ставится из
//       FPropertyValueImpl::ResetToDefault :1162). Скрипт же из Python (`set_editor_property`)
//       ChangeType не задаёт вовсе, и тот остаётся умолчанием структуры — `Unspecified`
//       (PropertyAccessUtil.h:67; PyWrapperOwnerContext.cpp:55-80 заполняет только цепочку и режим
//       уведомления; событие собирается в PropertyAccessUtil.cpp:780-795). Перечень видов правки —
//       UnrealType.h:6944-6970.
//   Итог: «скрипт» = гейт поднят ЛИБО (адреса свойств совпали И вид правки = Unspecified).
//   Всё остальное — рука владельца. Сомнительных форм не осталось: любая неизвестная форма с
//   несовпавшими адресами или с непустым видом правки считается РУКОЙ (авторитетная запись), а
//   неизвестная форма «массив целиком + Unspecified» — скриптом (сторож копии на месте).
static int32 GAsdaFxGlowEmittersWriter = -1;

// ═══ ★★★ ВОЛНА Ф19 (С-1, 20.09.2026): «ЭПОХА ГЕЙТА» — СЧЁТЧИК ОПУСКАНИЙ КЛЮЧА ═══════════════════
//   Разбор целиком — у объявления GlowWriterGateSuspectEpoch в AsdaEffectActor.h. Коротко: признак
//   «этому эффекту уже сказали о залипшем гейте» обязан сниматься в тот миг, когда КЛЮЧ уходит
//   из 1, а не когда владелец в очередной раз заглянет в панель именно этого эффекта. Счётчик
//   увеличивается на каждое такое опускание; актор хранит номер эпохи, в которой предупреждал.
//   Нуль как стартовое значение безопасен: поле актора стартует с INDEX_NONE (-1), поэтому
//   ПЕРВОЕ предупреждение в нулевой эпохе всё равно выдаётся.
//   Константная инициализация int32 — значит счётчик готов до любых статических конструкторов,
//   в том числе до конструктора переменной ниже, который ставит на него колбэк.
static int32 GAsdaFxGlowWriterGateEpoch = 0;

// Колбэк смены значения ключа `asda.Fx.Glow.EmittersWriter`. Зовётся движком на КАЖДОЙ принятой
//   установке (ConsoleManager.cpp, FConsoleVariableBase::OnChanged из Set) — в том числе из
//   консольной команды, которой ключ поднимает и опускает сборщик (Tools/asda_fx_common.py
//   ::emitters_writer_gate). К этому моменту ссылочная переменная уже обновлена, поэтому значение
//   читается и из неё, и из Var — берём Var, чтобы не зависеть от порядка записи.
static void AsdaGlowOnEmittersWriterGateChanged(IConsoleVariable* Var)
{
	// Эпоха меняется ТОЛЬКО на уходе из 1: подъём в 1 (в том числе вложенный, сборщик внутри
	//   раскладчика) — это продолжение того же залипания, а не новое событие.
	if (Var && Var->GetInt() != 1)
	{
		++GAsdaFxGlowWriterGateEpoch;
	}
}

// ═══ ★★★ Ф20 (ЗАМЕЧАНИЕ РЕВИЗИИ НА БУДУЩЕЕ): ЭТОТ КЛЮЧ ПОДНИМАЕТСЯ И ОПУСКАЕТСЯ ТОЛЬКО ═════════
//   КОНСОЛЬНОЙ КОМАНДОЙ — ОДНИМ И ТЕМ ЖЕ ПРИОРИТЕТОМ. СМЕШИВАТЬ ИСТОЧНИКИ НЕЛЬЗЯ.
//   У консольной переменной движок хранит не только значение, но и ПРИОРИТЕТ последней установки
//   (EConsoleVariableFlags, ECVF_SetBy* в биты маски ECVF_SetByMask). Установка, чей приоритет
//   НИЖЕ уже записанного, ОТБРАСЫВАЕТСЯ: FConsoleVariableBase::CanChange (ConsoleManager.cpp:
//   274-312) возвращает `NewPri >= OldPri`, и при false запись не делается, а колбэк OnChanged
//   НЕ ЗОВЁТСЯ ВОВСЕ. ВЫЗЫВАЮЩИЙ ОБ ЭТОМ НЕ УЗНАЁТ: IConsoleVariable::Set ничего не возвращает,
//   а единственный след — строка Warning в LogConsoleManager (там же, :304-308), которую никто
//   не читает. Последствия для нас прямые и оба тихие:
//     (1) ключ, поднятый командой из консоли (ECVF_SetByConsole), НЕ опустится вызовом из кода
//         (ECVF_SetByCode — приоритет ниже): залипание останется, а Tools/asda_fx_common.py
//         ::emitters_writer_gate будет уверен, что вернул прежнее значение;
//     (2) эпоха гейта (счётчик выше) не сдвинется, потому что колбэк не позвали, — то есть и
//         одноразовость предупреждения не снимется.
//   ПРАВИЛО. Все установки этого ключа идут ОДНИМ приоритетом ECVF_SetByConsole: сборщик шлёт
//   консольную команду, самопроверка плагина зовёт Set(..., ECVF_SetByConsole). Никакого
//   SetByCode/SetByScalability/SetByProjectSetting здесь быть не должно. Если когда-нибудь
//   понадобится поднимать ключ из C++, поднимать его надо ТЕМ ЖЕ ECVF_SetByConsole.
static FAutoConsoleVariableRef CVarAsdaFxGlowEmittersWriter(
	TEXT("asda.Fx.Glow.EmittersWriter"),
	GAsdaFxGlowEmittersWriter,
	TEXT("Кто пишет массив «Эмиттеры»: -1 (умолчание) = определять по событию правки; 0 = панель деталей (рука владельца, запись авторитетна); 1 = скрипт-сборщик (перепечка из .jeb, настройки восстанавливаются из долговечной копии). ВНИМАНИЕ: поднимать и опускать ТОЛЬКО консолью (один приоритет ECVF_SetByConsole) — установка меньшим приоритетом отбрасывается молча и без колбэка."),
	FConsoleVariableDelegate::CreateStatic(&AsdaGlowOnEmittersWriterGateChanged),
	ECVF_Default);

// ═══ ★★★ ВОЛНА Ф20 (С-1, 20.09.2026): ИНТЕРВАЛ ПОВТОРА ПРЕДУПРЕЖДЕНИЯ О ЗАЛИПШЕМ ГЕЙТЕ ═════════
//   Разбор целиком — у объявления GlowWriterGateSuspectTimeSec в AsdaEffectActor.h. Коротко:
//   одного сообщения на эпоху мало — всплывающее окно живёт 15 секунд, и пропустивший его
//   владелец дальше теряет правки молча. Через этот интервал предупреждение обязано вернуться.
//   УМОЛЧАНИЕ 25 с — середина предложенного ревизией окна 20–30 с: дольше жизни всплывающего
//   сообщения (15 с), то есть пропущенное окно успевает смениться новым, и много больше
//   длительности потока событий от ползунка (миллисекунды), то есть поток по-прежнему гасится.
//   Ключ заведён ОТДЕЛЬНО от гейта и нужен прежде всего самопроверке: она ставит заведомо
//   короткий интервал и доказывает возврат сообщения настоящим ожиданием, а не подтасовкой поля.
//   Значение ≤ 0 отключает повтор по времени (остаётся только смена эпохи) — путь отступления
//   владельцу, если сообщения покажутся навязчивыми.
static float GAsdaFxGlowGateWarnRepeatSec = 25.f;
static FAutoConsoleVariableRef CVarAsdaFxGlowGateWarnRepeatSec(
	TEXT("asda.Fx.Glow.GateWarnRepeatSec"),
	GAsdaFxGlowGateWarnRepeatSec,
	TEXT("Через сколько секунд повторить предупреждение о залипшем ключе asda.Fx.Glow.EmittersWriter, если правки того же эффекта продолжают молча откатываться. Умолчание 25 (всплывающее сообщение живёт 15 с). 0 и меньше — повторять только при смене значения гейта."),
	ECVF_Default);

// ★ Ф19 (С-1): текущая эпоха гейта. Обоснование — у объявления в AsdaEffectActor.h.
int32 AAsdaEffectActor::GetGlowWriterGateEpoch()
{
	return GAsdaFxGlowWriterGateEpoch;
}

// ★ Ф20 (С-1): нынешний интервал повтора предупреждения. Обоснование — у объявления в заголовке.
float AAsdaEffectActor::GetGlowWriterGateWarnRepeatSec()
{
	return GAsdaFxGlowGateWarnRepeatSec;
}

// ═══ ★★★ ВОЛНА Ф16 (СРЕДН-1, 20.09.2026): ЖЕСТ РЕДАКТОРА, КОТОРЫЙ ШЛЁТ РОВНО ФОРМУ СКРИПТА ══════
//   ЧТО НАШЛОСЬ. Ф15 закрыла все жесты панели деталей, но мимо панели есть ещё один путь, и он
//   строит событие ОДНОАРГУМЕНТНЫМ конструктором:
//     Editor.cpp:1771-1774  `if (Options.Flags & ECopyOptions::CallPostEditChangeProperty)
//                            { FPropertyChangedEvent PropertyChangedEvent(Property);
//                              TargetActor->PostEditChangeProperty(PropertyChangedEvent); }`
//   Одноаргументный конструктор кладёт MemberProperty = Property и оставляет вид правки
//   `Unspecified` — то есть ОБА признака Ф15 (совпавшие адреса + Unspecified) дают «скрипт».
//   КТО ЭТОТ ПУТЬ ЗАПУСКАЕТ. Флаг `ECopyOptions::CallPostEditChangeProperty` во всём движке ставит
//   ровно ОДНО место — «Keep Simulation Changes» (LevelEditorActions.cpp:2750-2754, сплошной поиск
//   по Engine/Source). Прочие вызовы `EditorUtilities::CopyActorProperties` его НЕ ставят
//   (Kismet2.cpp:1555 и :2212, SSubobjectInstanceEditor.cpp:625/707/764) и потому безопасны;
//   Editor.cpp:1928/1972 — та же ветка для КОМПОНЕНТОВ, наш актор туда не попадает.
//   ЦЕНА ОШИБКИ. Владелец крутит эффект в Simulate и жмёт K («сохранить состояние»): массив
//   `Emitters` копируется из симулируемого актора в редакторский, мы считаем это перепечаткой
//   сборщика и НЕМЕДЛЕННО раздаём поверх старые числа из долговечной копии — сохранённое
//   состояние теряется в тот же миг.
//   ПРИЗНАК, КОТОРЫЙ ЭТО ЗАКРЫВАЕТ. Условие входа в «Keep Simulation Changes» — работающая
//   симуляция: `GEditor->GetSelectedActorCount() > 0 && GEditor->PlayWorld != NULL`
//   (LevelEditorActions.cpp:2726). Значит, `GEditor->PlayWorld != nullptr` — достаточный и
//   дешёвый разделитель: сборщики эффектов гоняются коммандлетом либо в редакторе БЕЗ PIE, и
//   поднимают явный гейт (п. 1), который проверяется РАНЬШЕ этого признака.
static bool AsdaGlowIsSimulationRunning()
{
	return GEditor && GEditor->PlayWorld != nullptr;
}

bool AAsdaEffectActor::IsEmittersWriteFromDetailsPanel(const FPropertyChangedEvent& Event)
{
	// (1) Явный гейт — сильнее любых догадок о форме события. С волны Ф16 его поднимают ВСЕ
	//     сборщики, пишущие массив эмиттеров (asda_fx_common.emitters_writer_gate).
	if (GAsdaFxGlowEmittersWriter == 0) { return true; }
	if (GAsdaFxGlowEmittersWriter == 1) { return false; }
	// (2) Адреса свойств: у листа структуры Property — другой объект, а у ЭЛЕМЕНТА массива движок
	//     кладёт Property = nullptr вовсе (UObject::PostEditChangeChainProperty, Obj.cpp:613-614:
	//     `PropertyChain.GetActiveNode()` пуст, потому что узел элемента в цепочку не попадает —
	//     PropertyNode.cpp:3734-3737 пропускает узлы с ArrayIndex != INDEX_NONE и Inner). И то и
	//     другое ≠ MemberProperty, так что сравнение УКАЗАТЕЛЯМИ закрывает оба случая.
	if (Event.Property != Event.MemberProperty) { return true; }
	// (3) Вид правки: `Unspecified` шлёт только скрипт, панель — никогда
	if (Event.ChangeType != EPropertyChangeType::Unspecified) { return true; }
	// (4) ★ Ф16 (СРЕДН-1): «Keep Simulation Changes» — единственный жест редактора, дающий ровно
	//     форму скрипта. Он возможен ТОЛЬКО при работающей симуляции; разбор — в шапке выше.
	if (AsdaGlowIsSimulationRunning()) { return true; }
	// ═══ ★★★ Ф16: ПОЧЕМУ УМОЛЧАНИЕ ОСТАВШЕЙСЯ ФОРМЫ — «СКРИПТ», А НЕ «ПАНЕЛЬ» ═════════════════════
	//   Ревизия предлагала считать рукой владельца ЛЮБУЮ нераспознанную форму — раз сборщики
	//   теперь поднимают гейт сами. Разобрано по ЦЕНЕ ОШИБКИ, и она НЕСИММЕТРИЧНА:
	//     * ошибка «скрипт назван панелью» ⇒ запись авторитетна ⇒ SaveEmitterGlow(true)
	//       пересобирает EmitterGlowSaved по СВЕЖЕРАСПЕЧАТАННЫМ эмиттерам, у которых свечение
	//       дефолтное, — записи настроек владельца выбрасываются, и `save_loaded_asset` сборщика
	//       тут же кладёт это на диск. ОТКАТА НЕТ;
	//     * ошибка «панель названа скриптом» ⇒ RestoreEmitterGlow возвращает в массив ПРЕЖНИЕ
	//       числа владельца, а копия остаётся нетронутой (запись неавторитетна, сторож на месте).
	//       Владелец видит, что его правка «не взялась», и повторяет её. НИЧЕГО НЕ ПОТЕРЯНО.
	//   Пока цена ошибок такова, умолчание обязано быть тем, которое данных не теряет. Явный
	//   гейт `asda.Fx.Glow.EmittersWriter 0` даёт владельцу переключить это одним словом.
	return false;
}

// ★ 27.10.2026: «Копировать/Вставить свет» (разбор — в заголовке у объявлений). Формат буфера — строки:
//   ASDA_FX_GLOW_V1 / LIGHT\t<FxLight целиком, ExportText> / EM\t<номер>\t<имя эмиттера>\t<Glow, ExportText>.
static const TCHAR* const kAsdaGlowClipHeader = TEXT("ASDA_FX_GLOW_V1");

void AAsdaEffectActor::CopyGlowToClipboard()
{
	FString Out = FString(kAsdaGlowClipHeader) + TEXT("\n");
	FString Light;
	FAsdaFxLightSpec::StaticStruct()->ExportText(Light, &FxLight, nullptr, this, PPF_None, nullptr);
	Out += FString::Printf(TEXT("LIGHT\t%s\n"), *Light);
	for (int32 i = 0; i < Emitters.Num(); ++i)
	{
		FString Glow;
		FAsdaFxEmitterGlow::StaticStruct()->ExportText(Glow, &Emitters[i].Glow, nullptr, this, PPF_None, nullptr);
		Out += FString::Printf(TEXT("EM\t%d\t%s\t%s\n"), i, *Emitters[i].Name, *Glow);
	}
	FPlatformApplicationMisc::ClipboardCopy(*Out);
	UE_LOG(LogAsdaFxManualLock, Log, TEXT("[копировать свет] %s: свет и свечение %d эмиттеров в буфере обмена"), *GetName(), Emitters.Num());
}

void AAsdaEffectActor::PasteGlowFromClipboard()
{
	FString Clip;
	FPlatformApplicationMisc::ClipboardPaste(Clip);
	TArray<FString> Lines;
	Clip.ParseIntoArrayLines(Lines);
	if (Lines.Num() < 2 || Lines[0].TrimStartAndEnd() != kAsdaGlowClipHeader)
	{
		UE_LOG(LogAsdaFxManualLock, Warning, TEXT("[вставить свет] %s: в буфере нет света эффекта — сначала «Копировать свет»"), *GetName());
		return;
	}
	// Разбор до правки: при битом буфере актор не трогаем вовсе.
	FAsdaFxLightSpec NewLight = FxLight;
	bool bLight = false;
	struct FEmGlow { int32 Index; FString Name; FAsdaFxEmitterGlow Glow; };
	TArray<FEmGlow> Src;
	for (const FString& L : Lines)
	{
		TArray<FString> F;
		L.ParseIntoArray(F, TEXT("\t"), /*InCullEmpty*/ false);
		if (F.Num() == 2 && F[0] == TEXT("LIGHT"))
		{
			bLight = FAsdaFxLightSpec::StaticStruct()->ImportText(*F[1], &NewLight, this, PPF_None, GWarn, FAsdaFxLightSpec::StaticStruct()->GetName()) != nullptr;
		}
		else if (F.Num() == 4 && F[0] == TEXT("EM"))
		{
			FEmGlow E{ FCString::Atoi(*F[1]), F[2], FAsdaFxEmitterGlow() };
			if (FAsdaFxEmitterGlow::StaticStruct()->ImportText(*F[3], &E.Glow, this, PPF_None, GWarn, FAsdaFxEmitterGlow::StaticStruct()->GetName()))
			{
				Src.Add(MoveTemp(E));
			}
		}
	}
	if (!bLight)
	{
		UE_LOG(LogAsdaFxManualLock, Warning, TEXT("[вставить свет] %s: блок света в буфере не разобран — ничего не изменено"), *GetName());
		return;
	}

	const FScopedTransaction Tx(NSLOCTEXT("AsdaFx", "PasteGlow", "Вставить свет эффекта"));
	Modify();
	FxLight = NewLight;
	FxLight.bManual = true;   // вставка = ручная настройка: генератор дефолтов такой эффект больше не трогает

	// Свечение эмиттеров: по имени (имена у копий одного .jeb совпадают); имени нет — по номеру при равном числе.
	int32 Applied = 0;
	TSet<int32> Used;
	for (const FEmGlow& E : Src)
	{
		int32 Dst = INDEX_NONE;
		for (int32 i = 0; i < Emitters.Num(); ++i)
		{
			if (!Used.Contains(i) && Emitters[i].Name == E.Name) { Dst = i; break; }
		}
		if (Dst == INDEX_NONE && Src.Num() == Emitters.Num() && Emitters.IsValidIndex(E.Index) && !Used.Contains(E.Index))
		{
			Dst = E.Index;
		}
		if (Dst != INDEX_NONE)
		{
			Emitters[Dst].Glow = E.Glow;
			Used.Add(Dst);
			++Applied;
		}
	}
	SaveEmitterGlow(/*bAuthoritative*/ true);   // долговечная копия свечения — как после правки рукой в панели
	PostEditChange();
	MarkPackageDirty();
	UE_LOG(LogAsdaFxManualLock, Log, TEXT("[вставить свет] %s: свет заменён, свечение эмиттеров %d из %d (в буфере %d)"),
		*GetName(), Applied, Emitters.Num(), Src.Num());
}

void AAsdaEffectActor::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	// ★ ЗАМОК «переопределено вручную» (волна Ф2-ВСЕ 09.09.2026). Дефолты света проставляются в 1274 ассета
	//   BP_Fx_* СКРИПТОМ, и прогонять его придётся ещё не раз (правка таблицы семейств, новые эффекты).
	//   Чтобы поштучная настройка владельца не была затёрта следующим прогоном, любая ЕГО правка полей
	//   FxLight взводит FxLight.bManual — генератор такие эффекты пропускает. Само поле bManual из правила
	//   исключено: снятие замка не должно тут же взводить его обратно.
	//   Определяем «правку внутри FxLight» по ЦЕПОЧКЕ свойств (MemberProperty = FxLight, Property = поле
	//   структуры): у полей USTRUCT одинаковые имена встречаются и в других структурах актора.
	//   ★ И ТОЛЬКО НА ПРАВКУ ОТДЕЛЬНОГО ПОЛЯ, а не структуры целиком (Property != MemberProperty).
	//   Владелец в панели деталей всегда правит ЛИСТ (Property = IntensityCd, MemberProperty = FxLight).
	//   Присвоение структуры ЦЕЛИКОМ делает только скрипт простановки дефолтов.
	//   ★★★ И ВСЁ РАВНО ЭТОГО МАЛО. Замер 09.09.2026: прогон генератора по 1274 ассетам взвёл bManual
	//   во ВСЕХ, хотя писал структуру целиком. Поэтому поверх — ЯВНЫЙ ГЕЙТ `asda.Fx.Light.NoManualLock 1`,
	//   который генератор поднимает на время своей работы и опускает после (закон «доказывать вторым
	//   методом»: догадка о форме FPropertyChangedEvent — не доказательство, а гейт работает при любой её форме).
	const FName Member = Event.MemberProperty ? Event.MemberProperty->GetFName() : NAME_None;
	const FName Leaf = Event.Property ? Event.Property->GetFName() : NAME_None;
	if (Member == GET_MEMBER_NAME_CHECKED(AAsdaEffectActor, FxLight))
	{
		// Диагностика формы события (по умолчанию молчит; `log LogAsdaFxManualLock Verbose` включает)
		UE_LOG(LogAsdaFxManualLock, Verbose, TEXT("[замок] %s: Member=%s Leaf=%s тип=%d гейт=%d"),
			*GetName(), *Member.ToString(), *Leaf.ToString(), (int32)Event.ChangeType, GAsdaFxLightNoManualLock);
		if (GAsdaFxLightNoManualLock == 0
			&& Leaf != Member
			&& Leaf != GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, bManual))
		{
			FxLight.bManual = true;
		}

		// ═══ ★★★ ВОЛНА Ф5 (10.09.2026): ТРИ УДОБСТВА ПАНЕЛИ ДЕТАЛЕЙ ═══════════════════════════════
		// (1) АВТОПЕРЕВОД ЦВЕТА В РЕЖИМ «ЯВНЫЙ». Дефолтный режим цвета — «Средний по всем эмиттерам»,
		//     и правка поля «Цвет (явный)» в нём НЕ ДАЁТ НИЧЕГО: владелец крутит цвет, свет не меняется,
		//     поле выглядит сломанным. Держать поле серым под EditCondition — не выход: тогда до него
		//     вообще не добраться, не переключив сперва режим. Поэтому правка цвета САМА включает режим.
		//     Срабатывает ТОЛЬКО на правку ЛИСТА (Leaf == ColorSRGB): скрипт простановки дефолтов пишет
		//     структуру ЦЕЛИКОМ (Leaf == Member) и сюда не попадает — режимы семейств он не ломает.
		if (Leaf == GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, ColorSRGB)
			&& GAsdaFxGlowAutoFixedColor != 0
			&& FxLight.ColorMode != EAsdaFxLightColorMode::Fixed)
		{
			FxLight.ColorMode = EAsdaFxLightColorMode::Fixed;
		}
		// (2) ПОДХВАТ ТЕКУЩЕЙ ЭМИССИИ ПРИ ВКЛЮЧЕНИИ ГАЛОЧКИ «Сила свечения: задать вручную».
		//     Без него включение галочки роняло бы яркость эффекта с табличной (до 5.0) на дефолт поля
		//     1.0 — эффект зримо ТУСКНЕЕТ в момент, когда владелец только собрался его настраивать.
		//     Читаем ФАКТИЧЕСКОЕ значение с материала первой секции (не из таблицы: авторитет —
		//     то, чем реально рисуется), и только если параметр там ЕСТЬ (у BLEND_MODULATE его нет).
		if (Leaf == GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, bOverrideEmissive) && FxLight.bOverrideEmissive)
		{
			//   ★ 10.10.2026 (ревизия fx_blend_fix, МЕЛК-1): у контейнеров «mod» в Material стоит Modulate-половина без
			//   параметра, а буст (`_fxb…`) живёт в аддитивной половине MaterialB — её читаем, если в Material его нет.
			bool bPicked = false;   // подхвачено значение с материала эмиттера
			for (const FAsdaFxEmitter& Em : Emitters)
			{
				for (UMaterialInterface* Mi : { Em.Material.Get(), Em.MaterialB.Get() })
				{
					float Cur = 0.f;   // значение EmissiveBoost секции
					if (Mi && Mi->GetScalarParameterValue(FMaterialParameterInfo(AsdaMat::kParamEmissiveBoost), Cur))   // ★ В1-C F10
					{
						FxLight.EmissiveBoost = Cur;
						bPicked = true;
						break;
					}
				}
				if (bPicked) { break; }
			}
		}
		// (3) ПРИМЕНИТЬ НЕМЕДЛЕННО. Секции никто не пересобирал — донести значение до уже стоящих
		//     на них материалов. Пересборка (смена топологии, возврат из-за отсечки) донесёт его сама.
		RefreshGlowMaterials();
	}

	// ═══ ★★★ ВОЛНА Ф8 (10.09.2026): ПОЭМИТТЕРНОЕ СВЕЧЕНИЕ ═══════════════════════════════════════
	//   Правка ЛЮБОГО поля внутри массива «Эмиттеры» (в том числе группы «Свечение эмиттера»)
	//   приходит сюда с MemberProperty = Emitters. Делаем два дела:
	//     (1) ПОДХВАТ ЧИСЕЛ ЭФФЕКТА при переключении эмиттера в «задано вручную». Без него
	//         переключатель ронял бы свет эмиттера на C++-заглушку (7.5 кд, точечный) — эффект
	//         зримо меняется в тот самый момент, когда владелец только собрался его настраивать.
	//         Тот же приём, что у галочки «Сила свечения: задать вручную» (п. 2 волны Ф5).
	//         Признак `bSpecSeeded` держит подхват ОДНОРАЗОВЫМ: иначе правка любого другого поля
	//         эмиттера возвращала бы числа эффекта поверх уже настроенных.
	//     (2) СНИМОК В ДОЛГОВЕЧНУЮ КОПИЮ — она и переживает перепечку из .jeb (см. EmitterGlowSaved).
	if (Member == GET_MEMBER_NAME_CHECKED(AAsdaEffectActor, Emitters))
	{
		// ★★★ Ф15 (К-1): ПРИЗНАК «ПИШЕТ ПАНЕЛЬ» — ОДИН НА ОБА РЕШЕНИЯ (восстанавливать ли копию и
		//   авторитетна ли запись). Разбор и доказательства — у IsEmittersWriteFromDetailsPanel выше.
		const bool bFromPanel = IsEmittersWriteFromDetailsPanel(Event);
		UE_LOG(LogAsdaFxManualLock, Verbose, TEXT("[поэмиттерное свечение] %s: запись в «Эмиттеры» — %s (Property=%p MemberProperty=%p вид=0x%X ключ=%d симуляция=%d)"),
			*GetName(), bFromPanel ? TEXT("ПАНЕЛЬ ДЕТАЛЕЙ (рука владельца)") : TEXT("СКРИПТ (перепечка)"),
			(const void*)Event.Property, (const void*)Event.MemberProperty, (uint32)Event.ChangeType,
			GAsdaFxGlowEmittersWriter, AsdaGlowIsSimulationRunning() ? 1 : 0);
		// ★ Ф16 (СРЕДН-1): «скрипт» БЕЗ поднятого гейта — форма, которую мы распознали догадкой.
		//   С волны Ф16 все наши сборщики гейт поднимают, поэтому такая запись означает либо чужой
		//   скрипт, либо неизвестный жест редактора. Молчать об этом нельзя: именно здесь
		//   выясняется, что список «кто пишет эмиттеры» неполон.
		if (!bFromPanel && GAsdaFxGlowEmittersWriter < 0)
		{
			UE_LOG(LogAsdaFxManualLock, Warning, TEXT("[поэмиттерное свечение] %s: массив «Эмиттеры» записан ЦЕЛИКОМ без поднятого ключа %s — считаю перепечаткой скрипта (копия настроек сохранена и роздана обратно). Если это была правка в редакторе, поднимите asda.Fx.Glow.EmittersWriter 0 на время работы."),
				*GetName(), TEXT("asda.Fx.Glow.EmittersWriter"));
		}
		// ═══ ★★★ ВОЛНА Ф17 (С-2): ПОДНЯТЫЙ ГЕЙТ ПОВЕРХ ЧЕЛОВЕЧЕСКОЙ ПРАВКИ ═══════════════════════
		//   Разбор целиком — у объявления OnEmittersWriterGateSuspect в AsdaEffectActor.h. Коротко:
		//   ключ 1 закорачивает IsEmittersWriteFromDetailsPanel в «скрипт» ПЕРВОЙ же строкой, и
		//   правка владельца молча откатывается RestoreEmitterGlow. Форма события при этом
		//   по-прежнему человеческая: правился ЛИСТ структуры или элемент массива
		//   (Property != MemberProperty) либо вид правки не Unspecified (скрипт шлёт только его).
		//   Это ЕДИНСТВЕННЫЙ наблюдаемый симптом залипшего ключа — молчать о нём нельзя.
		//   Номер `GlowWriterGateSuspectEpoch` держит сообщение ОДНОРАЗОВЫМ на актор за ЭПОХУ
		//   ГЕЙТА: перетаскивание ползунка шлёт десятки таких событий в секунду.
		// ★★★ Ф18 (С-1): СБРОС ПРИЗНАКА — ОДНОРАЗОВОСТЬ ДЕЙСТВУЕТ, ПОКА ГЕЙТ ПОДНЯТ, А НЕ ВЕСЬ
		//   СЕАНС. Прежде сброса в продукте не было вовсе, и ВТОРОЕ залипание гейта в том же
		//   сеансе на том же эффекте проходило беззвучно (уходило в Verbose ниже).
		// ★★★ Ф19 (С-1): СБРОС ЦЕПЛЯЕТСЯ ЗА КЛЮЧ, А НЕ ЗА ЗАПИСЬ В ЭТОТ АКТОР. Разбор целиком — у
		//   объявления GlowWriterGateSuspectEpoch в AsdaEffectActor.h. Коротко: сброс Ф18 стоял
		//   ЗДЕСЬ, внутри ветки «пишут в Emitters», и требовал записи при гейте ≠ 1. Но законный
		//   писатель один — сборщик, а он держит гейт ПОДНЯТЫМ всё время работы
		//   (Tools/asda_fx_common.py::emitters_writer_gate), то есть его запись приходит при гейте
		//   == 1 и ничего не снимает. Оставался сценарий «эффект X не трогали между залипаниями»:
		//   сброса нет ⇒ второе предупреждение не выдаётся ⇒ правка откатывается молча.
		//   Теперь причина снимается там, где она и исчезает — в колбэке самого ключа
		//   (AsdaGlowOnEmittersWriterGateChanged выше): уход значения из 1 увеличивает эпоху, и
		//   несовпадение номеров делает следующее залипание НОВЫМ событием сразу у всех эффектов.
		// ═══ ★★★ ВОЛНА Ф20 (С-1): ПОВТОР ЕЩЁ И ПО ВРЕМЕНИ ═══════════════════════════════════════
		//   Разбор целиком — у объявления GlowWriterGateSuspectTimeSec в AsdaEffectActor.h.
		//   Коротко: «один раз за эпоху» оставляло владельца без слова, если он пропустил
		//   единственное всплывающее сообщение (оно живёт 15 с) и продолжил перебирать ТОТ ЖЕ
		//   эффект при том же поднятом ключе. Ключ при этом не опускается, эпоха не меняется —
		//   и каждая следующая правка откатывается молча. Теперь предупреждение возвращается
		//   через `asda.Fx.Glow.GateWarnRepeatSec` секунд (умолчание 25).
		const int32 GateEpochNow = GetGlowWriterGateEpoch();
		const bool bLooksHuman = (Event.Property != Event.MemberProperty)
			|| (Event.ChangeType != EPropertyChangeType::Unspecified);
		if (GAsdaFxGlowEmittersWriter == 1 && bLooksHuman)
		{
			// Часы платформы, а не времени мира: правка приходит и при остановленной симуляции,
			//   и в коммандлете, где мира может не быть вовсе.
			const double NowSec = FPlatformTime::Seconds();
			// ПЕРВАЯ ПРИЧИНА ПОВТОРА — новая эпоха гейта (закон Ф19, без изменений).
			const bool bNewEpoch = (GlowWriterGateSuspectEpoch != GateEpochNow);
			// ВТОРАЯ ПРИЧИНА — время. Интервал ≤ 0 отключает повтор по времени намеренно (путь
			//   отступления владельцу). Стартовое -1.0 значит «не предупреждали»: разность с ним
			//   у монотонных часов заведомо больше любого интервала, но на новую эпоху мы и так
			//   сработаем, поэтому отдельного случая здесь не нужно.
			const float RepeatSec = GetGlowWriterGateWarnRepeatSec();
			const bool bIntervalPassed = (RepeatSec > 0.f)
				&& ((NowSec - GlowWriterGateSuspectTimeSec) >= (double)RepeatSec);
			if (bNewEpoch || bIntervalPassed)
			{
				// Сколько правок владельца откатилось молча с прошлого раза — это и есть
				//   потерянная работа, поэтому число идёт прямо в текст сообщения.
				const int32 MutedSince = GlowWriterGateSuspectMuted;
				GlowWriterGateSuspectEpoch = GateEpochNow;
				GlowWriterGateSuspectTimeSec = NowSec;
				GlowWriterGateSuspectMuted = 0;
				// Слово о причине повтора готовится отдельно: директиве препроцессора и ветвлению
				//   внутри списка аргументов UE_LOG не место.
				const TCHAR* const WhyWord = bNewEpoch
					? TEXT("первая правка в этой эпохе гейта")
					: TEXT("ПОВТОР: ключ всё ещё поднят, а правки продолжаются");
				UE_LOG(LogAsdaFxManualLock, Warning, TEXT("[поэмиттерное свечение] %s: ключ %s ПОДНЯТ в 1 («пишет скрипт»), а правка пришла в ЧЕЛОВЕЧЕСКОЙ форме (Property=%p MemberProperty=%p вид=0x%X, эпоха гейта %d, %s; молча откачено с прошлого предупреждения: %d правок). Такая правка молча откатывается. Если сейчас никакой сборщик не работает, ключ залип — верните его: asda.Fx.Glow.EmittersWriter -1"),
					*GetName(), TEXT("asda.Fx.Glow.EmittersWriter"),
					(const void*)Event.Property, (const void*)Event.MemberProperty, (uint32)Event.ChangeType,
					GateEpochNow, WhyWord, MutedSince);
				// Видимое сообщение показывает редакторский плагин: игровой модуль своих окон не
				// заводит. Тот же приём, что у OnEmitterGlowOrphanOverflow (волна Ф16, МЕЛК-4).
				OnEmittersWriterGateSuspect().Broadcast(this, GAsdaFxGlowEmittersWriter);
			}
			else
			{
				// Повторы ВНУТРИ ИНТЕРВАЛА — только в Verbose: перетаскивание ползунка даёт десятки
				// событий в секунду, и Warning на каждое утопил бы лог (и само первое предупреждение).
				// ★ Ф20: но каждый такой повтор ТЕПЕРЬ СЧИТАЕТСЯ — число уйдёт в следующее
				//   предупреждение, чтобы владелец увидел объём уже потерянной работы.
				++GlowWriterGateSuspectMuted;
				UE_LOG(LogAsdaFxManualLock, Verbose, TEXT("[поэмиттерное свечение] %s: правка человеческой формы при поднятом ключе (повтор в эпохе %d, подавлено подряд %d; до следующего предупреждения %.1f с)"),
					*GetName(), GateEpochNow, GlowWriterGateSuspectMuted,
					(RepeatSec > 0.f) ? ((double)RepeatSec - (NowSec - GlowWriterGateSuspectTimeSec)) : 0.0);
			}
		}

		// ★★★ ПЕРЕПЕЧКА В ТОЙ ЖЕ СЕССИИ. Сборщик (Tools/asda_fx_common.py::finish_effect_bp и
		//   Tools/asda_place_effects.py) пишет `emitters` ЦЕЛИКОМ. Именно здесь и только здесь
		//   настройки надо ВЕРНУТЬ из долговечной копии: без этого перепечатанный эффект светил бы
		//   дефолтом до перезагрузки карты, и владелец, глядя на экран сразу после прогона сборщика,
		//   увидел бы «настройки потерялись» — неотличимо от настоящей потери. При правке В ПАНЕЛИ
		//   восстановление ЗАПРЕЩЕНО: владелец, вернувший эмиттеру «взято от эффекта» (в том числе
		//   через «Reset to Default»), получил бы старое значение обратно.
		if (!bFromPanel)
		{
			const int32 Back = RestoreEmitterGlow();
			if (Back > 0)
			{
				UE_LOG(LogAsdaFxManualLock, Log, TEXT("[поэмиттерное свечение] %s: перепечка эмиттеров — восстановлено %d из %d записей копии"),
					*GetName(), Back, EmitterGlowSaved.Num());
			}
		}
		for (FAsdaFxEmitter& Em : Emitters)
		{
			if (Em.Glow.Source == EAsdaFxEmitterGlowSource::Manual && !Em.Glow.bSpecSeeded)
			{
				Em.Glow.Spec = FxLight;
				Em.Glow.Spec.bManual = true;   // числа теперь СВОИ: генератор дефолтов их не трогает
				Em.Glow.bSpecSeeded = true;
			}
			// Возврат в «от эффекта» снимает признак: следующий переход в «вручную» снова подхватит
			// АКТУАЛЬНЫЕ числа эффекта, а не те, что были когда-то.
			if (Em.Glow.Source == EAsdaFxEmitterGlowSource::FromEffect) { Em.Glow.bSpecSeeded = false; }
		}
		// ═══ ★★★ ВОЛНА Ф14 (К-1, 20.09.2026): ЗДЕСЬ СТОЯЛО ГОЛОЕ SaveEmitterGlow() ════════════════
		//   И ИМЕННО ЭТО ТЕРЯЛО РАБОТУ ВЛАДЕЛЬЦА. Неавторитетная запись включает сторож «пустой
		//   снимок копию не затирает», а вернув ЕДИНСТВЕННОМУ настроенному эмиттеру «взято от
		//   эффекта», владелец получает ровно пустой снимок при непустой копии. Отменённая запись
		//   оставалась в EmitterGlowSaved, уходила в .uasset и воскресала при следующей загрузке.
		// ═══ ★★★ ВОЛНА Ф15 (К-1): ПРИЗНАК ЗАМЕНЁН — `Leaf != Member` ПО ИМЕНАМ БЫЛ НЕВЕРЕН ═══════
		//   Он не отличал панель от сборщика ни на операциях над самим массивом, ни на «Reset to
		//   Default» по одному элементу (у ArrayProperty->Inner ТО ЖЕ имя). Теперь решение принимает
		//   IsEmittersWriteFromDetailsPanel — по явному ключу, адресам свойств и виду правки.
		//   Рука владельца ⇒ авторитетно: в `Emitters` сейчас ровно то, что он хотел.
		SaveEmitterGlow(/*bAuthoritative=*/ bFromPanel);
	}
	Super::PostEditChangeProperty(Event);
}

// ★ Ф15 (С-2): событие «этот эффект участвовал в отмене/повторе». Обоснование — у объявления.
AAsdaEffectActor::FAsdaFxOnPostEditUndo& AAsdaEffectActor::OnPostEditUndo()
{
	static FAsdaFxOnPostEditUndo Event;   // один на модуль: подписчик — редакторский плагин
	return Event;
}

void AAsdaEffectActor::PostEditUndo()
{
	Super::PostEditUndo();
	OnPostEditUndo().Broadcast(this);
}

void AAsdaEffectActor::PostEditUndo(TSharedPtr<ITransactionObjectAnnotation> TransactionAnnotation)
{
	// Вторая форма — та же по смыслу: движок выбирает её, когда у записи транзакции есть аннотация
	// (EditorTransaction.cpp:952-957). Пропусти мы её — часть отмен осталась бы незамеченной.
	Super::PostEditUndo(TransactionAnnotation);
	OnPostEditUndo().Broadcast(this);
}
#endif // WITH_EDITOR
