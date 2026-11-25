// Copyright (c) 2026 @sitxovski (Discord)
// Реализация слоя данных «Конфигуратора свечения Asda» — см. AsdaFxGlowConfigModel.h.
#include "AsdaFxGlowConfigModel.h"
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "AsdaItemBrowserData.h"

#include "AsdaEffectActor.h"
#include "AsdaFxNames.h"                    // ★ дедуп №2 В1-C (F5): имя BP блёсток лута — одна константа с лут-актором
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10): имя параметра свечения мастеров эффектов
#include "World/AsdaFxLightSettings.h"
#include "World/AsdaFxLightSubsystem.h"

#include "IPythonScriptPlugin.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Internationalization/Regex.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"   // ★ волна Р1 п.7: пометка пакета ассета изменённым (поэмиттерные настройки)
#include "UObject/ObjectSaveContext.h" // ★ ревизия Ф12 (С2): пересъём снимка «как в ассете» после записи пакета
#include "EditConditionEvaluator.h"    // ★ ревизия Ф11 (С1): ШТАТНЫЙ разбор meta EditCondition (модуль PropertyEditor)
#include "ScopedTransaction.h"         // ★ ревизия Ф11 (К2): правка CDO ассета — под транзакцией, с отменой
#include "Editor.h"                    // GEditor (транзакция доступна только в редакторе)
#include "Editor/TransBuffer.h"        // ★ Ф16 (МЕЛК-1): OnBeforeRedoUndo/OnUndo/OnRedo — точные границы транзакции
#include "Misc/HotReloadInterface.h"   // ★ Ф15 (МЕЛК-3): OnModuleCompilerStarted — предупредить ДО выгрузки модуля
#include "Modules/ModuleManager.h"     // ★ Ф15 (МЕЛК-3): FModuleManager::GetModulePtr («HotReload» может и не быть)
// ★ волна Ф14 (МЕЛК-5): предупреждение о потере склада снимков при выгрузке модуля — всплывающим
//   сообщением редактора (модальное окно в обработчике выгрузки Live Coding недопустимо)
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGlowConfig, Log, All);

// Поля, которые владелец крутит чаще всего — наверху панели в этом порядке; прочие уходят в «Дополнительно»
static const TCHAR* GAsdaGlowMainFields[] = {
	TEXT("bEnabled"), TEXT("IntensityCd"), TEXT("bOverrideEmissive"), TEXT("EmissiveBoost"),
	TEXT("ColorMode"), TEXT("ColorSRGB"), TEXT("ColorSaturation"),
	TEXT("RadiusMin"), TEXT("RadiusMax"), TEXT("Shape"), TEXT("Scope"),
	TEXT("bCastShadow"), TEXT("MaxDrawDistance"), TEXT("FlickerHz"), TEXT("FlickerAmplitude"),
};

// Эффект блёсток дропа, если BP_LootDropFx не отдал имя (данные эталона: eff_0_item_99_drop01-a/-b)
static const TCHAR* GAsdaLootJebFallback = TEXT("eff_0_item_99_drop01-a_00");
// ★ Дедуп №2 В1-C (F5): путь класса блёсток — общим правилом от того же имени BP, что спавнит лут-актор (была вторая копия
//   литерала полного пути). Строка побайтно прежняя: «/Game/AsdaAssets/Effects/BP_LootDropFx.BP_LootDropFx_C».
static FString AsdaLootFxClassPath() { return AsdaAssetPaths::FxClassPathFromBp(AsdaFxBp::kLootDrop); }

// ═══ ★★★ ВОЛНА Р0 (20.09.2026, п.2): РАЗДЕЛЫ ПАНЕЛИ ПОЛЕЙ ═══════════════════════════════════════════
//   ЗАЧЕМ. До волны Р0 семьдесят с лишним полей шли ОДНИМ списком: «основные» сверху, всё остальное —
//   сплошной лентой под галочкой «Все поля». Найти в ней нужную строку глазами нельзя, а владелец
//   правит эти числа руками каждый день. Разделы — те же шесть смысловых групп, на которые поля
//   делятся в самой структуре FAsdaFxLightSpec, только теперь они видны и складываются.
//   ★ Поле, которого нет ни в одном списке, попадает в «Служебное» — новый параметр не исчезает молча.
static const TCHAR* GAsdaGlowSectionTitles[] = {
	TEXT("Свет"),
	TEXT("Свечение эффекта"),
	TEXT("Форма и размещение"),
	TEXT("Тень"),
	TEXT("Отбор и дальность"),
	TEXT("Служебное"),
};

// Поля раздела «Свет»: включение, охват, сила, цвет, мерцание и ДАЛЬНОСТЬ ЗАТУХАНИЯ
static const TCHAR* GAsdaGlowSecLight[] = {
	TEXT("bEnabled"), TEXT("Scope"), TEXT("IntensityCd"),
	TEXT("ColorMode"), TEXT("ColorSRGB"), TEXT("ColorSaturation"),
	TEXT("FlickerHz"), TEXT("FlickerAmplitude"),
	TEXT("RadiusMode"), TEXT("RadiusScale"), TEXT("RadiusBoundsScale"), TEXT("RadiusMin"), TEXT("RadiusMax"),
	TEXT("VolumetricScattering"), TEXT("bAffectGI"),
};
// Поля раздела «Свечение эффекта»: яркость САМОГО эффекта на экране (эмиссия материала)
static const TCHAR* GAsdaGlowSecEmissive[] = { TEXT("bOverrideEmissive"), TEXT("EmissiveBoost") };
// Поля раздела «Форма и размещение»: форма источника, её размеры, оси, привязка, раскладка и размер лампы.
// ★ «Размер лампы» (SourceRadius/SoftSourceRadius) стоит ЗДЕСЬ, а не рядом с «Дальностью затухания»:
//   именно соседство этих двух величин владелец назвал путающим (жалоба 20.09, п.5 волны Р0)
static const TCHAR* GAsdaGlowSecShape[] = {
	TEXT("Shape"),
	TEXT("RectSizeMode"), TEXT("RectWidth"), TEXT("RectHeight"), TEXT("RectSizeScale"),
	TEXT("RectNormalMode"), TEXT("RectRotation"), TEXT("bRectTwoSided"),
	TEXT("BarnDoorAngle"), TEXT("BarnDoorLength"),
	TEXT("SpotDirMode"), TEXT("SpotRotation"), TEXT("SpotOuterCone"), TEXT("SpotInnerCone"),
	TEXT("TubeLengthScale"), TEXT("TubeLengthMode"), TEXT("TubeLength"),
	TEXT("RingSegments"), TEXT("RingRadiusScale"), TEXT("RingSizeMode"), TEXT("RingRadiusUU"),
	TEXT("AutoElongation"), TEXT("AutoFlatness"), TEXT("bAutoFlatRoundAsDisc"),
	TEXT("ShapeAnchor"), TEXT("OffsetLocal"), TEXT("ShapeRotation"),
	TEXT("ShapeSegments"), TEXT("ShapeSegmentRadiusScale"),
	TEXT("DiscSizeMode"), TEXT("DiscRadius"), TEXT("DiscSizeScale"), TEXT("DiscNormalMode"),
	TEXT("DiscStrips"), TEXT("DiscStripRadiusScale"),
	TEXT("SphereSizeMode"), TEXT("SphereRadius"), TEXT("SphereSizeScale"),
	TEXT("Position"), TEXT("Offset"), TEXT("SourceRadius"), TEXT("SoftSourceRadius"),
};
// Поля раздела «Тень»
static const TCHAR* GAsdaGlowSecShadow[] = {
	TEXT("bCastShadow"), TEXT("ShadowResolutionScale"), TEXT("ShadowMaxDistance"),
	TEXT("ShadowBias"), TEXT("ShadowSlopeBias"), TEXT("ShadowSharpen"), TEXT("ContactShadowLength"),
};
// Поля раздела «Отбор и дальность»: борьба за бюджет и исчезновение вдали
static const TCHAR* GAsdaGlowSecCull[] = {
	TEXT("Priority"), TEXT("MaxDrawDistance"), TEXT("FadeRange"), TEXT("AlphaPower"),
};

// Номер раздела поля (последний — «Служебное»: всё, что не попало ни в один список)
static int32 AsdaGlowSectionOf(FName Cpp)
{
	struct FSec { const TCHAR* const* Names; int32 Num; int32 Index; };
	static const FSec Secs[] = {
		{ GAsdaGlowSecLight,    (int32)UE_ARRAY_COUNT(GAsdaGlowSecLight),    0 },
		{ GAsdaGlowSecEmissive, (int32)UE_ARRAY_COUNT(GAsdaGlowSecEmissive), 1 },
		{ GAsdaGlowSecShape,    (int32)UE_ARRAY_COUNT(GAsdaGlowSecShape),    2 },
		{ GAsdaGlowSecShadow,   (int32)UE_ARRAY_COUNT(GAsdaGlowSecShadow),   3 },
		{ GAsdaGlowSecCull,     (int32)UE_ARRAY_COUNT(GAsdaGlowSecCull),     4 },
	};
	for (const FSec& S : Secs)
	{
		for (int32 k = 0; k < S.Num; ++k) { if (Cpp == FName(S.Names[k])) { return S.Index; } }
	}
	return (int32)UE_ARRAY_COUNT(GAsdaGlowSectionTitles) - 1;
}

// ═══ ★★★ ВОЛНА Р0 (п.5): ПОПРАВКИ ПОДПИСЕЙ И ПОДСКАЗОК — ТОЛЬКО НА СТОРОНЕ КОНФИГУРАТОРА ═══════════
//   Сами DisplayName/ToolTip лежат в Source/AsdaReborn/Public/World/AsdaFxLightSpec.h — это файл другой
//   волны, и трогать его в этой работе нельзя. Поэтому исправленные имена живут здесь, а список нужных
//   правок в самом заголовке отдан в отчёт волны.
//   ЧТО ИМЕННО ЛЕЧИТСЯ (обследование 20.09):
//     * «Радиус мин/макс» читается как «размер лампы», хотя это ДАЛЬНОСТЬ ЗАТУХАНИЯ, и рядом действительно
//       есть отдельный «Размер источника» — две разные величины с похожими именами стояли встык;
//     * «шторки» — термин студийного света, в игровом редакторе он ничего не объясняет;
//     * «Степень альфы» — название формулы, а не того, что параметр делает;
//     * «Эмиттер-донор» — жаргон волны Ф2, владельцу нужен «номер эмиттера»;
//     * «Дальность видимости» путается с дальностью отсечения самого эффекта.
//   TipHead дописывается ПЕРЕД исходной подсказкой, а не вместо неё: в исходных подсказках лежат единицы,
//   цены и замеры, терять их нельзя.
struct FAsdaGlowTextFix
{
	const TCHAR* Cpp;      // имя поля C++
	const TCHAR* Name;     // подпись в конфигураторе ("" — оставить как в DisplayName, только без номера)
	const TCHAR* TipHead;  // уточнение перед исходной подсказкой ("" — не дописывать)
};
static const FAsdaGlowTextFix GAsdaGlowTextFix[] = {
	{ TEXT("RadiusMin"), TEXT("Дальность затухания: минимум, юн"),
	  TEXT("ЭТО ДАЛЬНОСТЬ, А НЕ РАЗМЕР ЛАМПЫ: на этой дистанции свет гаснет полностью. Размер самой лампы задаёт «Размер лампы (мягкость блика)» в разделе «Форма и размещение».") },
	{ TEXT("RadiusMax"), TEXT("Дальность затухания: максимум, юн"),
	  TEXT("ЭТО ДАЛЬНОСТЬ, А НЕ РАЗМЕР ЛАМПЫ: на этой дистанции свет гаснет полностью. Размер самой лампы задаёт «Размер лампы (мягкость блика)» в разделе «Форма и размещение».") },
	{ TEXT("RadiusMode"), TEXT("Дальность затухания: откуда брать"), TEXT("") },
	{ TEXT("RadiusScale"), TEXT("Дальность затухания: множитель геометрии, x"), TEXT("") },
	{ TEXT("RadiusBoundsScale"), TEXT("Дальность затухания: множитель габаритов, x"), TEXT("") },
	{ TEXT("BarnDoorAngle"), TEXT("Прямоугольник: сужение пучка, град"),
	  TEXT("В панели деталей это поле называется «шторки» (жаргон студийного света).") },
	{ TEXT("BarnDoorLength"), TEXT("Прямоугольник: длина сужения, юн"),
	  TEXT("В панели деталей это поле называется «длина шторок».") },
	{ TEXT("AlphaPower"), TEXT("Как гаснет вместе с эффектом (степень)"), TEXT("") },
	{ TEXT("Emitter"), TEXT("Номер эмиттера-образца (-1 = авто)"),
	  TEXT("В панели деталей это поле называется «Эмиттер-донор».") },
	{ TEXT("MaxDrawDistance"), TEXT("Свет виден до, юн"), TEXT("") },
	{ TEXT("FadeRange"), TEXT("Свет гаснет на последних, юн"), TEXT("") },
	{ TEXT("SourceRadius"), TEXT("Размер лампы (мягкость блика), юн"),
	  TEXT("Это РАЗМЕР САМОЙ ЛАМПЫ, а не дальность: дальность задают «Дальность затухания: минимум/максимум» в разделе «Свет».") },
	{ TEXT("SoftSourceRadius"), TEXT("Размер лампы: мягкая кромка, юн"), TEXT("") },
	// ★★★ РЕВИЗИЯ Ф11 (С4): ЗАМОК АССЕТА — ТОЛЬКО ДЛЯ ЧТЕНИЯ. Поле пришло в панель вместе с таблицей
	//   SPEC_FIELDS чужой волны, но записать его отсюда НЕЛЬЗЯ: генератор ini ставит у записей
	//   таблицы всегда False (Tools/asda_gen_fx_light_ini.py:326), да и смысл у замка АССЕТНЫЙ.
	//   Прятать его нельзя (молча исчезнувшее поле — та же ложь), поэтому показываем серым с
	//   объяснением, где его правят на самом деле.
	{ TEXT("bManual"), TEXT("Замок ассета «Переопределено вручную» (только чтение)"),
	  TEXT("ЗДЕСЬ ЭТО ПОЛЕ НЕ ПРАВИТСЯ. Замок принадлежит САМОМУ АССЕТУ BP_Fx_… (категория Effect Glow в панели деталей) и означает «поля этого ассета старше правила семейства». У записей таблицы свечения он всегда выключен: генератор ini пишет False. Показано для справки — чтобы порядок разрешения (эмиттер → предмет → эффект → ЗАМОК → семейство → поля ассета) читался целиком.") },
};

// ★ Волна Р0 (п.3): СНЯТЬ НОМЕР С ПОДПИСИ. Номера «1.», «6.01.» в DisplayName нужны ТОЛЬКО панели
//   деталей UE: она внутри категории умеет сортировать строки по алфавиту подписи, и без номера порядок
//   «сверху частое» там рассыпался бы (довод записан в шапке FAsdaFxLightSpec, п.6). В конфигураторе
//   сортировки по подписи нет вовсе, а порядок свой — сначала часто правимые поля, затем разделы;
//   номера в нём противоречили бы этому порядку и читались как «пункт 6.23 идёт после пункта 12».
//   Поэтому номер снимается ЗДЕСЬ, а в заголовке структуры остаётся нетронутым.
static FString AsdaGlowStripNumber(const FString& In)
{
	static const FRegexPattern Pat(TEXT("^[0-9]+(\\.[0-9]+)*\\.[ \\t]*"));
	FRegexMatcher M(Pat, In);
	return M.FindNext() ? In.RightChop(M.GetMatchEnding()) : In;
}

// ═══ ★★★ РЕВИЗИЯ Ф11 (С1 и С2, 20.09.2026): РАЗБОР meta EditCondition — ШТАТНЫЙ ════════════════════
//   БЫЛО: своя рекурсивная грамматика (~120 строк: SplitTop / ReadProp / Primary / Eval). Она
//   появилась только потому, что штатного API никто не искал, а модуль PropertyEditor не был
//   подключён к AsdaItemBrowser.Build.cs. Нарушен закон check-existing-before-creating.
//   СТАЛО: EditConditionEvaluator::IsPropertyEditable(FProperty*, UScriptStruct*, void*) — ровно тот
//   же парсер FEditConditionParser, которым пользуется панель деталей, с контекстом для ГОЛОЙ
//   структуры (FStructEditConditionContext, Private/EditConditionEvaluator.cpp).
//   ЧТО ЭТИМ ЗАКРЫТО, кроме дублирования:
//     * приоритет операторов: в UE «&&» и «||» РАВНОПРАВНЫ и правоассоциативны
//       (EditConditionParser.cpp:774-775, ExpressionParserTypes.h:582-588), а своя грамматика считала
//       их по C-семантике. На нынешних 47 выражениях расхождений не было, но первое же новое условие
//       без скобок разошлось бы молча;
//     * типы полей: свой ReadProp не знал FByteProperty, int8 и int64 — условие на таком поле молча
//       считалось выполненным;
//     * fail-open: свой разбор «не понял» отдельное СРАВНЕНИЕ и считал выполненным только его, а
//       движок при неразборе считает выполненным ВСЁ выражение целиком. Теперь поведение общее.
//   ★ Ключ EditCondition читает сам движок, поэтому поле FAsdaGlowField::EditCondition осталось
//     только как быстрый признак «условие вообще есть» (и для отчётов самопроверки).

const TArray<FString>& FAsdaFxGlowConfigModel::GetSections()
{
	static const TArray<FString> Titles = []()
	{
		TArray<FString> T;
		for (const TCHAR* S : GAsdaGlowSectionTitles) { T.Add(S); }
		return T;
	}();
	return Titles;
}

bool FAsdaFxGlowConfigModel::IsFieldActive(const FAsdaGlowField& F, const FAsdaFxLightSpec& S, bool& bOutHides)
{
	bOutHides = F.bEditConditionHides;
	if (!F.Prop || F.EditCondition.IsEmpty()) { return true; }
	// ★ Ревизия Ф11 (С1): считает ШТАТНЫЙ вычислитель движка по ГОЛОЙ структуре — тот же, что у панели
	//   деталей. При неразборе выражения он возвращает true (fail open), что нам и нужно: спрятанное
	//   поле владелец не найдёт и решит, что настройки нет (закон false-negatives-method).
	//   PRAGMA_*_EXPERIMENTAL_WARNINGS — API помечен UE_EXPERIMENTAL(5.8): предупреждение гасим осознанно,
	//   а не отключением проверки на весь модуль.
	PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS
	return EditConditionEvaluator::IsPropertyEditable(F.Prop, FAsdaFxLightSpec::StaticStruct(), &S);
	PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS
}

FAsdaFxGlowConfigModel::FAsdaFxGlowConfigModel()
{
	Items = FAsdaItemBrowserData::Get();
}

FString FAsdaFxGlowConfigModel::ToolsDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools")));
}

FString FAsdaFxGlowConfigModel::WorkDir()
{
	const FString D = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AsdaFxGlow")));
	IFileManager::Get().MakeDirectory(*D, true);
	return D;
}

bool FAsdaFxGlowConfigModel::RunGlowPython(const TArray<FString>& Args, FString& OutError)
{
	IPythonScriptPlugin* P = IPythonScriptPlugin::Get();
	if (!P || !P->IsPythonAvailable())
	{
		OutError = TEXT("встроенный Python редактора недоступен (плагин PythonScriptPlugin выключен?)");
		return false;
	}
	FString ArgList;
	for (const FString& A : Args)
	{
		ArgList += FString::Printf(TEXT("r'%s', "), *A.Replace(TEXT("\\"), TEXT("/")).Replace(TEXT("'"), TEXT("")));
	}
	// Литеральный скрипт, а не имя файла: модуль перезагружается на каждом вызове (правка генератора подхватывается без перезапуска)
	FPythonCommandEx Cmd;
	Cmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	Cmd.FileExecutionScope = EPythonFileExecutionScope::Private;
	Cmd.Command = FString::Printf(TEXT(
		"import sys, importlib\n"
		"_t = r'%s'\n"
		"if _t not in sys.path:\n"
		"    sys.path.insert(0, _t)\n"
		"import asda_fx_glow_config as _afg\n"
		"_afg = importlib.reload(_afg)\n"
		"_afg.cli([%s])\n"), *ToolsDir().Replace(TEXT("\\"), TEXT("/")), *ArgList);
	if (!P->ExecPythonCommandEx(Cmd))
	{
		OutError = FString::Printf(TEXT("Python: %s"), *Cmd.CommandResult);
		return false;
	}
	return true;
}

// ── значения полей ──────────────────────────────────────────────────────────
FString FAsdaFxGlowConfigModel::ValueText(const FAsdaGlowField& F, const FAsdaFxLightSpec& S)
{
	if (!F.Prop) { return TEXT("?"); }
	if (const FBoolProperty* B = CastField<FBoolProperty>(F.Prop)) { return B->GetPropertyValue_InContainer(&S) ? TEXT("да") : TEXT("нет"); }
	if (const FFloatProperty* Fl = CastField<FFloatProperty>(F.Prop))
	{
		FString T = FString::Printf(TEXT("%.3f"), Fl->GetPropertyValue_InContainer(&S));
		while (T.EndsWith(TEXT("0"))) { T.LeftChopInline(1); }
		if (T.EndsWith(TEXT("."))) { T.LeftChopInline(1); }
		return T;
	}
	if (const FIntProperty* I = CastField<FIntProperty>(F.Prop)) { return FString::FromInt(I->GetPropertyValue_InContainer(&S)); }
	if (const FEnumProperty* E = CastField<FEnumProperty>(F.Prop))
	{
		const int64 V = E->GetUnderlyingProperty()->GetSignedIntPropertyValue(E->ContainerPtrToValuePtr<void>(&S));
		return E->GetEnum()->GetDisplayNameTextByValue(V).ToString();
	}
	if (const FStructProperty* SP = CastField<FStructProperty>(F.Prop))
	{
		if (SP->Struct == TBaseStructure<FColor>::Get())
		{
			const FColor* C = SP->ContainerPtrToValuePtr<FColor>(&S);
			return FString::Printf(TEXT("R%d G%d B%d"), C->R, C->G, C->B);
		}
		if (SP->Struct == TBaseStructure<FVector>::Get())
		{
			const FVector* V = SP->ContainerPtrToValuePtr<FVector>(&S);
			return FString::Printf(TEXT("%.0f %.0f %.0f"), V->X, V->Y, V->Z);
		}
		// ★★★ РЕВИЗИЯ Ф11 (К1, 20.09.2026): ПОВОРОТ. Волна Ф11 завела три поля `FRotator`
		//   (RectRotation, SpotRotation, ShapeRotation), а здесь ветки для них не было — колонка
		//   «по умолчанию» показывала владельцу «?» у всех трёх. Подписи русские и в том же порядке,
		//   что в панели деталей UE и в таблице генератора: Pitch, Yaw, Roll.
		if (SP->Struct == TBaseStructure<FRotator>::Get())
		{
			const FRotator* R = SP->ContainerPtrToValuePtr<FRotator>(&S);
			return FString::Printf(TEXT("тангаж %.0f°, рыскание %.0f°, крен %.0f°"), R->Pitch, R->Yaw, R->Roll);
		}
	}
	return TEXT("?");
}

TSharedPtr<FJsonValue> FAsdaFxGlowConfigModel::ValueJson(const FAsdaGlowField& F, const FAsdaFxLightSpec& S)
{
	if (!F.Prop) { return MakeShared<FJsonValueNull>(); }
	if (const FBoolProperty* B = CastField<FBoolProperty>(F.Prop)) { return MakeShared<FJsonValueBoolean>(B->GetPropertyValue_InContainer(&S)); }
	if (const FFloatProperty* Fl = CastField<FFloatProperty>(F.Prop)) { return MakeShared<FJsonValueNumber>(Fl->GetPropertyValue_InContainer(&S)); }
	if (const FIntProperty* I = CastField<FIntProperty>(F.Prop)) { return MakeShared<FJsonValueNumber>(I->GetPropertyValue_InContainer(&S)); }
	if (const FEnumProperty* E = CastField<FEnumProperty>(F.Prop))
	{
		const int64 V = E->GetUnderlyingProperty()->GetSignedIntPropertyValue(E->ContainerPtrToValuePtr<void>(&S));
		// Короткое имя элемента («Bounds») — его генератор переводит в ключ JSON обратной картой ENUM_MAPS
		return MakeShared<FJsonValueString>(E->GetEnum()->GetNameStringByValue(V));
	}
	if (const FStructProperty* SP = CastField<FStructProperty>(F.Prop))
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		if (SP->Struct == TBaseStructure<FColor>::Get())
		{
			const FColor* C = SP->ContainerPtrToValuePtr<FColor>(&S);
			Arr = { MakeShared<FJsonValueNumber>(C->R), MakeShared<FJsonValueNumber>(C->G), MakeShared<FJsonValueNumber>(C->B) };
		}
		else if (SP->Struct == TBaseStructure<FVector>::Get())
		{
			const FVector* V = SP->ContainerPtrToValuePtr<FVector>(&S);
			Arr = { MakeShared<FJsonValueNumber>(V->X), MakeShared<FJsonValueNumber>(V->Y), MakeShared<FJsonValueNumber>(V->Z) };
		}
		// ★★★ РЕВИЗИЯ Ф11 (К1): ПОВОРОТ — три числа в порядке Pitch, Yaw, Roll (тип "r" генератора).
		//   Без этой ветки отсюда уходил ПУСТОЙ массив, и Python валился на нём с ValueError —
		//   то есть правка поворота роняла СОХРАНЕНИЕ ЦЕЛИКОМ (ни один файл не записывался).
		else if (SP->Struct == TBaseStructure<FRotator>::Get())
		{
			const FRotator* R = SP->ContainerPtrToValuePtr<FRotator>(&S);
			Arr = { MakeShared<FJsonValueNumber>(R->Pitch), MakeShared<FJsonValueNumber>(R->Yaw), MakeShared<FJsonValueNumber>(R->Roll) };
		}
		else
		{
			// Незнакомая структура: пустой массив Python принять не может и не должен — говорим «не задано»
			return MakeShared<FJsonValueNull>();
		}
		return MakeShared<FJsonValueArray>(Arr);
	}
	return MakeShared<FJsonValueNull>();
}

bool FAsdaFxGlowConfigModel::ValueFromJson(const FAsdaGlowField& F, const TSharedPtr<FJsonValue>& V, FAsdaFxLightSpec& S)
{
	if (!F.Prop || !V.IsValid() || V->IsNull()) { return false; }
	if (FBoolProperty* B = CastField<FBoolProperty>(F.Prop)) { B->SetPropertyValue_InContainer(&S, V->AsBool()); return true; }
	if (FFloatProperty* Fl = CastField<FFloatProperty>(F.Prop)) { Fl->SetPropertyValue_InContainer(&S, (float)V->AsNumber()); return true; }
	if (FIntProperty* I = CastField<FIntProperty>(F.Prop)) { I->SetPropertyValue_InContainer(&S, (int32)V->AsNumber()); return true; }
	if (FEnumProperty* E = CastField<FEnumProperty>(F.Prop))
	{
		const int64 Val = E->GetEnum()->GetValueByNameString(V->AsString());
		if (Val == INDEX_NONE) { return false; }
		E->GetUnderlyingProperty()->SetIntPropertyValue(E->ContainerPtrToValuePtr<void>(&S), Val);
		return true;
	}
	if (FStructProperty* SP = CastField<FStructProperty>(F.Prop))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		if (A.Num() < 3) { return false; }
		if (SP->Struct == TBaseStructure<FColor>::Get())
		{
			FColor* C = SP->ContainerPtrToValuePtr<FColor>(&S);
			C->R = (uint8)FMath::Clamp((int32)A[0]->AsNumber(), 0, 255);
			C->G = (uint8)FMath::Clamp((int32)A[1]->AsNumber(), 0, 255);
			C->B = (uint8)FMath::Clamp((int32)A[2]->AsNumber(), 0, 255);
			C->A = 255;
			return true;
		}
		if (SP->Struct == TBaseStructure<FVector>::Get())
		{
			FVector* Vec = SP->ContainerPtrToValuePtr<FVector>(&S);
			*Vec = FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber());
			return true;
		}
		// ★★★ РЕВИЗИЯ Ф11 (К1): ПОВОРОТ читается из [Pitch, Yaw, Roll] — тот же порядок, что пишет
		//   ValueJson и что понимает генератор (_fmt, тип "r"). Без этой ветки колонка «по умолчанию»
		//   у трёх полей поворота оставалась С НУЛЯМИ, какое бы число ни стояло в таблице.
		if (SP->Struct == TBaseStructure<FRotator>::Get())
		{
			FRotator* Rot = SP->ContainerPtrToValuePtr<FRotator>(&S);
			*Rot = FRotator(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber());
			return true;
		}
	}
	return false;
}

// ── загрузка ────────────────────────────────────────────────────────────────
bool FAsdaFxGlowConfigModel::Reload()
{
	bReady = false;
	Error.Empty();
	States.Reset();
	if (!Items.IsValid() || !Items->EnsureLoaded())
	{
		Error = FString::Printf(TEXT("таблицы предметов не загрузились: %s"), Items.IsValid() ? *Items->GetLoadError() : TEXT("нет объекта данных"));
		return false;
	}
	const FString StatePath = FPaths::Combine(WorkDir(), TEXT("state.json"));
	IFileManager::Get().Delete(*StatePath, false, true, true);
	FString PyErr;
	if (!RunGlowPython({ TEXT("export"), StatePath }, PyErr) || !ParseExport(StatePath))
	{
		Error = PyErr.IsEmpty() ? FString::Printf(TEXT("экспорт состояния не прочитан: %s"), *StatePath) : PyErr;
		return false;
	}
	// Имя .jeb блёсток — из самого ассета, чтобы конфигуратор правил ровно то, что спавнит лут-актор
	LootJeb = GAsdaLootJebFallback;
	LootFxClassPath.Empty();
	const FString LootClassPath = AsdaLootFxClassPath();   // ★ В1-C F5
	if (UClass* Cls = LoadClass<AActor>(nullptr, *LootClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		LootFxClassPath = LootClassPath;
		if (const AAsdaEffectActor* Cdo = Cast<AAsdaEffectActor>(Cls->GetDefaultObject()))
		{
			if (!Cdo->EffectName.IsEmpty()) { LootJeb = Cdo->EffectName; }
		}
	}
	if (WeaponStates.Num() == 0) { BuildWeaponStates(); }
	if (FxEffects.Num() == 0) { BuildFxEffects(); }   // ★ волна Р1 п.6: корень «Эффекты карт и навыков»
	bReady = true;
	return true;
}

// ★★★ ВОЛНА Р1 (п.6): ПЕРЕПИСЬ ИМПОРТИРОВАННЫХ ЭФФЕКТОВ.
//   Один запрос реестра ассетов на всю вкладку. Классы НЕ грузятся: имя .jeb берётся обратной формулой
//   реестра путей (та же, которой пользуется рантайм при пустом EffectName), семейство — FindRule.
//   ★ Загрузка 1283 классов BP_Fx_* ради дерева обошлась бы в минуты и сотни мегабайт; лениво грузится
//     только тот ассет, чей узел владелец раскрыл (BuildEmitterChildren).
void FAsdaFxGlowConfigModel::BuildFxEffects()
{
	FxEffects.Reset();
	TArray<FAssetData> Assets;
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByPath(FName(AsdaAssetPaths::kFxRoot), Assets, false);
	const UAsdaFxLightSettings* S = UAsdaFxLightSettings::Get();
	for (const FAssetData& AD : Assets)
	{
		const FString Name = AD.AssetName.ToString();
		if (!Name.StartsWith(TEXT("BP_Fx_"))) { continue; }
		FFxEffectEntry E;
		E.Jeb = AsdaAssetPaths::JebFromFxClassName(Name);
		E.ClassPath = AsdaAssetPaths::FxClassPathFromBp(Name);
		const FAsdaFxLightRule* R = S->FindRule(E.Jeb);
		E.FamilyId = R ? R->Id : FString(TEXT("other"));
		FxEffects.Add(MoveTemp(E));
	}
	FxEffects.Sort([](const FFxEffectEntry& A, const FFxEffectEntry& B) { return A.Jeb < B.Jeb; });
	UE_LOG(LogAsdaGlowConfig, Log, TEXT("[конфигуратор] эффектов BP_Fx_* в проекте: %d"), FxEffects.Num());
}

bool FAsdaFxGlowConfigModel::ParseExport(const FString& Path)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path)) { return false; }
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) { return false; }

	// Поля — из SPEC_FIELDS генератора: основные сверху в заданном порядке, остальные — «Дополнительно» в порядке таблицы
	Fields.Reset();
	TArray<FAsdaGlowField> All;
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("fields")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		if (A.Num() < 3) { continue; }
		FAsdaGlowField F;
		F.JsonKey = A[0]->AsString();
		F.CppName = FName(*A[1]->AsString());
		F.Type = A[2]->AsString();
		F.Prop = FAsdaFxLightSpec::StaticStruct()->FindPropertyByName(F.CppName);
		if (!F.Prop) { UE_LOG(LogAsdaGlowConfig, Warning, TEXT("[конфигуратор] поле %s есть в генераторе, но нет в FAsdaFxLightSpec"), *F.CppName.ToString()); }
		else
		{
			// ★ Волна Р0 (пп.1, 2, 3, 5): раздел, подпись без номера, поправленная подсказка и условие работы —
			//   считаются ОДИН РАЗ здесь, а не на каждую перестройку панели (RebuildFields зовётся на каждый выбор)
			F.Section = AsdaGlowSectionOf(F.CppName);
			F.Label = AsdaGlowStripNumber(F.Prop->GetDisplayNameText().ToString());
			F.Tip = F.Prop->GetToolTipText().ToString();
			for (const FAsdaGlowTextFix& Fix : GAsdaGlowTextFix)
			{
				if (F.CppName != FName(Fix.Cpp)) { continue; }
				if (FCString::Strlen(Fix.Name) > 0) { F.Label = Fix.Name; }
				if (FCString::Strlen(Fix.TipHead) > 0) { F.Tip = FString(Fix.TipHead) + TEXT("\n\n") + F.Tip; }
				break;
			}
			if (F.Prop->HasMetaData(TEXT("EditCondition"))) { F.EditCondition = F.Prop->GetMetaData(TEXT("EditCondition")); }
			F.bEditConditionHides = F.Prop->HasMetaData(TEXT("EditConditionHides"));
			// ★ Ревизия Ф11 (С4): поля, которые сюда приходят, но записываются не отсюда. Список
			//   намеренно ЯВНЫЙ: молчаливое «а это поле у нас не работает» — та самая ложь, за
			//   которую волну и поправили
			static const FName NManual = GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, bManual);
			F.bReadOnly = (F.CppName == NManual);
		}
		All.Add(F);
	}
	for (const TCHAR* Name : GAsdaGlowMainFields)
	{
		if (const FAsdaGlowField* F = All.FindByPredicate([Name](const FAsdaGlowField& X) { return X.CppName == FName(Name); })) { Fields.Add(*F); }
	}
	for (FAsdaGlowField& F : All)
	{
		if (!Fields.ContainsByPredicate([&F](const FAsdaGlowField& X) { return X.CppName == F.CppName; }))
		{
			F.bAdvanced = true;
			Fields.Add(F);
		}
	}

	auto ReadOwn = [](const TSharedPtr<FJsonObject>& O)
	{
		TSet<FName> S;
		for (const TSharedPtr<FJsonValue>& V : O->GetArrayField(TEXT("own"))) { S.Add(FName(*V->AsString())); }
		return S;
	};
	Export.Reset();
	FamilyOrder.Reset();
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("families")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FAsdaGlowTarget T; T.bFamily = true; T.FamilyId = O->GetStringField(TEXT("id"));
		FExportEntry E;
		E.Own = ReadOwn(O);
		E.Defaults = O->GetObjectField(TEXT("defaults"));
		E.Doc = O->GetStringField(TEXT("doc"));
		Export.Add(T.Key(), E);
		FamilyOrder.Add(T.FamilyId);
	}
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("overrides")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FAsdaGlowTarget T; T.Jeb = O->GetStringField(TEXT("jeb")); T.ItemId = (int32)O->GetNumberField(TEXT("item_id"));
		FExportEntry E;
		E.Own = ReadOwn(O);
		E.Defaults = O->GetObjectField(TEXT("defaults"));
		E.Label = O->GetStringField(TEXT("label"));
		Export.Add(T.Key(), E);
	}
	BoostRules.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Boost = nullptr;
	if (Root->TryGetArrayField(TEXT("boost"), Boost))
	{
		for (const TSharedPtr<FJsonValue>& V : *Boost)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			// Правило только для BLEND_MODULATE (modulate_never) к яркости свечения отношения не имеет
			bool bOnlyMod = true;
			for (const TSharedPtr<FJsonValue>& M : O->GetArrayField(TEXT("modes"))) { if (M->AsString() != TEXT("mod")) { bOnlyMod = false; } }
			if (bOnlyMod) { continue; }
			BoostRules.Add({ O->GetStringField(TEXT("id")), O->GetStringField(TEXT("jeb")), (float)O->GetNumberField(TEXT("boost")) });
		}
	}
	return Fields.Num() > 0 && FamilyOrder.Num() > 0;
}

void FAsdaFxGlowConfigModel::BuildWeaponStates()
{
	WeaponStates.Reset();
	JebItemCount.Reset();
	// Какие BP_Fx_* реально импортированы — одним запросом реестра (а не PackageExists на каждую из ~десятка тысяч ступеней)
	TSet<FString> Bps;
	{
		TArray<FAssetData> Assets;
		FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AR.Get().GetAssetsByPath(FName(AsdaAssetPaths::kFxRoot), Assets, false);   // ★ волна 2 п. 7: корень — реестр путей
		for (const FAssetData& A : Assets) { Bps.Add(A.AssetName.ToString()); }
	}
	for (int32 i = 0; i < Items->Num(); ++i)
	{
		const FAsdaItemRow* R = Items->GetItemRow(i);
		if (!R || (R->ItemType != 8 && R->ItemType != 9)) { continue; }
		for (int32 Enh = 5; Enh <= 7; ++Enh)
		{
			// Формула имени — 1:1 с рантаймом (UAsdaWorldSubsystem::BuildGlowJebName), общая с браузером предметов
			const FString Jeb = Items->BuildGlowJebName(i, Enh);
			if (Jeb.IsEmpty()) { continue; }
			const FString Bp = AsdaAssetPaths::FxBpName(Jeb);   // ★ волна 2 п. 7: правила реестра путей
			FWeaponState W;
			W.ItemIndex = i;
			W.Code = R->ItemCode;
			W.Enhance = Enh;
			W.Jeb = Jeb;
			W.FxClassPath = Bps.Contains(Bp) ? AsdaAssetPaths::FxClassPathFromBp(Bp) : FString();
			WeaponStates.Add(W);
			JebItemCount.FindOrAdd(Jeb.ToLower())++;
		}
	}
	UE_LOG(LogAsdaGlowConfig, Log, TEXT("[конфигуратор] ступеней свечения оружия/щитов: %d, разных эффектов: %d"), WeaponStates.Num(), JebItemCount.Num());
}

// ── дерево ──────────────────────────────────────────────────────────────────
// ★ Волна Р1 (п.7): УЗЕЛ-ЗАГЛУШКА ПОД ЭФФЕКТОМ. STreeView рисует стрелку раскрытия только у узла, у
//   которого УЖЕ есть дети, а грузить ассет ради настоящего списка эмиттеров до раскрытия нельзя —
//   в проекте 1283 эффекта. Заглушка стоит ровно до первого раскрытия (BuildEmitterChildren её снимает)
// ★★★ Ревизия Ф12 (С3): есть ли у эффекта ПОЭМИТТЕРНЫЕ числа прямо в ассете — ТРИ ответа.
//   FindObject ничего не грузит, и для незагруженного класса честный ответ — «НЕ ЗНАЮ».
//   БЫЛО: незнание возвращалось как false, то есть как уверенное «правок нет». Эффект с
//   поэмиттерной настройкой из ПРОШЛОГО сеанса под фильтром «только изменённые» в дерево не
//   попадал, а подсказка советовала «раскройте его узел» — узла-то в отфильтрованном дереве и нет.
//   Классическое ложное «нет» (закон false-negatives-method).
//   СТАЛО: незнание отдаётся отдельным значением, в дереве рисуется «?», а сколько их — печатается
//   рядом с фильтром. Грузить 1283 класса ради отметки по-прежнему нельзя.
enum class EAsdaGlowFxMark : uint8 { No, Yes, Unknown };
static EAsdaGlowFxMark AsdaGlowFxHasEmitterGlowLoaded(const FString& ClassPath)
{
	if (ClassPath.IsEmpty()) { return EAsdaGlowFxMark::No; }   // ассета нет вовсе — правкам взяться негде
	UClass* Cls = FindObject<UClass>(nullptr, *ClassPath);
	const AAsdaEffectActor* Cdo = Cls ? Cast<AAsdaEffectActor>(Cls->GetDefaultObject(false)) : nullptr;
	if (!Cdo) { return EAsdaGlowFxMark::Unknown; }             // класс не в памяти — прочитать нечем
	for (const FAsdaFxEmitter& E : Cdo->Emitters)
	{
		if (E.Glow.Source != EAsdaFxEmitterGlowSource::FromEffect) { return EAsdaGlowFxMark::Yes; }
	}
	// ★ И долговечная копия: эмиттеры могли быть перепечатаны, а настройки ещё не восстановлены
	return Cdo->EmitterGlowSaved.Num() > 0 ? EAsdaGlowFxMark::Yes : EAsdaGlowFxMark::No;
}

static void AsdaGlowAddEmitterStub(const TSharedPtr<FAsdaGlowNode>& Node)
{
	if (!Node.IsValid() || !Node->bNeedEmitters || Node->Children.Num() > 0) { return; }
	TSharedPtr<FAsdaGlowNode> Stub = MakeShared<FAsdaGlowNode>();
	Stub->Kind = EAsdaGlowNodeKind::Group;
	Stub->Label = TEXT("(раскрыть — эмиттеры эффекта)");
	Node->Children.Add(Stub);
}

void FAsdaFxGlowConfigModel::BuildTree(const FString& Filter, bool bOnlyChanged, TArray<TSharedPtr<FAsdaGlowNode>>& OutRoots) const
{
	OutRoots.Reset();
	NumUnknownGlow = 0;   // ★ С3: считаем заново при каждом построении дерева
	if (!bReady) { return; }
	const FString Flt = Filter.TrimStartAndEnd().ToLower();
	auto Hit = [&Flt](const FString& S) { return Flt.IsEmpty() || S.ToLower().Contains(Flt); };
	const UAsdaFxLightSettings* Set = UAsdaFxLightSettings::Get();
	auto HasOv = [this](const FString& Jeb, int32 Item) { FAsdaGlowTarget T; T.Jeb = Jeb; T.ItemId = Item; return Export.Contains(T.Key()); };
	// ★ Волна Р1 (п.8): несохранённое у записи эффекта/предмета и у семейства
	auto HasPendOv = [this](const FString& Jeb, int32 Item) { FAsdaGlowTarget T; T.Jeb = Jeb; T.ItemId = Item; return HasPending(T); };
	auto HasPendFam = [this](const FString& Id) { FAsdaGlowTarget T; T.bFamily = true; T.FamilyId = Id; return HasPending(T); };
	// ★ Волна Р1 (п.8): фильтр «только изменённые» — узел проходит, если у него есть сохранённая (●)
	//   или несохранённая (*) правка. У групп решают дети: пустая группа в дерево не попадает
	auto Keep = [bOnlyChanged](bool bMark, bool bPend) { return !bOnlyChanged || bMark || bPend; };

	// 1. Глобальные семейства
	TSharedPtr<FAsdaGlowNode> Glob = MakeShared<FAsdaGlowNode>();
	for (const FString& Id : FamilyOrder)
	{
		const FAsdaFxLightRule* Rule = Set->Families.FindByPredicate([&Id](const FAsdaFxLightRule& R) { return R.Id == Id; });
		const FString Jeb = Rule ? Rule->Jeb : FString();
		if (!Hit(Id) && !Hit(Jeb)) { continue; }
		// У семейства запись в таблице есть ВСЕГДА, поэтому «изменённым» считается только несохранённое
		const bool bPend = HasPendFam(Id);
		if (!Keep(false, bPend)) { continue; }
		TSharedPtr<FAsdaGlowNode> N = MakeShared<FAsdaGlowNode>();
		N->Kind = EAsdaGlowNodeKind::Family;
		N->FamilyId = Id;
		N->Jeb = Jeb;
		N->bDirty = bPend;
		N->Label = FString::Printf(TEXT("%s   %s"), *Id, *Jeb);
		Glob->Children.Add(N);
	}
	Glob->Label = FString::Printf(TEXT("Глобальные семейства (%d)"), Glob->Children.Num());
	Glob->Count = Glob->Children.Num();
	if (Glob->Children.Num() > 0) { OutRoots.Add(Glob); }

	// 2. Лут: семейство, эффект у всех предметов, сохранённые переопределения отдельных предметов
	TSharedPtr<FAsdaGlowNode> Loot = MakeShared<FAsdaGlowNode>();
	const FString LootFam = FamilyOfJeb(LootJeb);
	if (Hit(LootFam) || Hit(TEXT("лут дроп")) || Hit(LootJeb))
	{
		if (Keep(false, HasPendFam(LootFam)))
		{
			TSharedPtr<FAsdaGlowNode> F = MakeShared<FAsdaGlowNode>();
			F->Kind = EAsdaGlowNodeKind::Family; F->FamilyId = LootFam;
			F->bDirty = HasPendFam(LootFam);
			F->Label = FString::Printf(TEXT("Семейство %s (свет всех эффектов дропа)"), *LootFam);
			Loot->Children.Add(F);
		}
		if (Keep(HasOv(LootJeb, 0), HasPendOv(LootJeb, 0)))
		{
			TSharedPtr<FAsdaGlowNode> E = MakeShared<FAsdaGlowNode>();
			E->Kind = EAsdaGlowNodeKind::Effect; E->Jeb = LootJeb; E->FamilyId = LootFam; E->FxClassPath = LootFxClassPath;
			E->bHasOverride = HasOv(LootJeb, 0);
			E->bDirty = HasPendOv(LootJeb, 0);
			E->bNeedEmitters = !LootFxClassPath.IsEmpty();   // ★ волна Р1 п.7: эмиттеры строятся при раскрытии
			AsdaGlowAddEmitterStub(E);
			E->Label = FString::Printf(TEXT("Блёстки дропа — все предметы   %s"), *LootJeb);
			Loot->Children.Add(E);
		}
	}
	for (const TPair<FString, FExportEntry>& P : Export)
	{
		TArray<FString> Parts;
		P.Key.ParseIntoArray(Parts, TEXT("|"), false);
		if (Parts.Num() != 3 || Parts[0] != TEXT("O") || Parts[1] != LootJeb.ToLower() || Parts[2] == TEXT("0")) { continue; }
		const int32 Code = FCString::Atoi(*Parts[2]);
		const int32 Idx = Items->IndexByCode(Code);
		const FString Name = Idx != INDEX_NONE ? Items->GetName(Idx) : P.Value.Label;
		if (!Hit(Name) && !Hit(Parts[2]) && !Hit(LootJeb)) { continue; }
		TSharedPtr<FAsdaGlowNode> E = MakeShared<FAsdaGlowNode>();
		E->Kind = EAsdaGlowNodeKind::Effect; E->Jeb = LootJeb; E->FamilyId = LootFam; E->FxClassPath = LootFxClassPath;
		E->ItemCode = Code; E->ItemIndex = Idx; E->bHasOverride = true;
		E->bDirty = HasPendOv(LootJeb, Code);
		E->bNeedEmitters = !LootFxClassPath.IsEmpty();
		AsdaGlowAddEmitterStub(E);
		E->Label = FString::Printf(TEXT("%d : %s — блёстки этого предмета"), Code, Name.IsEmpty() ? TEXT("(имени нет)") : *Name);
		Loot->Children.Add(E);
	}
	Loot->Label = TEXT("Лут");
	Loot->Count = Loot->Children.Num();
	if (Loot->Children.Num() > 0) { OutRoots.Add(Loot); }

	// 3. Оружие и щиты: класс → предмет → ступени
	TSharedPtr<FAsdaGlowNode> Weap = MakeShared<FAsdaGlowNode>();
	TMap<FString, TSharedPtr<FAsdaGlowNode>> Classes;
	TSharedPtr<FAsdaGlowNode> CurItem;
	for (int32 k = 0; k < WeaponStates.Num(); ++k)
	{
		const FWeaponState& W = WeaponStates[k];
		const FAsdaItemRow* R = Items->GetItemRow(W.ItemIndex);
		if (!R) { continue; }
		const FString ClassLabel = (R->ItemType == 8) ? FString(TEXT("Щиты")) : FAsdaItemBrowserData::WeaponSubTypeLabel(R->ItemSubType);
		const FString Name = Items->GetName(W.ItemIndex);
		// Совпадение по предмету: имя, код, класс или .jeb ЛЮБОЙ его ступени
		if (!CurItem.IsValid() || CurItem->ItemIndex != W.ItemIndex)
		{
			bool bMatch = Flt.IsEmpty() || Hit(Name) || Hit(FString::FromInt(W.Code)) || Hit(ClassLabel);
			for (int32 j = k; !bMatch && j < WeaponStates.Num() && WeaponStates[j].ItemIndex == W.ItemIndex; ++j) { bMatch = Hit(WeaponStates[j].Jeb); }
			if (!bMatch)
			{
				while (k + 1 < WeaponStates.Num() && WeaponStates[k + 1].ItemIndex == W.ItemIndex) { ++k; }
				CurItem.Reset();
				continue;
			}
			TSharedPtr<FAsdaGlowNode>& Cls = Classes.FindOrAdd(ClassLabel);
			if (!Cls.IsValid()) { Cls = MakeShared<FAsdaGlowNode>(); Cls->Label = ClassLabel; }
			CurItem = MakeShared<FAsdaGlowNode>();
			CurItem->Kind = EAsdaGlowNodeKind::Item;
			CurItem->ItemIndex = W.ItemIndex;
			CurItem->ItemCode = W.Code;
			CurItem->Label = FString::Printf(TEXT("%d : %s"), W.Code, Name.IsEmpty() ? TEXT("(имени нет в данных)") : *Name);
			Cls->Children.Add(CurItem);
			Cls->Count++;
		}
		const bool bStMark = HasOv(W.Jeb, W.Code) || HasOv(W.Jeb, 0);
		const bool bStPend = HasPendOv(W.Jeb, W.Code) || HasPendOv(W.Jeb, 0);
		if (!Keep(bStMark, bStPend)) { continue; }
		TSharedPtr<FAsdaGlowNode> St = MakeShared<FAsdaGlowNode>();
		St->Kind = EAsdaGlowNodeKind::ItemState;
		St->ItemIndex = W.ItemIndex;
		St->ItemCode = W.Code;
		St->Enhance = W.Enhance;
		St->Jeb = W.Jeb;
		St->FxClassPath = W.FxClassPath;
		St->FamilyId = FamilyOfJeb(W.Jeb);
		St->bHasOverride = bStMark;
		St->bDirty = bStPend;
		St->bNeedEmitters = !W.FxClassPath.IsEmpty();   // ★ волна Р1 п.7
		AsdaGlowAddEmitterStub(St);
		St->Label = FString::Printf(TEXT("+%d%s   %s%s"), W.Enhance, W.Enhance == 7 ? TEXT(" и выше") : TEXT(""), *W.Jeb,
			W.FxClassPath.IsEmpty() ? TEXT("   (ассета BP нет — в игре эффекта не будет)") : TEXT(""));
		CurItem->bHasOverride |= St->bHasOverride;
		CurItem->bDirty |= St->bDirty;
		CurItem->Children.Add(St);
	}
	Classes.KeySort([](const FString& A, const FString& B) { return A < B; });
	for (TPair<FString, TSharedPtr<FAsdaGlowNode>>& P : Classes)
	{
		// ★ Волна Р1 (п.8): при фильтре «только изменённые» предмет без уцелевших ступеней не показывается
		P.Value->Children.RemoveAll([](const TSharedPtr<FAsdaGlowNode>& N) { return N->Children.Num() == 0 && N->Kind == EAsdaGlowNodeKind::Item; });
		P.Value->Count = P.Value->Children.Num();
		if (P.Value->Count == 0) { continue; }
		P.Value->Label = FString::Printf(TEXT("%s (%d)"), *P.Key, P.Value->Count);
		Weap->Count += P.Value->Count;
		Weap->Children.Add(P.Value);
	}
	Weap->Label = FString::Printf(TEXT("Оружие и щиты — свечение заточки (%d предметов)"), Weap->Count);
	if (Weap->Children.Num() > 0) { OutRoots.Add(Weap); }

	// ═══ ★★★ 4. ВОЛНА Р1 (п.6): ЭФФЕКТЫ КАРТ И НАВЫКОВ ═══════════════════════════════════════════════
	//   ДО ЭТОЙ ВОЛНЫ ИХ В КОНФИГУРАТОРЕ НЕ БЫЛО ВОВСЕ. В дереве жили только семейства, лут и свечение
	//   заточки оружия, то есть 1283 эффекта карт, навыков, порталов и мобов правились ТОЛЬКО поштучно
	//   через панель деталей ассета — а там нет ни наследования от семейства, ни «по умолчанию», ни
	//   сравнения с соседями. Именно эти эффекты владелец и собрался перерабатывать.
	//   Группировка — по семейству света: оно же задаёт наследуемые числа, поэтому «соседи по группе»
	//   и «эффекты с общими дефолтами» — одно и то же множество.
	TSharedPtr<FAsdaGlowNode> FxRoot = MakeShared<FAsdaGlowNode>();
	{
		TMap<FString, TSharedPtr<FAsdaGlowNode>> Fams;
		for (const FFxEffectEntry& E : FxEffects)
		{
			if (!Hit(E.Jeb) && !Hit(E.FamilyId)) { continue; }
			// ★ Ревизия Ф11 (С3): ● у эффекта — не только запись таблицы, но и поэмиттерные числа,
			//   УЖЕ ЛЕЖАЩИЕ В АССЕТЕ. Признак берётся только у классов, которые уже в памяти
			//   (AsdaGlowFxHasEmitterGlowLoaded — FindObject, без загрузки): грузить 1283 класса ради
			//   отметки в дереве нельзя.
			// ★★★ Ревизия Ф12 (С3): у незагруженного класса ответ «не знаю» — он и показывается «?»,
			//   а не выдаётся за «правок нет». Под фильтром «только изменённые» такие эффекты в
			//   дерево не попадают (иначе фильтр показывал бы почти весь архив), но их ЧИСЛО
			//   считается и печатается рядом с фильтром — владелец видит, чего мы не знаем
			const EAsdaGlowFxMark AssetMark = AsdaGlowFxHasEmitterGlowLoaded(E.ClassPath);
			const bool bMark = HasOv(E.Jeb, 0) || AssetMark == EAsdaGlowFxMark::Yes;
			const bool bUnknown = !bMark && AssetMark == EAsdaGlowFxMark::Unknown;
			if (bUnknown) { ++NumUnknownGlow; }
			const bool bPend = HasPendOv(E.Jeb, 0);
			if (!Keep(bMark, bPend || IsFxAssetUnsaved(E.ClassPath))) { continue; }
			TSharedPtr<FAsdaGlowNode>& Fam = Fams.FindOrAdd(E.FamilyId);
			if (!Fam.IsValid()) { Fam = MakeShared<FAsdaGlowNode>(); Fam->FamilyId = E.FamilyId; }
			TSharedPtr<FAsdaGlowNode> N = MakeShared<FAsdaGlowNode>();
			N->Kind = EAsdaGlowNodeKind::Effect;
			N->Jeb = E.Jeb;
			N->FamilyId = E.FamilyId;
			N->FxClassPath = E.ClassPath;
			N->bHasOverride = bMark;
			N->bMarkUnknown = bUnknown;
			N->bDirty = bPend || IsFxAssetUnsaved(E.ClassPath);
			N->bNeedEmitters = true;
			AsdaGlowAddEmitterStub(N);
			N->Label = E.Jeb;
			Fam->bHasOverride |= N->bHasOverride;
			Fam->bDirty |= N->bDirty;
			Fam->Children.Add(N);
		}
		Fams.KeySort([](const FString& A, const FString& B) { return A < B; });
		for (TPair<FString, TSharedPtr<FAsdaGlowNode>>& P : Fams)
		{
			P.Value->Count = P.Value->Children.Num();
			P.Value->Label = FString::Printf(TEXT("%s (%d)"), *P.Key, P.Value->Count);
			FxRoot->Count += P.Value->Count;
			FxRoot->Children.Add(P.Value);
		}
	}
	FxRoot->Label = FString::Printf(TEXT("Эффекты карт и навыков (%d)"), FxRoot->Count);
	if (FxRoot->Children.Num() > 0) { OutRoots.Add(FxRoot); }
}

// ★★★ ВОЛНА Р1 (п.7): СПИСОК ЭМИТТЕРОВ ЭФФЕКТА — СТРОИТСЯ ПРИ РАСКРЫТИИ УЗЛА.
//   ★ Именно «свой свет у каждого эмиттера» вместе с формой и лечит жалобу владельца «свет идёт из
//   центра, а не по форме»: пока источник ОДИН на эффект, его числа — агрегат по всем эмиттерам, и
//   форма меряется по секции одного донора. Поэмиттерный свет даёт каждой светящейся части эффекта
//   свою геометрию, свой центр и свою сферу затухания.
void FAsdaFxGlowConfigModel::BuildEmitterChildren(const TSharedPtr<FAsdaGlowNode>& Node) const
{
	if (!Node.IsValid() || !Node->bNeedEmitters) { return; }
	Node->bNeedEmitters = false;   // вторая попытка не нужна: пустой список — тоже ответ
	Node->Children.Reset();        // снимаем узел-заглушку, ради которого рисовалась стрелка раскрытия
	if (Node->FxClassPath.IsEmpty()) { return; }
	UClass* Cls = LoadClass<AActor>(nullptr, *Node->FxClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	const AAsdaEffectActor* Cdo = Cls ? Cast<AAsdaEffectActor>(Cls->GetDefaultObject()) : nullptr;
	if (!Cdo) { return; }
	for (int32 i = 0; i < Cdo->Emitters.Num(); ++i)
	{
		const FAsdaFxEmitter& Em = Cdo->Emitters[i];
		TSharedPtr<FAsdaGlowNode> N = MakeShared<FAsdaGlowNode>();
		N->Kind = EAsdaGlowNodeKind::Emitter;
		N->Jeb = Node->Jeb;
		N->FamilyId = Node->FamilyId;
		N->FxClassPath = Node->FxClassPath;
		N->ItemCode = Node->ItemCode;
		N->ItemIndex = Node->ItemIndex;
		N->EmitterIndex = i;
		const TCHAR* Src =
			Em.Glow.Source == EAsdaFxEmitterGlowSource::Manual ? TEXT("свои числа") :
			Em.Glow.Source == EAsdaFxEmitterGlowSource::Off ? TEXT("света нет") : TEXT("от эффекта");
		N->bHasOverride = Em.Glow.Source != EAsdaFxEmitterGlowSource::FromEffect;
		// ★ Ревизия Ф11 (С8): * ставится ТОЛЬКО тому эмиттеру, который и правда отличается от ассета.
		//   Прежде bDirty здесь не ставился вовсе, а в строке дерева * получали ВСЕ эмиттеры тронутого
		//   ассета — по такой отметке нельзя было понять, что именно ты трогал
		FAsdaGlowEmitterRef ER; ER.ClassPath = Node->FxClassPath; ER.Index = i;
		N->bDirty = IsEmitterPending(ER);
		N->Label = FString::Printf(TEXT("эмиттер %d: %s   [тип .jeb 0x%X, %s]"), i,
			Em.Name.IsEmpty() ? TEXT("(без имени)") : *Em.Name, Em.JebType, Src);
		Node->Children.Add(N);
	}
}

// ── подписи ─────────────────────────────────────────────────────────────────
FString FAsdaFxGlowConfigModel::DescribeFamily(const FString& FamilyId) const
{
	FAsdaGlowTarget T; T.bFamily = true; T.FamilyId = FamilyId;
	const FExportEntry* E = Export.Find(T.Key());
	if (!E) { return FString(); }
	FString D = E->Doc;
	return D.Len() > 400 ? D.Left(400) + TEXT("…") : D;
}

// ★ Хвосты волны 3 (15.09.2026): тело — дословно прежний цикл DescribeBoost/CurrentMaterialBoost (порядок правил = порядок файла)
const FAsdaFxGlowConfigModel::FBoostRule* FAsdaFxGlowConfigModel::FindBoostRule(const FString& Jeb) const
{
	for (const FBoostRule& R : BoostRules)
	{
		const FRegexPattern Pat(R.Jeb, ERegexPatternFlags::CaseInsensitive);
		FRegexMatcher M(Pat, Jeb);
		if (M.FindNext()) { return &R; }
	}
	return nullptr;
}

FString FAsdaFxGlowConfigModel::DescribeBoost(const FString& Jeb) const
{
	if (const FBoostRule* R = FindBoostRule(Jeb))
	{
		return FString::Printf(TEXT("материал по таблице asda_fx_boost.json: семейство «%s», EmissiveBoost ×%.2f"), *R->Id, R->Boost);
	}
	return TEXT("материал по таблице asda_fx_boost.json: ни одно правило не подходит — EmissiveBoost ×1 (исходный)");
}

int32 FAsdaFxGlowConfigModel::NumItemsWithJeb(const FString& Jeb) const
{
	const int32* N = JebItemCount.Find(Jeb.ToLower());
	return N ? *N : 0;
}

FString FAsdaFxGlowConfigModel::FamilyOfJeb(const FString& Jeb) const
{
	const FAsdaFxLightRule* R = UAsdaFxLightSettings::Get()->FindRule(Jeb);
	return R ? R->Id : FString(TEXT("other"));
}

// ── состояние цели ──────────────────────────────────────────────────────────
// ★ волна 3 п.16 (15.09.2026): поиск записи — общие UAsdaFxLightSettings::FindOverrideIn / IndexOfOverride (была своя копия AsdaFindExactOverride)

FAsdaFxLightSpec FAsdaFxGlowConfigModel::ResolveLive(const FAsdaGlowTarget& T, bool& bOutHasEmissive) const
{
	UAsdaFxLightSettings* S = GetMutableDefault<UAsdaFxLightSettings>();
	bOutHasEmissive = false;
	if (T.bFamily)
	{
		const FAsdaFxLightRule* R = S->Families.FindByPredicate([&T](const FAsdaFxLightRule& X) { return X.Id == T.FamilyId; });
		FAsdaFxLightSpec Out = R ? R->Spec : FAsdaFxLightSpec();
		bOutHasEmissive = Out.bOverrideEmissive;
		return Out;
	}
	// Та же цепочка, что сводит генератор: своя запись → запись эффекта → семейство (эмиссия семейства НЕ наследуется).
	// ★ волна 3 п.16 (15.09.2026): «своя → эффекта» — общая формула рантайма (линейная: массив здесь правится на месте без смены ревизии)
	if (const FAsdaFxGlowOverride* Ov = UAsdaFxLightSettings::FindOverrideIn(S->Overrides, T.Jeb, T.ItemId))
	{
		bOutHasEmissive = Ov->Spec.bOverrideEmissive;
		return Ov->Spec;
	}
	const FAsdaFxLightRule* R = S->FindRule(T.Jeb);
	FAsdaFxLightSpec Out = R ? R->Spec : FAsdaFxLightSpec();
	Out.bOverrideEmissive = false;
	return Out;
}

FAsdaGlowEditState& FAsdaFxGlowConfigModel::GetState(const FAsdaGlowTarget& T)
{
	const FString Key = T.Key();
	if (FAsdaGlowEditState* Found = States.Find(Key)) { return *Found; }
	FAsdaGlowEditState St;
	St.Target = T;
	bool bEm = false;
	St.Work = ResolveLive(T, bEm);
	if (const FExportEntry* E = Export.Find(Key))
	{
		// Сохранённая запись: дефолт каждого поля посчитан генератором «если убрать этот ключ»
		St.bExistsSaved = true;
		St.SavedOwn = E->Own;
		St.Defaults = St.Work;
		for (const FAsdaGlowField& F : Fields)
		{
			const TSharedPtr<FJsonValue> V = E->Defaults.IsValid() ? E->Defaults->TryGetField(F.CppName.ToString()) : nullptr;
			if (!V.IsValid() || V->IsNull()) { St.DefaultUnset.Add(F.CppName); continue; }
			ValueFromJson(F, V, St.Defaults);
		}
	}
	else
	{
		// Записи ещё нет: дефолт = то, что действует сейчас по наследству
		St.Defaults = St.Work;
		if (!bEm)
		{
			St.DefaultUnset.Add(GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, bOverrideEmissive));
			St.DefaultUnset.Add(GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, EmissiveBoost));
		}
	}
	return States.Add(Key, MoveTemp(St));
}

// ── правки ──────────────────────────────────────────────────────────────────
void FAsdaFxGlowConfigModel::SetField(const FAsdaGlowTarget& T, const FAsdaGlowField& F, const FAsdaFxLightSpec& Src)
{
	// ★ Ревизия Ф11 (С4): поле «только для чтения» не пишется и в таблицу — виджет его уже гасит,
	//   но запрет обязан стоять в СЛОЕ ДАННЫХ: иначе следующий виджет снова обойдёт его молча
	if (!F.Prop || F.bReadOnly) { return; }
	FAsdaGlowEditState& St = GetState(T);
	F.Prop->CopyCompleteValue_InContainer(&St.Work, &Src);
	St.bPendingRemove = false;
	St.PendingSet.Add(F.CppName);
	St.PendingReset.Remove(F.CppName);
	static const FName NBoost = GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, EmissiveBoost);
	static const FName NOverride = GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, bOverrideEmissive);
	static const FName NColor = GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, ColorSRGB);
	static const FName NColorMode = GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, ColorMode);
	// Сила свечения без галочки «задать» не действует — число её взводит (как подсказка поля в панели деталей)
	if (F.CppName == NBoost && !St.Work.bOverrideEmissive)
	{
		St.Work.bOverrideEmissive = true;
		St.PendingSet.Add(NOverride);
		St.PendingReset.Remove(NOverride);
	}
	// Включение галочки подхватывает текущую эмиссию, чтобы картинка не прыгнула на 1.0
	if (F.CppName == NOverride && St.Work.bOverrideEmissive && St.DefaultUnset.Contains(NBoost) && !St.PendingSet.Contains(NBoost))
	{
		// ★ Ревизия 13.09 (п.5): текущая эмиссия — то, чем эффект рисуется СЕЙЧАС: EmissiveBoost материала ассета
		//   (у семейства — его импортированного примера), иначе правило asda_fx_boost.json. Без этого включение
		//   галочки роняло яркость семейства на 1.0 в тот момент, когда владелец только начал настройку
		St.Work.EmissiveBoost = CurrentMaterialBoost(T);
		St.PendingSet.Add(NBoost);
	}
	// Явный цвет без режима «Явный» не применяется — тот же автоперевод, что asda.Fx.Glow.AutoFixedColor у актора
	if (F.CppName == NColor && St.Work.ColorMode != EAsdaFxLightColorMode::Fixed)
	{
		St.Work.ColorMode = EAsdaFxLightColorMode::Fixed;
		St.PendingSet.Add(NColorMode);
		St.PendingReset.Remove(NColorMode);
	}
	PushLive(St);
}

void FAsdaFxGlowConfigModel::ResetField(const FAsdaGlowTarget& T, const FAsdaGlowField& F)
{
	if (!F.Prop || F.bReadOnly) { return; }   // ★ ревизия Ф11 (С4): см. довод в SetField
	FAsdaGlowEditState& St = GetState(T);
	static const FName NBoost = GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, EmissiveBoost);
	static const FName NOverride = GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, bOverrideEmissive);
	auto ResetOne = [&St](const FAsdaGlowField& X)
	{
		X.Prop->CopyCompleteValue_InContainer(&St.Work, &St.Defaults);
		St.PendingSet.Remove(X.CppName);
		if (St.SavedOwn.Contains(X.CppName)) { St.PendingReset.Add(X.CppName); }
	};
	ResetOne(F);
	// Пара «галочка + число» силы свечения сбрасывается вместе, если у дефолта её нет вовсе
	if ((F.CppName == NBoost || F.CppName == NOverride) && St.DefaultUnset.Contains(F.CppName))
	{
		for (const FAsdaGlowField& X : Fields)
		{
			if (X.Prop && (X.CppName == NBoost || X.CppName == NOverride)) { ResetOne(X); }
		}
		St.Work.bOverrideEmissive = false;
	}
	PushLive(St);
}

void FAsdaFxGlowConfigModel::RemoveOverride(const FAsdaGlowTarget& T)
{
	if (T.bFamily) { return; }
	FAsdaGlowEditState& St = GetState(T);
	St.PendingSet.Reset();
	St.PendingReset.Reset();
	St.bPendingRemove = St.bExistsSaved;
	St.Work = St.Defaults;
	if (St.DefaultUnset.Contains(GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, bOverrideEmissive))) { St.Work.bOverrideEmissive = false; }
	PushLive(St);
}

void FAsdaFxGlowConfigModel::PushLive(FAsdaGlowEditState& St)
{
	UAsdaFxLightSettings* S = GetMutableDefault<UAsdaFxLightSettings>();
	const FAsdaGlowTarget& T = St.Target;
	if (T.bFamily)
	{
		if (FAsdaFxLightRule* R = S->Families.FindByPredicate([&T](const FAsdaFxLightRule& X) { return X.Id == T.FamilyId; }))
		{
			const FString Label = R->Spec.FamilyId;
			R->Spec = St.Work;
			R->Spec.FamilyId = Label;
			R->Spec.bManual = false;
		}
	}
	else
	{
		TSet<FName> Own;
		if (!St.bPendingRemove)
		{
			for (const FAsdaGlowField& F : Fields) { if (St.IsOwn(F.CppName)) { Own.Add(F.CppName); } }
		}
		ComposeOverride(T, Own, St.Work);
	}
	// ★ Ревизия 13.09 (п.4): зависимые записи пересводятся сразу — иначе до сохранения видно одно, после другое
	RecomposeDependents(T);
	UAsdaFxLightSubsystem::NotifyGlowTablesChanged();
}

// ── ★ Ревизия 13.09 (п.4): сведение записей overrides в CDO — та же цепочка, что у генератора ─────────────
FAsdaFxLightSpec FAsdaFxGlowConfigModel::InheritedBase(const FString& Jeb, int32 ItemId, bool& bOutLight, bool& bOutEmissive) const
{
	const UAsdaFxLightSettings* S = UAsdaFxLightSettings::Get();
	bOutLight = false;
	bOutEmissive = false;
	if (ItemId != 0)
	{
		for (const FAsdaFxGlowOverride& O : S->Overrides)
		{
			if (O.ItemId == 0 && O.Jeb.Equals(Jeb, ESearchCase::IgnoreCase))
			{
				bOutLight = O.bLight;
				bOutEmissive = O.Spec.bOverrideEmissive;
				return O.Spec;
			}
		}
	}
	// Семейство: сила свечения семейства переопределением НЕ наследуется (у неё свой уровень ниже замка ассета)
	const FAsdaFxLightRule* R = S->FindRule(Jeb);
	FAsdaFxLightSpec Out = R ? R->Spec : FAsdaFxLightSpec();
	Out.bOverrideEmissive = false;
	return Out;
}

TSet<FName> FAsdaFxGlowConfigModel::OwnFieldsOf(const FAsdaGlowTarget& T) const
{
	TSet<FName> Own;
	if (const FAsdaGlowEditState* St = States.Find(T.Key()))
	{
		if (!St->bPendingRemove)
		{
			for (const FAsdaGlowField& F : Fields) { if (St->IsOwn(F.CppName)) { Own.Add(F.CppName); } }
		}
		return Own;
	}
	if (const FExportEntry* E = Export.Find(T.Key())) { return E->Own; }
	return Own;
}

void FAsdaFxGlowConfigModel::ComposeOverride(const FAsdaGlowTarget& T, const TSet<FName>& Own, const FAsdaFxLightSpec& OwnValues)
{
	static const FName NBoost = GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, EmissiveBoost);
	static const FName NOverride = GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, bOverrideEmissive);
	UAsdaFxLightSettings* S = GetMutableDefault<UAsdaFxLightSettings>();
	const int32 Idx = UAsdaFxLightSettings::IndexOfOverride(S->Overrides, T.Jeb, T.ItemId);   // ★ волна 3 п.16 (15.09.2026)
	if (Own.Num() == 0)
	{
		// Своих ключей нет — записи нет (генератор пустую запись не пишет)
		if (Idx != INDEX_NONE) { S->Overrides.RemoveAt(Idx); }
		return;
	}
	bool bLight = false, bBaseEm = false;
	FAsdaFxLightSpec Spec = InheritedBase(T.Jeb, T.ItemId, bLight, bBaseEm);
	for (const FAsdaGlowField& F : Fields)
	{
		if (!F.Prop || !Own.Contains(F.CppName)) { continue; }
		F.Prop->CopyCompleteValue_InContainer(&Spec, &OwnValues);
		if (F.CppName != NBoost && F.CppName != NOverride) { bLight = true; }   // свой ключ света ⇒ запись переопределяет свет
	}
	// Эмиссия: своя галочка — как задана; своё число без галочки её взводит (как у генератора); иначе — от основы
	if (Own.Contains(NOverride))   { Spec.bOverrideEmissive = OwnValues.bOverrideEmissive; }
	else if (Own.Contains(NBoost)) { Spec.bOverrideEmissive = true; }
	else                           { Spec.bOverrideEmissive = bBaseEm; }
	Spec.bManual = false;
	Spec.FamilyId = FString::Printf(TEXT("%s/%s"), *FamilyOfJeb(T.Jeb), T.ItemId ? *FString::Printf(TEXT("item%d"), T.ItemId) : TEXT("effect"));
	FAsdaFxGlowOverride& Ex = (Idx != INDEX_NONE) ? S->Overrides[Idx] : S->Overrides.AddDefaulted_GetRef();
	Ex.Jeb = T.Jeb;
	Ex.ItemId = T.ItemId;
	Ex.bLight = bLight;
	Ex.Spec = Spec;
}

void FAsdaFxGlowConfigModel::RecomposeDependents(const FAsdaGlowTarget& Changed)
{
	UAsdaFxLightSettings* S = GetMutableDefault<UAsdaFxLightSettings>();
	// Кого пересводить: у семейства — все записи его эффектов (сначала записи эффектов, потом предметов),
	//   у записи эффекта — записи его предметов. Запись предмета зависимых не имеет
	TArray<FAsdaGlowTarget> Deps;
	auto AddDep = [&Deps](const FString& Jeb, int32 Item)
	{
		if (Deps.ContainsByPredicate([&](const FAsdaGlowTarget& D) { return D.ItemId == Item && D.Jeb.Equals(Jeb, ESearchCase::IgnoreCase); })) { return; }
		FAsdaGlowTarget D; D.Jeb = Jeb; D.ItemId = Item; Deps.Add(D);
	};
	auto Matches = [this, &Changed](const FString& Jeb, int32 Item)
	{
		if (Changed.bFamily) { return FamilyOfJeb(Jeb) == Changed.FamilyId; }
		return Changed.ItemId == 0 && Item != 0 && Jeb.Equals(Changed.Jeb, ESearchCase::IgnoreCase);
	};
	if (!Changed.bFamily && Changed.ItemId != 0) { return; }
	for (const FAsdaFxGlowOverride& O : S->Overrides) { if (Matches(O.Jeb, O.ItemId)) { AddDep(O.Jeb, O.ItemId); } }
	// Состояния правки без записи в CDO тоже зависят от основы (их «по умолчанию» и унаследованные значения)
	for (const TPair<FString, FAsdaGlowEditState>& P : States)
	{
		const FAsdaGlowTarget& D = P.Value.Target;
		if (!D.bFamily && Matches(D.Jeb, D.ItemId)) { AddDep(D.Jeb, D.ItemId); }
	}
	Deps.StableSort([](const FAsdaGlowTarget& A, const FAsdaGlowTarget& B) { return (A.ItemId != 0) < (B.ItemId != 0); });

	static const FName NBoost = GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, EmissiveBoost);
	static const FName NOverride = GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, bOverrideEmissive);
	for (const FAsdaGlowTarget& D : Deps)
	{
		const TSet<FName> Own = OwnFieldsOf(D);
		bool bBaseLight = false, bBaseEm = false;
		const FAsdaFxLightSpec Base = InheritedBase(D.Jeb, D.ItemId, bBaseLight, bBaseEm);
		FAsdaFxLightSpec Values = Base;
		if (FAsdaGlowEditState* DSt = States.Find(D.Key()))
		{
			// Унаследованные значения и «по умолчанию» состояния — от новой основы; свои поля не трогаются
			for (const FAsdaGlowField& F : Fields)
			{
				if (F.Prop && !Own.Contains(F.CppName)) { F.Prop->CopyCompleteValue_InContainer(&DSt->Work, &Base); }
			}
			DSt->Defaults = Base;
			DSt->DefaultUnset.Remove(NBoost);
			DSt->DefaultUnset.Remove(NOverride);
			if (!bBaseEm) { DSt->DefaultUnset.Add(NBoost); DSt->DefaultUnset.Add(NOverride); }
			Values = DSt->Work;
		}
		else if (const int32 OvIdx = UAsdaFxLightSettings::IndexOfOverride(S->Overrides, D.Jeb, D.ItemId); OvIdx != INDEX_NONE)   // ★ волна 3 п.16 (15.09.2026)
		{
			Values = S->Overrides[OvIdx].Spec;   // свои поля записи лежат в её же числах
		}
		ComposeOverride(D, Own, Values);
	}
}

bool FAsdaFxGlowConfigModel::HasSaved(const FAsdaGlowTarget& T) const
{
	return Export.Contains(T.Key());
}

// ★ Волна Р1 (п.8): состояние правки заводится лениво — у цели, к которой не прикасались, его просто нет
bool FAsdaFxGlowConfigModel::HasPending(const FAsdaGlowTarget& T) const
{
	const FAsdaGlowEditState* St = States.Find(T.Key());
	return St && St->IsDirty();
}

// ═══ ★★★ ВОЛНА Р1 (п.7): ПОЭМИТТЕРНОЕ СВЕЧЕНИЕ — ПРАВКА ИДЁТ В CDO АССЕТА ═══════════════════════════
//   ★★★ ПОЧЕМУ ЭТО НЕ ПОПАДАЕТ В «СОХРАНИТЬ». Кнопка «Сохранить» пишет Tools/asda_fx_light.json и
//   Config/DefaultGame.ini. Запись таблицы переопределений адресуется ключом «имя .jeb + код предмета»
//   (FAsdaFxGlowOverride, World/AsdaFxLightSettings.h) и номера эмиттера не содержит вовсе — значит
//   поэмиттерные числа туда физически не ложатся. Их место — АССЕТ эффекта, и путь до диска у них
//   штатный редакторский: правка → пакет помечен изменённым → владелец сохраняет ассет сам.
//   ★ Молча помечать пакет изменённым нельзя (закон owner-asset-edits-never-lose), поэтому пометка —
//     ОТДЕЛЬНАЯ кнопка, а до неё правка живёт только в памяти редактора и видна на сцене и в PIE.
AAsdaEffectActor* FAsdaFxGlowConfigModel::EmitterCdo(const FAsdaGlowEmitterRef& R) const
{
	if (!R.IsValid()) { return nullptr; }
	UClass* Cls = LoadClass<AActor>(nullptr, *R.ClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	AAsdaEffectActor* Cdo = Cls ? Cast<AAsdaEffectActor>(Cls->GetDefaultObject()) : nullptr;
	return (Cdo && Cdo->Emitters.IsValidIndex(R.Index)) ? Cdo : nullptr;
}

EAsdaFxEmitterGlowSource FAsdaFxGlowConfigModel::EmitterSource(const FAsdaGlowEmitterRef& R) const
{
	const AAsdaEffectActor* Cdo = EmitterCdo(R);
	return Cdo ? Cdo->Emitters[R.Index].Glow.Source : EAsdaFxEmitterGlowSource::FromEffect;
}

FAsdaFxLightSpec FAsdaFxGlowConfigModel::EmitterInherited(const FAsdaGlowEmitterRef& R) const
{
	const AAsdaEffectActor* Cdo = EmitterCdo(R);
	return Cdo ? Cdo->FxLight : FAsdaFxLightSpec();
}

FAsdaFxLightSpec FAsdaFxGlowConfigModel::EmitterWork(const FAsdaGlowEmitterRef& R) const
{
	const AAsdaEffectActor* Cdo = EmitterCdo(R);
	if (!Cdo) { return FAsdaFxLightSpec(); }
	const FAsdaFxEmitterGlow& G = Cdo->Emitters[R.Index].Glow;
	// «Задано вручную» — свои числа; в остальных состояниях эмиттер светит числами эффекта, их и показываем
	return G.Source == EAsdaFxEmitterGlowSource::Manual ? G.Spec : Cdo->FxLight;
}

// ═══ ★★★ РЕВИЗИЯ Ф12 (С1): АДРЕС ЛИСТОВОГО СВОЙСТВА ВНУТРИ FAsdaFxEmitterGlow ═════════════════════
//   Движок раздаёт ОДНО правимое свойство. У нас правка приходит структурой целиком (поле числа,
//   переключатель «откуда числа», «вернуть как в ассете»), поэтому сначала выясняется, какие ЛИСТЫ
//   она изменила, — и дальше каждый лист живёт своей жизнью, как у движка.
//   Путь — цепочка свойств от корня структуры: [Spec, IntensityCd] или [Source]. Глубина не
//   ограничена: FAsdaFxLightSpec правит соседняя волна, и вложенная структура в нём появиться может.
struct FAsdaGlowLeafPath
{
	TArray<const FProperty*> Chain;   // от корня к листу; последний элемент — сам лист
};

// Дойти от контейнера-корня до контейнера ЛИСТА (то есть до структуры, которой лист принадлежит)
static const void* AsdaGlowLeafOwner(const FAsdaGlowLeafPath& L, const void* Root)
{
	const void* P = Root;
	for (int32 i = 0; P && i + 1 < L.Chain.Num(); ++i) { P = L.Chain[i]->ContainerPtrToValuePtr<void>(P); }
	return P;
}
static void* AsdaGlowLeafOwnerMutable(const FAsdaGlowLeafPath& L, void* Root)
{
	void* P = Root;
	for (int32 i = 0; P && i + 1 < L.Chain.Num(); ++i) { P = L.Chain[i]->ContainerPtrToValuePtr<void>(P); }
	return P;
}

// Одинаков ли лист у двух корней. Сравнение самим свойством (Identical_InContainer), а не байтами
// и не текстом: у FString и FLinearColor байты врут, а текст зависит от точности печати числа.
// ═══ ★ Ф13 (МЕЛК): ЧЕМ ЭТО ОТЛИЧАЕТСЯ ОТ ДВИЖКА — И ПОЧЕМУ ВЫБРАНО ИМЕННО ТАК ═════════════════════
//   Панель деталей сравнивает ТЕКСТОМ: ExportText_Direct(..., PPF_ForDiff) и Equals с прежним
//   значением (PropertyNode.cpp:4296-4302). Текст — это печать значения, и она ТЕРЯЕТ разряды:
//   у float печатается ограниченное число знаков, поэтому два РАЗНЫХ числа, различающихся в
//   отброшенных разрядах, движок признаёт одинаковыми — и раздаёт экземпляру правку поверх числа,
//   которое владелец задал ему сам. Identical_InContainer зовёт правило сравнения самого свойства
//   (для чисел — operator==), то есть строже: раздаём только туда, где значение и вправду прежнее.
//   ОБРАТНОГО РИСКА НЕТ: строже — значит «не тронули лишнего». Единственное, что мы при этом
//   теряем, — раздача экземпляру, у которого число отличается в неразличимых глазом разрядах; он
//   останется со своим, и это честнее, чем молча его переписать.
//   ★ Сверка обоих правил на живых данных — в самопроверке (asda.Fx.Glow.SelfTest): при 15 416
//   сверках расхождений между старым и новым вычислителем условий 0 (файл report.txt волны).
static bool AsdaGlowLeafIdentical(const FAsdaGlowLeafPath& L, const void* RootA, const void* RootB)
{
	if (L.Chain.Num() == 0) { return true; }
	const FProperty* Leaf = L.Chain.Last();
	const void* A = AsdaGlowLeafOwner(L, RootA);
	const void* B = AsdaGlowLeafOwner(L, RootB);
	if (!A || !B) { return false; }
	for (int32 k = 0; k < Leaf->ArrayDim; ++k)   // ArrayDim > 1 — статический массив вида float X[3]
	{
		if (!Leaf->Identical_InContainer(A, B, k)) { return false; }
	}
	return true;
}

// Перенести лист из одного корня в другой
static void AsdaGlowLeafCopy(const FAsdaGlowLeafPath& L, void* RootDst, const void* RootSrc)
{
	if (L.Chain.Num() == 0) { return; }
	const FProperty* Leaf = L.Chain.Last();
	void* D = AsdaGlowLeafOwnerMutable(L, RootDst);
	const void* S = AsdaGlowLeafOwner(L, RootSrc);
	if (!D || !S) { return; }
	Leaf->CopyCompleteValue_InContainer(D, S);
}

// Собрать листы, различающиеся у двух значений структуры. Вглубь идём ТОЛЬКО через структуры:
// массив, карта и объектная ссылка — это лист целиком (их движок тоже раздаёт целиком)
static void AsdaGlowCollectChangedLeaves(const UStruct* Struct, const void* A, const void* B,
	TArray<FAsdaGlowLeafPath>& Out, TArray<const FProperty*>* Prefix = nullptr)
{
	TArray<const FProperty*> Local;
	TArray<const FProperty*>& Path = Prefix ? *Prefix : Local;
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		const FProperty* P = *It;
		Path.Add(P);
		const FStructProperty* SP = CastField<FStructProperty>(P);
		// ★★★ Ф13 (МЕЛК): У СТРУКТУРЫ СО СВОИМ Identical ВГЛУБЬ НЕ ИДЁМ. Рекурсия по всем полям
		//   теряет СОБСТВЕННОЕ правило сравнения структуры: у FGuid, FDateTime, FSoftObjectPath и
		//   прочих с STRUCT_IdenticalNative «равно» определяет их оператор, а не совпадение полей
		//   (для FSoftObjectPath, например, сравнение нечувствительно к регистру). Разошедшись с
		//   ним, мы бы раздавали экземплярам части структуры — то есть собирали бы у них состояние,
		//   которого не было ни в CDO, ни у владельца. Такая структура — ЛИСТ целиком, ровно как
		//   массив и карта ниже. Наличие своего правила спрашиваем у движка: UScriptStruct::
		//   ICppStructOps::HasIdentical() (Class.h), тот же признак читает FProperty::Identical.
		const bool bOwnIdentical = SP && SP->Struct && SP->Struct->GetCppStructOps() && SP->Struct->GetCppStructOps()->HasIdentical();
		if (SP && SP->Struct && P->ArrayDim == 1 && !bOwnIdentical)
		{
			AsdaGlowCollectChangedLeaves(SP->Struct, P->ContainerPtrToValuePtr<void>(A), P->ContainerPtrToValuePtr<void>(B), Out, &Path);
		}
		else
		{
			// Сравниваем ЗДЕСЬ, на своём уровне вложенности (A и B — контейнеры этого уровня),
			// а в Out кладём АБСОЛЮТНЫЙ путь от корня FAsdaFxEmitterGlow — по нему лист потом
			// адресуется и у экземпляра
			bool bSame = true;
			for (int32 k = 0; k < P->ArrayDim; ++k)
			{
				if (!P->Identical_InContainer(A, B, k)) { bSame = false; break; }
			}
			if (!bSame)
			{
				FAsdaGlowLeafPath L;
				L.Chain = Path;
				Out.Add(MoveTemp(L));
			}
		}
		Path.RemoveAt(Path.Num() - 1);
	}
}

// ═══ ★★★ РЕВИЗИЯ Ф11 (К1 и К2, 20.09.2026): ПРАВКА ЭМИТТЕРА — ШТАТНЫМ РЕДАКТОРСКИМ ПУТЁМ ══════════
//   ДВЕ БЕДЫ, КОТОРЫЕ ЭТО ЛЕЧИТ.
//   К1 «правка не доезжает». Числа эмиттера лежат в CDO ассета, а рантайм читает их У АКТОРА
//     (UAsdaFxLightSubsystem::GatherEmitters → Fx->Emitters[i].Glow у актора из TActorIterator;
//     долговечная копия AAsdaEffectActor::EmitterGlowSaved тоже инстансная). Обычная запись в CDO
//     значения в уже размещённые экземпляры НЕ раздаёт — обещание «правка видна сразу (окно уровня,
//     PIE)» было ложным для всего, что уже стоит на карте.
//     ЛЕЧЕНИЕ — тот же механизм, которым пользуется панель деталей: FPropertyNode::PropagatePropertyChange
//     (PropertyNode.cpp:4188-4308) собирает GetArchetypeInstances() и переносит новое значение ТОЛЬКО
//     тем экземплярам, у которых сейчас лежит СТАРОЕ значение CDO. Экземпляр, которому владелец задал
//     своё число поштучно, остаётся нетронутым. Сама функция живёт в приватном заголовке модуля
//     PropertyEditor (PropertyNode.h), поэтому здесь повторён её ЗАКОН, а не вызов.
//   К2 «тихое впечатывание в карты». Экземпляр сериализуется ДЕЛЬТОЙ против CDO. Пока правка CDO
//     живёт в памяти, а экземпляры держат прежние числа, СОХРАНЕНИЕ ЛЮБОЙ КАРТЫ впечатало бы прежние
//     значения в .umap явными переопределениями — молча и необратимо. Раздача (К1) это и закрывает:
//     после неё экземпляр СОВПАДАЕТ с CDO, и дельте взяться неоткуда.
//   ★ РАЗДАЧА НЕ ЗОВЁТ Modify() У ЭКЗЕМПЛЯРОВ — ровно как движок. Значит, ни одна карта от нашей
//     правки НЕ становится изменённой (в том числе защищённый Login.umap), а изменённым помечается
//     только пакет самого ассета BP_Fx_….
// ═══ ★★★ РЕВИЗИЯ Ф12 (С1, 20.09.2026): РАЗДАЧА ПРИВЕДЕНА К ЗАКОНУ ДВИЖКА ДОСЛОВНО ═════════════════
//   ТРИ ОТСТУПЛЕНИЯ, КОТОРЫЕ ЗДЕСЬ ЗАКРЫТЫ.
//   (а) ★★★ СРАВНИВАЛАСЬ ВСЯ СТРУКТУРА, А ДВИЖОК СРАВНИВАЕТ ОДНО ПРАВИМОЕ СВОЙСТВО.
//       Движок (PropertyNode.cpp:4296-4302) делает ExportText_Direct ИМЕННО правимого свойства и
//       сверяет его с PreviousValue — то есть «экземпляр ещё не трогали ВОТ ЭТИМ полем».
//       Наше сравнение всей FAsdaFxEmitterGlow означало «экземпляр не трогали НИ ОДНИМ полем»:
//       актор, которому владелец поштучно поправил, скажем, цвет, не получал бы правку СИЛЫ —
//       и навсегда оставался отличным от CDO. А отличие от CDO — это дельта, которую сохранение
//       карты впечатает в .umap явным переопределением, то есть ровно та тихая потеря, которую
//       раздача и заводилась предотвращать.
//       ЛЕЧЕНИЕ: считаем СПИСОК ЛИСТОВЫХ СВОЙСТВ, которые правка и вправду изменила (обход структуры
//       вглубь), и дальше каждое из них раздаём ОТДЕЛЬНО по закону движка. Одной правкой полей
//       меняется одно-два, при «вернуть как в ассете» — сколько было.
//       ★ Сравнение — Identical_InContainer самого свойства, а не побайтное и не текстовое: у FString
//       и FLinearColor побайтное дало бы ложное «отличается», а текстовое (как у движка) ещё и
//       зависит от точности печати float.
//   (б) ★ ПРОПУСКАЛИСЬ CDO НАСЛЕДНИКОВ. GetArchetypeInstances у CDO отдаёт все объекты класса и
//       производных, ВКЛЮЧАЯ их CDO (Obj.cpp:1079-1097, комментарий «we need to evaluate CDOs as
//       well»), а наш цикл их отбрасывал по RF_ClassDefaultObject. Движок вместо этого идёт
//       ОЧЕРЕДЬЮ от архетипа к его потомкам (PropertyNode.cpp:4258-4266, 4310-4317): сначала CDO
//       наследника, потом экземпляры, чей архетип — этот CDO. Здесь сделано так же.
//   (в) ★ ЭКЗЕМПЛЯРЫ В НЕОТКРЫТЫХ КАРТАХ НЕ ДОСТАЁТ НИКТО — ни мы, ни панель деталей движка.
//       Это не дефект, а граница механизма: GetArchetypeInstances видит только ЗАГРУЖЕННЫЕ объекты.
//       Актор в незагруженной карте получит новое значение при её следующем открытии — потому что
//       его прежнее значение совпадало с прежним CDO и в .umap дельты не записано. Актор, которому
//       значение задавали поштучно, так и останется со своим — как и задумано.
int32 FAsdaFxGlowConfigModel::PropagateEmitterGlow(AAsdaEffectActor* Cdo, int32 Index, const FAsdaFxEmitterGlow& Before)
{
	if (!Cdo || !Cdo->Emitters.IsValidIndex(Index)) { return 0; }
	const FAsdaFxEmitterGlow& After = Cdo->Emitters[Index].Glow;
	UScriptStruct* GlowStruct = FAsdaFxEmitterGlow::StaticStruct();
	// Ничего не изменилось — раздавать нечего (и экземпляры трогать незачем)
	if (GlowStruct->CompareScriptStruct(&Before, &After, PPF_None)) { return 0; }

	// Шаг 1: какие ЛИСТОВЫЕ свойства правка изменила. Путь к листу — цепочка свойств от корня
	// FAsdaFxEmitterGlow (например Spec → IntensityCd), она же служит адресом внутри экземпляра
	TArray<FAsdaGlowLeafPath> Changed;
	AsdaGlowCollectChangedLeaves(GlowStruct, &Before, &After, Changed);
	if (Changed.Num() == 0) { return 0; }

	// Шаг 2: обход архетипной цепочки ОЧЕРЕДЬЮ — ровно как FPropertyNode::PropagatePropertyChange
	TArray<UObject*> Arch, Queue;
	Cdo->GetArchetypeInstances(Arch);   // объекты этого класса и наследников (в т.ч. их CDO), кроме самого Cdo
	Queue.Add(Cdo);
	int32 N = 0;
	while (Queue.Num() > 0)
	{
		UObject* Cur = Queue[0];
		Queue.RemoveAt(0);
		if (Cur != Cdo)
		{
			AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(Cur);
			if (Fx && Fx->Emitters.IsValidIndex(Index))
			{
				FAsdaFxEmitterGlow& Dst = Fx->Emitters[Index].Glow;
				int32 Moved = 0;
				for (const FAsdaGlowLeafPath& L : Changed)
				{
					// ★ ЗАКОН ДВИЖКА, ПОЛЕ ЗА ПОЛЕМ: переносим только туда, где лежит ПРЕЖНЕЕ значение
					//   CDO ИМЕННО ЭТОГО поля. Поле, которое владелец задал этому актору сам, не трогаем
					if (!AsdaGlowLeafIdentical(L, &Dst, &Before)) { continue; }
					AsdaGlowLeafCopy(L, &Dst, &After);
					++Moved;
				}
				if (Moved > 0)
				{
					// Долговечная копия у экземпляра своя: пересобираем её из его же эмиттеров.
					// ★ К1: авторитетно — массив экземпляра сейчас ровно тот, что мы только что задали,
					//   и если настроенных эмиттеров в нём не осталось, копию надо очистить, а не сберечь
					Fx->SaveEmitterGlow(/*bAuthoritative=*/true);
					++N;
				}
			}
		}
		// Потомки текущего звена цепочки: их архетип — именно он
		// ★ Ф13 (МЕЛК): ЧЕМ ЭТО ОТЛИЧАЕТСЯ ОТ ДВИЖКА — И ПОЧЕМУ ОТЛИЧИЕ ДОПУСТИМО.
		//   Панель деталей зовёт FPropertyNode::GetArchetype (PropertyNode.cpp:157-159), а тот —
		//   FObjectArchetypeHelper::GetArchetype(Object, &PropertyEditorPolicy::Get()). Разница с
		//   UObject::GetArchetype() ровно одна: второй зовёт тот же GetArchetypeImpl с политикой
		//   nullptr (UObjectArchetype.cpp:277-285). Политика подменяет архетип только там, где её
		//   кто-то зарегистрировал, — это случаи переинстансирования и превью-объектов редактора
		//   блюпринтов/UMG. У размещённого на карте AAsdaEffectActor и у CDO наследника архетип
		//   от политики не зависит, и результат совпадает.
		//   ★ Позвать движковый вариант мы и не можем: PropertyEditorPolicy живёт в приватной части
		//   модуля PropertyEditor. Это граница, а не недосмотр, и потому она записана здесь.
		for (int32 i = 0; i < Arch.Num(); ++i)
		{
			if (Arch[i] && Arch[i]->GetArchetype() == Cur)
			{
				Queue.Add(Arch[i]);
				Arch.RemoveAt(i--);
			}
		}
	}
	return N;
}

// ═══ ★★★ РЕВИЗИЯ Ф12 (С2): СКЛАД СНИМКОВ «КАК В АССЕТЕ» — ОДИН НА МОДУЛЬ ══════════════════════════
//   Подробности и обоснование — у объявления в AsdaFxGlowConfigModel.h.
// Имя пакета из пути класса: «/Game/…/BP_Fx_x.BP_Fx_x_C» → «/Game/…/BP_Fx_x»
static FString AsdaGlowPackageNameFromClassPath(const FString& ClassPath)
{
	int32 Dot = INDEX_NONE;
	return ClassPath.FindChar(TEXT('.'), Dot) ? ClassPath.Left(Dot) : ClassPath;
}

// ★ Ф13 (К2): идёт ли ПРЯМО СЕЙЧАС наша собственная правка. Пометку «изменён», пришедшую внутри
//   этого счётчика, чужой считать нельзя — её ставит наш же Modify
static int32 GAsdaGlowInsideOwnEdit = 0;

// ★★★ ВОЛНА Ф14 (МЕЛК-4, 20.09.2026): КЛАСС АССЕТА ПО ПУТИ ИЛИ ПО СЛАБОЙ ССЫЛКЕ.
//   Путь класса — ключ склада, но он устаревает при Rename/Move ассета в браузере контента.
//   UObject при переименовании остаётся ТЕМ ЖЕ объектом, поэтому слабая ссылка, снятая при первой
//   правке, переименование и перенос переживает. Сперва пробуем путь (дёшево и точно), затем её.
//   ★ FindObject, а не LoadClass: склад описывает то, что УЖЕ в памяти; грузить ассет ради
//     проверки его состояния нельзя — это лишняя работа и лишний риск на 1283 эффектах.
static UClass* AsdaGlowResolveFxClass(const FString& ClassPath, const TWeakObjectPtr<UClass>& Weak)
{
	if (!ClassPath.IsEmpty())
	{
		if (UClass* ByPath = FindObject<UClass>(nullptr, *ClassPath)) { return ByPath; }
	}
	return Weak.Get();
}

// ═══ ★★★ ВОЛНА Ф15 (С-1, 20.09.2026): ПОИСК ПО КЛЮЧУ ТОЖЕ ОБЯЗАН ПЕРЕЖИВАТЬ RENAME/MOVE ══════════
//   ЧТО БЫЛО НЕ ТАК. Ф14 провела слабую ссылку на класс (МЕЛК-4) только в ПЕРЕБОРЫ склада — там,
//   где запись уже в руках. А одиннадцать мест искали запись ПО КЛЮЧУ `TouchedFx().Find(ClassPath)`,
//   и ключ — это ПУТЬ класса. После Rename/Move ассета дерево конфигуратора отдаёт НОВЫЙ путь
//   (BuildTree грузит классы по реестру, EmitterCdo :1249 — LoadClass по нему же), а в складе лежит
//   СТАРЫЙ. Поиск промахивается, и молча ломается всё сразу:
//     * отметка «*» («не записано») пропадает — IsFxAssetUnsaved не находит записи;
//     * EmitterEditBegin заводит ВТОРУЮ запись и снимает снимок «как в ассете» с уже ПРАВЛЕНОГО
//       CDO — исходное состояние теряется навсегда (на диск мы не пишем, бэкапа ассета нет);
//     * ReleaseFxAssetIfUnchanged не находит записи и признак «изменён» не снимает никогда;
//     * «Вернуть эмиттер как в ассете» перестаёт работать без единого слова.
//   ЛЕЧЕНИЕ. Один вход для всех поисков по ключу: сперва точный путь (дёшево), затем — ЖИВОЙ класс
//   этого пути против слабых ссылок записей склада. Найденная запись ПЕРЕКЛАДЫВАЕТСЯ под нынешний
//   путь: иначе перебор повторялся бы при каждом обращении, а ключ так и остался бы вечно старым.
//   ★ FindObject, а не LoadClass: склад описывает то, что УЖЕ в памяти (см. AsdaGlowResolveFxClass).
// ═══ ★★★ ВОЛНА Ф16 (СРЕДН-2, 20.09.2026): ПРЯМАЯ ВЕТКА ОБЯЗАНА ПРОВЕРЯТЬ, ЧЕЙ ЭТО КЛЮЧ ═══════════
//   ЧТО БЫЛО НЕ ТАК. Ф15 научила поиск переживать Rename/Move, но БЫСТРАЯ ветка `Store.Find(Path)`
//   отдавала запись, ни с чем не сверяясь. Путь класса — ключ ПЕРЕИСПОЛЬЗУЕМЫЙ: стоит ассету A
//   уехать с пути P (Rename/Move), а ассету B — приехать НА P (переименование другого эффекта или
//   новый ассет с прежним именем), и запись A выдаётся для B. Последствия молчаливые и разные:
//   снимок «как в ассете» от A становится базой для B, отметка «не записано» уезжает не на тот
//   ассет, а «Вернуть эмиттер как в ассете» впишет в B числа A.
//   ЛЕЧЕНИЕ ДЕШЁВОЕ. В прямой ветке сверяем слабую ссылку записи с ЖИВЫМ классом этого пути и при
//   расхождении уходим в перебор — то есть в ту же ветку, что и при промахе ключа.
//   ★ Сверяем, только когда сверять ЕСТЬ ЧЕМ: класс по пути не загружен (FindObject == nullptr)
//     или у записи нет слабой ссылки — прямое попадание принимаем, лучшего свидетельства нет.
//     Загружать ассет ради проверки нельзя (см. AsdaGlowResolveFxClass).
template <typename TVal>
static TVal* AsdaGlowFindByPathOrLiveClass(TMap<FString, TVal>& Store, const FString& ClassPath, const TCHAR* StoreName)
{
	if (ClassPath.IsEmpty()) { return Store.Find(ClassPath); }
	UClass* Live = FindObject<UClass>(nullptr, *ClassPath);
	bool bKeyHeldByForeigner = false;   // ключ занят записью УЕХАВШЕГО ассета, освободить не удалось
	if (TVal* Direct = Store.Find(ClassPath))
	{
		UClass* OfRecord = Direct->FxClass.Get();
		if (!Live || !OfRecord || OfRecord == Live) { return Direct; }
		// ★ Ф16 (СРЕДН-2): путь тот же, а ассет ДРУГОЙ. Запись принадлежит уехавшему ассету —
		//   отдавать её нынешнему жильцу пути нельзя. Ключ освобождаем и ищем запись перебором.
		//   ОСВОБОЖДАЕМ ПЕРЕКЛАДЫВАНИЕМ, А НЕ УДАЛЕНИЕМ: у уехавшего ассета это единственный снимок
		//   «как в ассете» (на диск мы не пишем, бэкапа нет) — выбросить его значит потерять данные.
		const FString TruePath = OfRecord->GetPathName();
		if (Store.Contains(TruePath))
		{
			// Собственный путь уехавшего ассета уже занят (цепочка переименований). Ничего не
			// двигаем: данных не теряем, ключ остаётся устаревшим, а нынешнему жильцу пути чужая
			// запись всё равно не достанется — ниже она ищется перебором и заводится заново.
			bKeyHeldByForeigner = true;
			UE_LOG(LogAsdaGlowConfig, Warning, TEXT("[конфигуратор] %s: по пути %s лежит запись ДРУГОГО ассета (%s), а его собственный путь тоже занят — ключ оставлен как есть, запись по нему НЕ отдаётся"),
				StoreName, *ClassPath, *TruePath);
		}
		else
		{
			TVal Evicted = MoveTemp(Store[ClassPath]);
			Store.Remove(ClassPath);
			Store.Add(TruePath, MoveTemp(Evicted));
			UE_LOG(LogAsdaGlowConfig, Warning, TEXT("[конфигуратор] %s: по пути %s лежала запись ДРУГОГО ассета (нынешний жилец пути — %s); запись переложена под собственный путь её ассета %s"),
				StoreName, *ClassPath, Live ? *Live->GetPathName() : TEXT("<не загружен>"), *TruePath);
		}
	}
	if (!Live) { return nullptr; }
	// Сперва НАЙТИ ключ и только потом менять карту: правка TMap внутри её же перебора недопустима
	// ★ Ф16 (СРЕДН-2): перебор берёт ПЕРВУЮ подошедшую запись, поэтому дубли (две записи с одной и
	//   той же слабой ссылкой) сделали бы выбор зависящим от порядка обхода TMap, а он не определён.
	//   Дублей быть не должно — ключ один на класс, — но «не должно» проверяется, а не полагается:
	//   считаем совпадения и, если их больше одного, говорим об этом вслух.
	// ═══ ★★★ ВОЛНА Ф17 (МЕЛК-2, 20.09.2026): ВЫБОР БЫЛ ДЕТЕРМИНИРОВАН НЕ ВСЕГДА ═══════════════════
	//   ЧТО БЫЛО НЕ ТАК — ДВЕ БЕДЫ В ОДНОЙ СТРОКЕ `if (StaleKey.IsEmpty() || P.Key == ClassPath)`.
	//   (1) НЕДЕТЕРМИНИРОВАННОСТЬ. Ф16 закрыла лишь случай «один из ключей равен нынешнему пути».
	//       Если же записей класса две и НИ ОДИН ключ пути не равен (два подряд переименования),
	//       бралась первая по обходу TMap — а порядок обхода зависит от истории вставок и удалений,
	//       то есть два одинаковых сеанса могли вернуть РАЗНЫЕ снимки «как в ассете». Для кнопки
	//       «Вернуть эмиттер как в ассете» это значит вписать владельцу непредсказуемые числа.
	//       ЛЕЧЕНИЕ: порядок задан явно — точное совпадение с нынешним путём сильнее всего, а среди
	//       остальных берётся лексикографически МЕНЬШИЙ ключ (полный порядок на строках, от обхода
	//       не зависит вовсе).
	//   (2) ПУСТОЙ КЛЮЧ ТЕРЯЛСЯ. `StaleKey.IsEmpty()` служила и признаком «ещё не выбран», поэтому
	//       запись, лежащая под ПУСТЫМ ключом, выбиралась, но тут же считалась ненайденной
	//       (`if (StaleKey.IsEmpty()) return nullptr`) — единственный снимок ассета пропадал молча.
	//       ЛЕЧЕНИЕ: отдельный сторож `bStaleKeyFound`, пустая строка — обычный ключ.
	FString StaleKey;
	bool bStaleKeyFound = false;    // ★ Ф17: «ключ выбран» — отдельно от его содержимого
	bool bStaleKeyIsExact = false;  // выбранный ключ РАВЕН нынешнему пути: лучше уже не найти
	int32 Matches = 0;
	for (const TPair<FString, TVal>& P : Store)
	{
		if (P.Value.FxClass.Get() != Live) { continue; }
		++Matches;                  // считаем ВСЕ совпадения, даже когда выбор уже сделан
		if (bStaleKeyIsExact) { continue; }
		const bool bExact = (P.Key == ClassPath);
		if (bExact || !bStaleKeyFound || P.Key < StaleKey)
		{
			StaleKey = P.Key;
			bStaleKeyFound = true;
			bStaleKeyIsExact = bExact;
		}
	}
	if (Matches > 1)
	{
		UE_LOG(LogAsdaGlowConfig, Warning, TEXT("[конфигуратор] %s: у класса %s НЕСКОЛЬКО записей (%d) — взята по ключу «%s» (%s); остальные останутся сиротами, проверьте склад"),
			StoreName, *Live->GetPathName(), Matches, *StaleKey,
			bStaleKeyIsExact ? TEXT("ключ равен нынешнему пути") : TEXT("лексикографически наименьший ключ"));
	}
	if (!bStaleKeyFound) { return nullptr; }
	if (StaleKey == ClassPath) { return Store.Find(StaleKey); }   // ключ уже верный — перекладывать нечего
	if (bKeyHeldByForeigner)
	{
		// ★ Ф16 (СРЕДН-2): нужный ключ занят чужой записью, которую не удалось переложить. Отдаём
		//   ПРАВИЛЬНУЮ запись прямо по её нынешнему (устаревшему) ключу: затирать чужую нельзя, а
		//   перебор всего лишь повторится при следующем обращении — это цена, а не потеря.
		UE_LOG(LogAsdaGlowConfig, Warning, TEXT("[конфигуратор] %s: запись класса %s отдана по старому ключу %s — нынешний путь %s занят чужой записью"),
			StoreName, *Live->GetPathName(), *StaleKey, *ClassPath);
		return Store.Find(StaleKey);
	}
	TVal Moved = MoveTemp(Store[StaleKey]);
	Store.Remove(StaleKey);
	TVal& Fresh = Store.Add(ClassPath, MoveTemp(Moved));
	UE_LOG(LogAsdaGlowConfig, Log, TEXT("[конфигуратор] %s: запись переложена со старого пути %s на нынешний %s (ассет переименован или перенесён)"),
		StoreName, *StaleKey, *ClassPath);
	return &Fresh;
}
// ★ Ф13 (С2): дескрипторы подписок склада — по ним они и снимаются при выгрузке модуля
static FDelegateHandle GAsdaGlowSavedHandle;
static FDelegateHandle GAsdaGlowDirtyHandle;
static FDelegateHandle GAsdaGlowUndoHandle;
// ★ Ф15 (С-2): подписка на «этот эффект участвовал в отмене» — по ней и решается, наша ли транзакция
static FDelegateHandle GAsdaGlowUndoObjHandle;
// Классы CDO, чьи объекты пришли в PostEditUndo текущей отмены/повтора. Набор ЖИВЁТ ОДНО СОБЫТИЕ:
// заполняется по ходу применения транзакции и съедается ближайшей PostUndoRedo.
static TSet<TWeakObjectPtr<UClass>> GAsdaGlowUndoTouchedClasses;

// ═══ ★★★ ВОЛНА Ф16 (МЕЛК-1, 20.09.2026): НАБОР НЕ ДОЛЖЕН СТАРЕТЬ ═════════════════════════════════
//   ЧТО БЫЛО НЕ ТАК. Набор заполнялся в PostEditUndo (по ходу применения транзакции), а съедался
//   ТОЛЬКО в ResyncAfterUndo по FEditorDelegates::PostUndoRedo. Но движок эту рассылку ПРОПУСКАЕТ:
//     * при `Succeeded == false` и при поднятом `bSuspendBroadcastPostUndoRedo`
//       (EditorServer.cpp:1181-1190 — оба условия в UEditorEngine::HandleTransactorRedoUndo);
//     * флаг поднимает не только наш код: его взводит Sequencer
//       (MovieSceneToolHelpers.cpp:4658-4659 и :4903-4904).
//   Тогда набор оставался лежать, и его съедал СЛЕДУЮЩИЙ, посторонний Ctrl+Z — то есть раздача
//   в размещённые экземпляры случалась не в той транзакции, ради защиты от чего Ф15 набор и завела.
//   ЛЕЧЕНИЕ — ГРАНИЦЫ ТРАНЗАКЦИИ ИЗ ПЕРВЫХ РУК. UTransBuffer шлёт три события ВОКРУГ применения
//   транзакции и ни одно из них не зависит ни от Succeeded, ни от bSuspendBroadcastPostUndoRedo
//   (EditorTransaction.cpp:1645-1651 для Undo, :1709-1715 для Redo):
//     OnBeforeRedoUndo — ДО Transaction.Apply(): здесь набор очищается и увеличивается счётчик
//       операции. Значит, набор, не съеденный прошлой отменой, не доживает до PostEditUndo новой;
//     OnUndo / OnRedo  — сразу ПОСЛЕ Apply(): транзакция применена целиком, можно раздавать.
//       Это и есть надёжная замена PostUndoRedo (её подписка оставлена запасной: если она придёт,
//       набор уже съеден и работы не будет).
//   СЧЁТЧИК ОПЕРАЦИИ — ВТОРОЙ РУБЕЖ на случай, если подписаться на буфер не удалось (в коммандлете
//   GEditor->Trans бывает пуст). NotePostEditUndo запоминает номер операции, при которой набор
//   наполнялся; ResyncAfterUndo отказывается работать по набору из ЧУЖОЙ операции.
static int32 GAsdaGlowUndoOpSerial = 0;      // растёт на каждое применение транзакции
static int32 GAsdaGlowUndoSetSerial = -1;    // номер операции, в которую наполнен набор
static FDelegateHandle GAsdaGlowTransBeforeHandle;
static FDelegateHandle GAsdaGlowTransUndoHandle;
static FDelegateHandle GAsdaGlowTransRedoHandle;

// Буфер транзакций редактора; nullptr — редактор без буфера (коммандлет), подписок не будет
static UTransBuffer* AsdaGlowTransBuffer()
{
	return GEditor ? Cast<UTransBuffer>(GEditor->Trans) : nullptr;
}

// ★ Ф13 (С2): подписки ставятся один раз; признак вынесен из тела функции, чтобы ShutdownStatics
//   мог его сбросить — иначе после выгрузки и повторной загрузки модуля склад остался бы без подписок
static bool GAsdaGlowStoreHooked = false;

// ★ Ф15 (МЕЛК-3): подписка на НАЧАЛО пересборки модулей — единственный момент, когда совет
//   «сохраните до пересборки» ещё исполним
static FDelegateHandle GAsdaGlowCompileHandle;

// ★ Ф16 (МЕЛК-4): подписка на «записей-сирот стало больше порога». Игровой модуль своих окон не
//   заводит, поэтому ВИДИМОЕ владельцу сообщение показывает редакторский плагин — здесь.
static FDelegateHandle GAsdaGlowOrphanHandle;

// ★ Ф17 (С-2): подписка на «ключ гейта поднят, а правка человеческая» — тот же приём и та же
//   причина: сказать владельцу видимым сообщением, почему его правка молча откатывается.
static FDelegateHandle GAsdaGlowGateSuspectHandle;

// ═══ ★★★ ВОЛНА Ф15 (МЕЛК-3, 20.09.2026): ПРЕДУПРЕЖДЕНИЕ ВОВРЕМЯ И ПО ДЕЛУ ═════════════════════════
//   ЧТО БЫЛО НЕ ТАК — ДВЕ БЕДЫ СРАЗУ.
//   (1) СРОК. Сообщение уходило из ShutdownStatics, то есть когда модуль УЖЕ выгружается, а совет
//       в нём был «сохраните их штатным Save All ДО пересборки» — в этот момент неисполнимый.
//   (2) СПИСОК. В «под риском» шёл и ассет, у которого пакет не нашёлся вовсе
//       (`if (!Pkg || Pkg->IsDirty())`). Пакет не находится ровно тогда, когда ассет ВЫГРУЖЕН из
//       памяти: терять по нему нечего (CDO нет, правок нет), а владелец видел его в списке
//       «сохраните» и шёл искать несуществующую правку.
//   ЛЕЧЕНИЕ. Один и тот же список показывается ДВАЖДЫ и с РАЗНЫМ текстом:
//     * при начале пересборки модулей (IHotReloadInterface::OnModuleCompilerStarted,
//       HotReloadInterface.h:107-108) — там совет «сохраните сейчас» ещё выполним;
//     * при самой выгрузке — там честно говорится, ЧТО именно уже потеряно и что правки в памяти
//       при этом целы: пакеты остаются помеченными изменёнными, и «Save All» после пересборки их
//       запишет; пропадает только возврат «как в ассете».
//   В список идут ТОЛЬКО ассеты с живым пакетом, помеченным изменённым.
void FAsdaFxGlowConfigModel::WarnSnapshotsAtRisk(bool bBeforeCompile)
{
	TArray<FString> AtRisk;
	for (const TPair<FString, FAsdaGlowFxTouch>& P : TouchedFx())
	{
		const UClass* Cls = AsdaGlowResolveFxClass(P.Key, P.Value.FxClass);
		const UPackage* Pkg = Cls ? Cls->GetOutermost() : nullptr;
		// ★ Ф15: ассет без пакета в память не загружен — терять по нему нечего, в список не идёт
		if (Pkg && Pkg->IsDirty()) { AtRisk.Add(Pkg->GetName()); }
	}
	if (AtRisk.Num() == 0) { return; }
	AtRisk.Sort();
	const FString List = FString::Join(AtRisk, TEXT(", "));
	const FString Msg = bBeforeCompile
		? FString::Printf(TEXT(
			"★ Конфигуратор свечения: началась пересборка модулей.\n\n"
			"Когда модуль выгрузится, снимки «как в ассете» по %d незаписанному ассету пропадут — кнопка «Вернуть эмиттер как в ассете» по ним работать перестанет.\n\n"
			"Сейчас это ещё поправимо: сохраните их штатным «Save All»: %s"), AtRisk.Num(), *List)
		: FString::Printf(TEXT(
			"★ Конфигуратор свечения: модуль ВЫГРУЖАЕТСЯ (полная пересборка плагина или закрытие редактора; "
			"пересборка через Live Coding сюда не приводит — она модуль не выгружает).\n\n"
			"Снимки «как в ассете» по %d незаписанному ассету ПОТЕРЯНЫ — кнопка «Вернуть эмиттер как в ассете» по ним больше не работает.\n\n"
			"Сами правки целы: пакеты остались помеченными изменёнными, и «Save All» запишет их и после пересборки: %s"),
			AtRisk.Num(), *List);
	UE_LOG(LogAsdaGlowConfig, Warning, TEXT("%s"), *Msg);
	// Всплывающее сообщение, а не модальное окно: и пересборка, и выгрузка идут внутри чужого
	// процесса, блокировать его диалогом нельзя
	FNotificationInfo Info(FText::FromString(Msg));
	Info.ExpireDuration = 15.f;
	Info.bFireAndForget = true;
	FSlateNotificationManager::Get().AddNotification(Info);
}

// ═══ ★★★ ВОЛНА Ф13 (К1, 20.09.2026): АВТОСЕЙВ — НЕ ЗАПИСЬ АССЕТА ═════════════════════════════════
//   ЧТО БЫЛО НЕ ТАК. Отсечка стояла одна: `Ctx.IsProceduralSave()`. А процедурным движок считает
//   ТОЛЬКО кук и EditorDomain (SavePackageUtilities.cpp:666-674: `bIsCooking ||
//   (SaveFlags & SAVE_BulkDataByReference)`) — автосейв под неё не попадает. При этом автосейв
//   РЕАЛЬНО шлёт PackageSavedWithContextEvent (SavePackage2.cpp:3670): он идёт обычным SavePackage
//   (FileHelpers.cpp:3628 «OBJ SAVEPACKAGE … AUTOSAVING=true» → EditorServer.cpp:4722
//   `SaveFlags = SAVE_FromAutosave`). Итог: через 10 минут после первой правки (автосейв включён по
//   умолчанию, BaseEditorPerProjectUserSettings.ini:180-186, у владельца не переопределён) снимок
//   выбрасывался, хотя .uasset на диск НЕ писался — автосейв пишет в Saved/Autosaves/ и даже не
//   обновляет путь пакета (SavePackageUtilities.cpp:660). Дальше «Вернуть эмиттер как в ассете»
//   молча ничего не делала (RevertEmitterToAsset выходит по !Base), следующая правка снимала снимок
//   с УЖЕ ИЗМЕНЁННОГО состояния, счётчик «не записано» падал в 0, ассет исчезал из вопроса при
//   закрытии — и в лог уходило ложное «записан на диск».
//   ЛЕЧЕНИЕ. Взят готовый признак движка FObjectPostSaveContext::IsFromAutoSave()
//   (ObjectSaveContext.h:483 — тот же SAVE_FromAutosave, по которому автосейв отличает сам себя).
//   Полная таблица «вид записи → поведение» — у объявления в заголовке.
//   ★ И ВТОРОЕ: снимок теперь не ВЫБРАСЫВАЕТСЯ, а ПЕРЕСНИМАЕТСЯ. Выброс убивал кнопку «Вернуть
//   эмиттер как в ассете» до следующей правки; пересъём оставляет её рабочей и честной —
//   «как в ассете» снова означает «как на диске».
void FAsdaFxGlowConfigModel::HandlePackageSaved(const FString& FileName, UPackage* Pkg, FObjectPostSaveContext Ctx)
{
	if (!Pkg) { return; }
	// Запись не состоялась — на диске всё по-прежнему, трогать нечего
	if (!Ctx.SaveSucceeded()) { return; }
	// Кук и EditorDomain: пишется не .uasset владельца, а производные данные
	if (Ctx.IsProceduralSave()) { return; }
	const FString Saved = Pkg->GetName();
	// ★ Ф14 (МЕЛК-4): имя пакета записи склада берём у ЖИВОГО класса, а не только из ключа. После
	//   Rename/Move ассета ключ хранит старый путь, и сверка по нему молча не находила совпадения —
	//   снимок «как в ассете» не переснимался, то есть после Ctrl+S врал в пользу досавового состояния
	auto TouchPackageName = [](const TPair<FString, FAsdaGlowFxTouch>& P) -> FString
	{
		if (const UClass* Live = AsdaGlowResolveFxClass(P.Key, P.Value.FxClass))
		{
			if (const UPackage* LivePkg = Live->GetOutermost()) { return LivePkg->GetName(); }
		}
		return AsdaGlowPackageNameFromClassPath(P.Key);
	};
	if (Ctx.IsFromAutoSave())
	{
		// ★ К1: АВТОСЕЙВ. Файл ушёл в Saved/Autosaves/, .uasset не тронут. Снимок остаётся прежним;
		//   заодно взводим защёлку под возврат признака «изменён», который автосейв делает сам
		//   сразу после записи (FileHelpers.cpp:3634-3635 «Re-mark the package as dirty»)
		// ★★★ Ф14 (МЕЛК-3): ФЛАГ SAVE_FromAutosave СТАВИТ НЕ ТОЛЬКО АВТОСЕЙВ. Второй источник —
		//   МИГРАЦИЯ ПАКЕТОВ («Migrate…»): AssetTools.cpp, SaveInstancedPackagesIntoDestination,
		//   `SaveArgs.SaveFlags |= SAVE_RehydratePayloads | SAVE_FromAutosave`. Вывод «снимок не
		//   трогать» для неё тот же (пишется чужой проект), а вот ЗАЩЁЛКА ей не нужна и ВРЕДНА:
		//   обратной пометки «изменён» миграция не делает, и защёлка съела бы следующую ЧУЖУЮ
		//   пометку — правку владельца в панели деталей. Отличаем по SAVE_RehydratePayloads:
		//   автосейв его не ставит (EditorServer.cpp:4722 — ровно `SAVE_FromAutosave`).
		const bool bMigration = (Ctx.GetSaveFlags() & SAVE_RehydratePayloads) != 0;
		for (TPair<FString, FAsdaGlowFxTouch>& P : TouchedFx())
		{
			if (TouchPackageName(P) != Saved) { continue; }
			P.Value.bAutoSaveRemarkPending = !bMigration;
			UE_LOG(LogAsdaGlowConfig, Verbose, TEXT("[конфигуратор] %s: АВТОСЕЙВ в %s — .uasset не записан, снимок «как в ассете» сохранён без изменений"),
				*Saved, *FileName);
		}
		return;
	}
	// Настоящая запись владельца (Ctrl+S, Save All, Save As): переснимаем снимок с того, что лежит
	// в CDO прямо сейчас, — на диске теперь ровно это
	for (TPair<FString, FAsdaGlowFxTouch>& P : TouchedFx())
	{
		if (TouchPackageName(P) != Saved) { continue; }
		FAsdaGlowFxTouch& T = P.Value;
		const AAsdaEffectActor* Cdo = nullptr;
		if (UClass* Cls = AsdaGlowResolveFxClass(P.Key, T.FxClass)) { Cdo = Cast<AAsdaEffectActor>(Cls->GetDefaultObject(false)); }
		if (!Cdo) { continue; }
		for (TPair<int32, FAsdaFxEmitterGlow>& B : T.Baseline)
		{
			if (Cdo->Emitters.IsValidIndex(B.Key)) { B.Value = Cdo->Emitters[B.Key].Glow; }
		}
		T.SavedBaseline = Cdo->EmitterGlowSaved;
		T.bSavedBaselineTaken = true;
		// Пакет только что записан, то есть чист: и «был ли изменён до нас», и «трогал ли кто-то ещё»
		// начинают счёт заново — обе отметки говорили о состоянии ДО записи
		T.bWasDirtyBeforeTouch = false;
		T.bForeignDirty = false;
		UE_LOG(LogAsdaGlowConfig, Log, TEXT("[конфигуратор] %s записан на диск (%s) — снимок «как в ассете» переснят с записанного состояния"), *P.Key, *FileName);
	}
}

TMap<FString, FAsdaFxGlowConfigModel::FAsdaGlowFxTouch>& FAsdaFxGlowConfigModel::TouchedFx()
{
	static TMap<FString, FAsdaGlowFxTouch> Store;
	if (!GAsdaGlowStoreHooked)
	{
		GAsdaGlowStoreHooked = true;
		// ★ Ф13 (К1): сам разбор вида записи живёт в HandlePackageSaved — самопроверка зовёт ЕГО ЖЕ
		GAsdaGlowSavedHandle = UPackage::PackageSavedWithContextEvent.AddStatic(&FAsdaFxGlowConfigModel::HandlePackageSaved);

		// ═══ ★★★ ВОЛНА Ф13 (К2): ЧУЖАЯ ПОМЕТКА «ИЗМЕНЁН» ═════════════════════════════════════
		//   Подписка нужна ровно затем, чтобы ReleaseFxAssetIfUnchanged не снимал признак,
		//   который держит НЕ наша правка. PackageMarkedDirtyEvent приходит на каждый
		//   MarkPackageDirty, даже когда пакет уже изменён (UObjectBaseUtility.cpp:273-274) —
		//   значит, правка владельца в панели деталей поверх нашей будет видна.
		//   ★ Во время отмены/повтора движок пометку подавляет (GIsTransacting там же, :257) —
		//   Ctrl+Z владельца чужой правкой не притворится.
		GAsdaGlowDirtyHandle = UPackage::PackageMarkedDirtyEvent.AddLambda(
			[](UPackage* Pkg, bool /*bWasDirty*/)
			{
				if (!Pkg || GAsdaGlowInsideOwnEdit > 0) { return; }   // это наша собственная правка
				const FString Name = Pkg->GetName();
				for (TPair<FString, FAsdaGlowFxTouch>& P : TouchedFx())
				{
					// ★ Ф14 (МЕЛК-4): имя пакета — у ЖИВОГО класса; после Rename/Move ключ устарел
					FString PkgOfTouch = AsdaGlowPackageNameFromClassPath(P.Key);
					if (const UClass* Live = AsdaGlowResolveFxClass(P.Key, P.Value.FxClass))
					{
						if (const UPackage* LivePkg = Live->GetOutermost()) { PkgOfTouch = LivePkg->GetName(); }
					}
					if (PkgOfTouch != Name) { continue; }
					if (P.Value.bAutoSaveRemarkPending)
					{
						// Возврат признака самим автосейвом — не чужая правка
						P.Value.bAutoSaveRemarkPending = false;
						continue;
					}
					if (!P.Value.bForeignDirty)
					{
						UE_LOG(LogAsdaGlowConfig, Log, TEXT("[конфигуратор] %s помечен изменённым ПОМИМО конфигуратора — признак «изменён» мы больше не снимаем (чужие правки не теряем)"), *Name);
					}
					P.Value.bForeignDirty = true;
				}
			});

		// ═══ ★★★ ВОЛНА Ф13 (С3): ПОСЛЕ ОТМЕНЫ — ПЕРЕСИНХРОНИЗАЦИЯ ЭКЗЕМПЛЯРОВ ══════════════════
		//   Обоснование целиком — у объявления ResyncAfterUndo в заголовке. Здесь важно одно:
		//   подписка живёт вместе со складом, а не со вкладкой. Владелец может закрыть вкладку и
		//   нажать Ctrl+Z — дельта в .umap от этого не становится менее опасной.
		GAsdaGlowUndoHandle = FEditorDelegates::PostUndoRedo.AddLambda([]() { FAsdaFxGlowConfigModel::ResyncAfterUndo(); });

		// ═══ ★★★ ВОЛНА Ф15 (С-2, 20.09.2026): ЧЬЯ ЭТО БЫЛА ТРАНЗАКЦИЯ ════════════════════════════
		//   У FEditorDelegates::PostUndoRedo нет ни одного признака отменённого действия, поэтому
		//   пересинхронизация шла по ВСЕМУ складу раздач на КАЖДЫЙ Ctrl+Z — хоть на отмену
		//   передвижения постороннего актора. Сценарий ревизии: правка → «Вернуть как в ассете» →
		//   правка того же эмиттера В ПАНЕЛИ деталей (склад раздач о ней не знает) → произвольная
		//   Ctrl+Z → расхождение с розданным находится → значение CDO разливается по размещённым
		//   экземплярам, то есть в чужие акторы и вне связи с отменяемым действием.
		//   ЛЕЧЕНИЕ. Движок зовёт AAsdaEffectActor::PostEditUndo только у объектов ИМЕННО ЭТОЙ
		//   транзакции (EditorTransaction.cpp:928-957). Подписка копит их пути, а PostUndoRedo,
		//   приходящая уже после применения транзакции целиком, работает ТОЛЬКО по накопленному.
		//   Пустой список ⇒ отменяли не нас ⇒ не трогаем ничего.
		GAsdaGlowUndoObjHandle = AAsdaEffectActor::OnPostEditUndo().AddStatic(&FAsdaFxGlowConfigModel::NotePostEditUndo);

		// ═══ ★★★ ВОЛНА Ф16 (МЕЛК-4): ПЕРЕПОЛНЕНИЕ СКЛАДА СИРОТ — ВИДИМЫМ СООБЩЕНИЕМ ═════════════
		//   Автоматического выброса записей копии больше нет вовсе (разбор — у объявления
		//   kEmitterGlowOrphanCap в AsdaEffectActor.h). Взамен владелец должен УВИДЕТЬ, что копия
		//   растёт, и получить точную команду для уборки. Актор шлёт это ОДИН раз на себя за сеанс,
		//   поэтому перетаскивание ползунка потоком сообщений не обернётся.
		GAsdaGlowOrphanHandle = AAsdaEffectActor::OnEmitterGlowOrphanOverflow().AddLambda(
			[](AAsdaEffectActor* Fx, int32 Orphans, int32 Cap)
			{
				if (!Fx) { return; }
				const FString Msg = FString::Printf(TEXT(
					"★ Свечение эмиттеров: у «%s» в сохранённой копии %d записей без эмиттера-хозяина при пороге %d.\n\n"
					"Ни одна НЕ отброшена — настройки целы. Записи остаются от перепечаток, переименовавших эмиттеры.\n\n"
					"Убрать самые старые (и записать ассет): консоль → asda.Fx.Glow.DropOrphans %s"),
					*Fx->GetName(), Orphans, Cap, *Fx->GetName());
				UE_LOG(LogAsdaGlowConfig, Warning, TEXT("%s"), *Msg);
				FNotificationInfo Info(FText::FromString(Msg));
				Info.ExpireDuration = 15.f;
				Info.bFireAndForget = true;
				FSlateNotificationManager::Get().AddNotification(Info);
			});

		// ═══ ★★★ ВОЛНА Ф17 (С-2): ЗАЛИПШИЙ ГЕЙТ — ВИДИМЫМ СООБЩЕНИЕМ ═════════════════════════════
		//   Разбор — у объявления OnEmittersWriterGateSuspect в AsdaEffectActor.h. Одним Warning-ом
		//   в логе тут не обойтись: симптом («правка в панели не берётся») владелец увидит СРАЗУ, а
		//   в лог посмотрит нескоро — и всё это время будет считать сломанной саму панель.
		//   СКОЛЬКО СООБЩЕНИЙ ЖДАТЬ. Здесь стояло «актор шлёт событие ОДИН раз на себя за сеанс» —
		//   это было верно до волны Ф18 и с тех пор дважды сузилось:
		//     Ф18 — «один раз за ПЕРИОД ЗАЛИПАНИЯ» (признак снимался записью при опущенном ключе);
		//     Ф19 — «один раз за ЭПОХУ КЛЮЧА»: эпоха увеличивается, когда значение
		//           asda.Fx.Glow.EmittersWriter уходит из 1, и только тогда актор вправе сказать
		//           о себе снова (AsdaEffectActor.h, GlowWriterGateSuspectEpoch).
		//   Потока сообщений это по-прежнему не даёт: внутри одной эпохи каждый эффект молчит после
		//   первого раза, а эпоха меняется лишь по явному опусканию ключа — то есть не чаще, чем
		//   отрабатывает сборщик.
		GAsdaGlowGateSuspectHandle = AAsdaEffectActor::OnEmittersWriterGateSuspect().AddLambda(
			[](AAsdaEffectActor* Fx, int32 Gate)
			{
				if (!Fx) { return; }
				const FString Msg = FString::Printf(TEXT(
					"★ Свечение эмиттеров: ключ asda.Fx.Glow.EmittersWriter поднят в %d («пишет скрипт»), "
					"а правка у «%s» пришла в человеческой форме.\n\n"
					"Пока ключ поднят, КАЖДАЯ правка эмиттеров в панели деталей молча откатывается к прежним числам.\n\n"
					"Если сейчас никакой сборщик эффектов не работает, ключ залип — верните его: "
					"консоль → asda.Fx.Glow.EmittersWriter -1"),
					Gate, *Fx->GetName());
				UE_LOG(LogAsdaGlowConfig, Warning, TEXT("%s"), *Msg);
				FNotificationInfo Info(FText::FromString(Msg));
				Info.ExpireDuration = 20.f;   // дольше обычного: без этого совета владелец застрянет
				Info.bFireAndForget = true;
				FSlateNotificationManager::Get().AddNotification(Info);
			});

		// ═══ ★★★ ВОЛНА Ф16 (МЕЛК-1): ГРАНИЦЫ ТРАНЗАКЦИИ — ИЗ БУФЕРА ОТМЕН ════════════════════════
		//   Разбор целиком — у объявления GAsdaGlowUndoOpSerial выше. Коротко: эти три события
		//   приходят ВСЕГДА, когда транзакция и вправду применяется, поэтому набор «наша транзакция»
		//   больше не может пережить своё событие и достаться чужому Ctrl+Z.
		if (UTransBuffer* Trans = AsdaGlowTransBuffer())
		{
			GAsdaGlowTransBeforeHandle = Trans->OnBeforeRedoUndo().AddLambda(
				[](const FTransactionContext& /*Ctx*/) { FAsdaFxGlowConfigModel::BeginUndoOperation(); });
			GAsdaGlowTransUndoHandle = Trans->OnUndo().AddLambda(
				[](const FTransactionContext& /*Ctx*/, bool bSucceeded)
				{
					if (bSucceeded) { FAsdaFxGlowConfigModel::ResyncAfterUndo(); }
					else { GAsdaGlowUndoTouchedClasses.Reset(); GAsdaGlowUndoSetSerial = -1; }
				});
			GAsdaGlowTransRedoHandle = Trans->OnRedo().AddLambda(
				[](const FTransactionContext& /*Ctx*/, bool bSucceeded)
				{
					if (bSucceeded) { FAsdaFxGlowConfigModel::ResyncAfterUndo(); }
					else { GAsdaGlowUndoTouchedClasses.Reset(); GAsdaGlowUndoSetSerial = -1; }
				});
		}
		else
		{
			// Говорим вслух: без буфера работает только запасная подписка PostUndoRedo, а она
			// пропускается движком при Succeeded == false и при bSuspendBroadcastPostUndoRedo
			UE_LOG(LogAsdaGlowConfig, Log, TEXT("[конфигуратор] буфера транзакций нет (GEditor->Trans пуст) — границы отмены берутся только из FEditorDelegates::PostUndoRedo"));
		}

		// ★ Ф15 (МЕЛК-3): предупредить о скорой потере снимков ТОГДА, когда это ещё поправимо, —
		//   в начале пересборки модулей. GetModulePtr, а не LoadModuleChecked: если модуля
		//   пересборки в этой сборке нет (например, Installed Build), предупреждать просто некому,
		//   и тащить ради этого зависимость в Build.cs незачем — интерфейс чисто виртуальный и
		//   объявлен в CoreUObject (Misc/HotReloadInterface.h).
		// ═══ ★★★ ВОЛНА Ф16 (МЕЛК-3, 20.09.2026): ЧЕСТНО ПРО LIVE CODING ══════════════════════════
		//   ЧТО ВЫЯСНИЛОСЬ. `OnModuleCompilerStarted` шлёт РОВНО ОДИН модуль — устаревший HotReload
		//   (HotReload.cpp:673 и :974 — единственные Broadcast во всём движке). Владелец
		//   пересобирает Live Coding'ом, а у ILiveCodingModule такого события нет вовсе: в его
		//   интерфейсе только `GetOnPatchCompleteDelegate` (ILiveCodingModule.h), и тот приходит
		//   УЖЕ ПОСЛЕ применения патча — совет «сохраните до пересборки» по нему неисполним.
		//   И ЭТО НЕ БЕДА: Live Coding НЕ ВЫГРУЖАЕТ игровые модули, он правит код на месте, поэтому
		//   ShutdownStatics не зовётся, склад снимков переживает пересборку и терять нечего.
		//   ⇒ Предупреждение остаётся для ДВУХ реальных случаев: кнопка «Compile» редактора при
		//   выключенном Live Coding (путь HotReload) и настоящая выгрузка модуля. Для Live Coding
		//   случай НЕАКТУАЛЕН — так и записано, гадать про «может не прийти» больше не нужно.
		//   ★ Подписалась ли она вообще — говорим в лог: молчащий механизм неотличим от сломанного.
		if (IHotReloadInterface* HotReload = FModuleManager::GetModulePtr<IHotReloadInterface>(TEXT("HotReload")))
		{
			GAsdaGlowCompileHandle = HotReload->OnModuleCompilerStarted().AddLambda(
				[](bool /*bIsAsyncCompile*/) { FAsdaFxGlowConfigModel::WarnSnapshotsAtRisk(/*bBeforeCompile=*/true); });
			UE_LOG(LogAsdaGlowConfig, Log, TEXT("[конфигуратор] предупреждение «снимки под риском» подписано на HotReload::OnModuleCompilerStarted (кнопка «Compile» при выключенном Live Coding). Пересборка через Live Coding этого события не шлёт и в нём не нуждается: модуль не выгружается, склад снимков цел"));
		}
		else
		{
			UE_LOG(LogAsdaGlowConfig, Log, TEXT("[конфигуратор] модуль HotReload не загружен — предупреждение «снимки под риском» остаётся только на выгрузке модуля. Для Live Coding это не потеря: он модуль не выгружает"));
		}
	}
	return Store;
}

// ═══ ★★★ ВОЛНА Ф14 (С-1, 20.09.2026): СКЛАД РАЗДАЧ — ОТДЕЛЬНЫЙ ОТ СКЛАДА СНИМКОВ ════════════════
//   Обоснование целиком — у объявления FAsdaGlowPushed в заголовке. Коротко: раздача в размещённые
//   экземпляры — СОВЕРШЁННЫЙ факт, и «ассет вернулся к исходному» его не отменяет. Пока склад
//   раздач лежал внутри записи о тронутом ассете, ReleaseFxAssetIfUnchanged уносил его вместе с
//   записью, и после «Вернуть как в ассете» + Ctrl+Z экземпляры оставались с розданным числом.
TMap<FString, FAsdaFxGlowConfigModel::FAsdaGlowPushed>& FAsdaFxGlowConfigModel::PushedFx()
{
	static TMap<FString, FAsdaGlowPushed> Store;
	// ★ Подписки ставит склад СНИМКОВ (TouchedFx) — ходим к нему, чтобы PostUndoRedo была на месте
	//   даже если раздача случилась раньше первого обращения к складу снимков
	TouchedFx();
	return Store;
}

// ★ Ф15 (С-1): ЕДИНСТВЕННЫЙ вход для поиска записи по пути класса. Все обращения «по ключу» идут
//   только сюда — иначе Rename/Move ассета снова начнёт ронять часть из них (см. шапку
//   AsdaGlowFindByPathOrLiveClass).
FAsdaFxGlowConfigModel::FAsdaGlowFxTouch* FAsdaFxGlowConfigModel::FindTouch(const FString& ClassPath)
{
	return AsdaGlowFindByPathOrLiveClass(TouchedFx(), ClassPath, TEXT("склад снимков"));
}

FAsdaFxGlowConfigModel::FAsdaGlowPushed* FAsdaFxGlowConfigModel::FindPushed(const FString& ClassPath)
{
	return AsdaGlowFindByPathOrLiveClass(PushedFx(), ClassPath, TEXT("склад раздач"));
}

// ★ Ф13 (С3): счётчик пересинхронизированных экземпляров за сеанс (в текстах UI — честная цифра)
static int32 GAsdaGlowUndoResynced = 0;
int32 FAsdaFxGlowConfigModel::NumUndoResynced() { return GAsdaGlowUndoResynced; }

// ★ Ф15 (С-2): движок позвал PostEditUndo у эффекта — значит, его объект в ИМЕННО ЭТОЙ транзакции.
//   Запоминаем КЛАСС (а не актор): склад раздач адресуется классом ассета, а в транзакцию может
//   попасть как CDO, так и размещённый экземпляр того же класса.
// ★ Ф16 (МЕЛК-1): «начинается применение транзакции». Отдельной функцией, а не телом лямбды, ровно
//   затем, чтобы самопроверка гоняла ТО ЖЕ САМОЕ, что зовёт буфер отмен: разойдись они — тест
//   проверял бы свою копию закона (грабля Ф14: два экземпляра одного разбора расходятся молча).
void FAsdaFxGlowConfigModel::BeginUndoOperation()
{
	++GAsdaGlowUndoOpSerial;
	GAsdaGlowUndoTouchedClasses.Reset();
	GAsdaGlowUndoSetSerial = -1;
}

int32 FAsdaFxGlowConfigModel::UndoTouchedClassCount() { return GAsdaGlowUndoTouchedClasses.Num(); }

void FAsdaFxGlowConfigModel::NotePostEditUndo(AAsdaEffectActor* Actor)
{
	if (!Actor) { return; }
	// ★ Ф16 (МЕЛК-1): набор помечается номером ИДУЩЕЙ СЕЙЧАС операции. Если он остался от прошлой
	//   (рассылку PostUndoRedo движок пропустил), номера разойдутся и ResyncAfterUndo его не примет
	if (GAsdaGlowUndoSetSerial != GAsdaGlowUndoOpSerial)
	{
		GAsdaGlowUndoTouchedClasses.Reset();
		GAsdaGlowUndoSetSerial = GAsdaGlowUndoOpSerial;
	}
	GAsdaGlowUndoTouchedClasses.Add(Actor->GetClass());
}

int32 FAsdaFxGlowConfigModel::ResyncAfterUndo()
{
	// ★★★ Ф15 (С-2): РАБОТАЕМ ТОЛЬКО ПО ТОМУ, ЧТО И ВПРАВДУ ОТМЕНЯЛИ. Набор заполняет
	//   NotePostEditUndo по ходу применения транзакции; пустой набор означает «отменяли не нас».
	//   Набор съедается здесь, каким бы ни был исход: он описывает ОДНО событие отмены.
	// ★★★ Ф16 (МЕЛК-1): НАБОР ПРИНИМАЕТСЯ ТОЛЬКО ОТ СВОЕЙ ОПЕРАЦИИ. Разбор — у объявления
	//   GAsdaGlowUndoOpSerial. Набор от чужой (несъеденный из-за пропущенной рассылки) выбрасывается
	//   молча не бывает: о нём говорится в лог, иначе «почему ничего не раздалось» не объяснить.
	const int32 SetSerial = GAsdaGlowUndoSetSerial;
	const TSet<TWeakObjectPtr<UClass>> Touched = MoveTemp(GAsdaGlowUndoTouchedClasses);
	GAsdaGlowUndoTouchedClasses.Reset();
	GAsdaGlowUndoSetSerial = -1;
	if (Touched.Num() == 0) { return 0; }
	if (SetSerial != GAsdaGlowUndoOpSerial)
	{
		UE_LOG(LogAsdaGlowConfig, Log, TEXT("[конфигуратор] после отмены/повтора: набор «наша транзакция» (%d класс(ов)) остался от ПРОШЛОЙ операции (№%d против нынешней №%d) — отброшен, в чужие акторы не пишем"),
			Touched.Num(), SetSerial, GAsdaGlowUndoOpSerial);
		return 0;
	}

	int32 Total = 0;
	TArray<FString> Keys;
	// ★★★ Ф14 (С-1): ХОДИМ ПО СКЛАДУ РАЗДАЧ, А НЕ ПО СКЛАДУ СНИМКОВ. Прежде здесь стоял
	//   TouchedFx(), и полный откат («Вернуть эмиттер как в ассете») уносил запись вместе с
	//   розданными числами: Ctrl+Z после него оставлял экземпляры с дельтой к архетипу, а та
	//   впечатывалась в .umap при сохранении карты. Склад раздач переживает удаление записи склада
	//   снимков — ровно потому, что роздано уже РОЗДАНО.
	PushedFx().GetKeys(Keys);
	UScriptStruct* GlowStruct = FAsdaFxEmitterGlow::StaticStruct();
	for (const FString& K : Keys)
	{
		FAsdaGlowPushed* T = PushedFx().Find(K);
		if (!T) { continue; }
		// ★ Ф14 (МЕЛК-4): класс — по пути ИЛИ по слабой ссылке (переименование/перенос ассета)
		UClass* Cls = AsdaGlowResolveFxClass(K, T->FxClass);
		// ★ Ф15 (С-2): ЧИСТКА СКЛАДА ПРИ ОТПУСКАНИИ АССЕТА. Прежде склад раздач не чистился ничем
		//   до выгрузки модуля. Запись об ассете, который уже выгружен из памяти (класс не
		//   разрешается ни путём, ни слабой ссылкой), не описывает ничего: экземпляров его в мире
		//   нет, раздавать нечего и некому. Держать её — только копить мусор и продлевать обход.
		if (!Cls) { PushedFx().Remove(K); continue; }
		if (T->ByEmitter.Num() == 0) { PushedFx().Remove(K); continue; }
		// ★★★ Ф15 (С-2): этот ассет в отменяемой транзакции не участвовал — не трогаем
		if (!Touched.Contains(TWeakObjectPtr<UClass>(Cls))) { continue; }
		AAsdaEffectActor* Cdo = Cast<AAsdaEffectActor>(Cls->GetDefaultObject(false));
		if (!Cdo) { continue; }
		for (TPair<int32, FAsdaFxEmitterGlow>& P : T->ByEmitter)
		{
			if (!Cdo->Emitters.IsValidIndex(P.Key)) { continue; }
			// CDO не менялся — по этому эмиттеру отмены/повтора не было, раздавать нечего
			if (GlowStruct->CompareScriptStruct(&P.Value, &Cdo->Emitters[P.Key].Glow, PPF_None)) { continue; }
			// ★ ЗАКОН ДВИЖКА С ПРАВИЛЬНЫМ «ПРЕЖНИМ»: у экземпляра сейчас лежит то, что раздали МЫ,
			//   — значит именно это и есть его «значение архетипа до правки». Экземпляр, которому
			//   владелец задал своё число, не совпадёт и останется нетронутым
			const int32 N = PropagateEmitterGlow(Cdo, P.Key, P.Value);
			P.Value = Cdo->Emitters[P.Key].Glow;   // теперь у экземпляров лежит ЭТО
			Total += N;
		}
	}
	if (Total > 0)
	{
		GAsdaGlowUndoResynced += Total;
		UAsdaFxLightSubsystem::NotifyGlowTablesChanged();
		UE_LOG(LogAsdaGlowConfig, Log, TEXT("[конфигуратор] после отмены/повтора откат роздан %d размещённым актор(ам) — дельты «экземпляр ≠ архетип» не осталось, сохранение карты ничего не впечатает"), Total);
	}
	return Total;
}

void FAsdaFxGlowConfigModel::ShutdownStatics()
{
	// ★ Ф13 (С2): подписки снимаются по дескриптору. Прежде handle отбрасывался, и при выгрузке
	//   модуля оставался делегат, указывающий в выгруженный код
	if (GAsdaGlowSavedHandle.IsValid())
	{
		UPackage::PackageSavedWithContextEvent.Remove(GAsdaGlowSavedHandle);
		GAsdaGlowSavedHandle.Reset();
	}
	if (GAsdaGlowDirtyHandle.IsValid())
	{
		UPackage::PackageMarkedDirtyEvent.Remove(GAsdaGlowDirtyHandle);
		GAsdaGlowDirtyHandle.Reset();
	}
	if (GAsdaGlowUndoHandle.IsValid())
	{
		FEditorDelegates::PostUndoRedo.Remove(GAsdaGlowUndoHandle);
		GAsdaGlowUndoHandle.Reset();
	}
	// ★ Ф15 (С-2): подписка на PostEditUndo эффектов живёт в ИГРОВОМ модуле и модуль плагина
	//   переживёт — снять её обязательно, иначе делегат укажет в выгруженный код
	if (GAsdaGlowUndoObjHandle.IsValid())
	{
		AAsdaEffectActor::OnPostEditUndo().Remove(GAsdaGlowUndoObjHandle);
		GAsdaGlowUndoObjHandle.Reset();
	}
	// ★ Ф16 (МЕЛК-4): подписка на переполнение склада сирот живёт в ИГРОВОМ модуле — снять
	if (GAsdaGlowOrphanHandle.IsValid())
	{
		AAsdaEffectActor::OnEmitterGlowOrphanOverflow().Remove(GAsdaGlowOrphanHandle);
		GAsdaGlowOrphanHandle.Reset();
	}
	// ★ Ф17 (С-2): подписка на подозрение о залипшем гейте живёт в ИГРОВОМ модуле — снять
	if (GAsdaGlowGateSuspectHandle.IsValid())
	{
		AAsdaEffectActor::OnEmittersWriterGateSuspect().Remove(GAsdaGlowGateSuspectHandle);
		GAsdaGlowGateSuspectHandle.Reset();
	}
	GAsdaGlowUndoTouchedClasses.Reset();
	GAsdaGlowUndoSetSerial = -1;
	// ★ Ф16 (МЕЛК-1): подписки на буфер транзакций живут в UnrealEd и модуль плагина переживут —
	//   снять обязательно, иначе делегат укажет в выгруженный код
	if (UTransBuffer* Trans = AsdaGlowTransBuffer())
	{
		if (GAsdaGlowTransBeforeHandle.IsValid()) { Trans->OnBeforeRedoUndo().Remove(GAsdaGlowTransBeforeHandle); }
		if (GAsdaGlowTransUndoHandle.IsValid())   { Trans->OnUndo().Remove(GAsdaGlowTransUndoHandle); }
		if (GAsdaGlowTransRedoHandle.IsValid())   { Trans->OnRedo().Remove(GAsdaGlowTransRedoHandle); }
	}
	GAsdaGlowTransBeforeHandle.Reset();
	GAsdaGlowTransUndoHandle.Reset();
	GAsdaGlowTransRedoHandle.Reset();
	// ★ Ф15 (С-2): раньше подписки на компилятор здесь не снимались — её ставит TouchedFx()
	if (GAsdaGlowCompileHandle.IsValid())
	{
		if (IHotReloadInterface* HotReload = FModuleManager::GetModulePtr<IHotReloadInterface>(TEXT("HotReload")))
		{
			HotReload->OnModuleCompilerStarted().Remove(GAsdaGlowCompileHandle);
		}
		GAsdaGlowCompileHandle.Reset();
	}
	// ═══ ★★★ ВОЛНА Ф14 (МЕЛК-5, 20.09.2026): ПОТЕРЮ СКЛАДА ГОВОРИМ ВСЛУХ ════════════════════════
	//   Склад снимков без подписок жить не должен: «как в ассете» без пересъёма после записи врёт.
	//   Но его очистка НЕ БЕСПЛАТНА и до этой правки была МОЛЧАЛИВОЙ: после горячей пересборки
	//   плагина (Live Coding, перекомпиляция модуля при открытом редакторе) пакеты остаются
	//   помеченными изменёнными, а вернуть эмиттер «как в ассете» уже нечем — снимка нет.
	//   Владелец узнавал об этом, только нажав кнопку и увидев, что она молчит.
	//   Сохранять склад между загрузками модуля НЕЛЬЗЯ: снимки — это структуры выгружаемого
	//   модуля, и пережить его выгрузку они не могут. Поэтому — честное предупреждение: в лог и
	//   всплывающим сообщением редактора, со списком ассетов, по которым возврат пропадает.
	WarnSnapshotsAtRisk(/*bBeforeCompile=*/false);
	TouchedFx().Empty();
	// ★ Ф14 (С-1): склад раздач — той же судьбы: его структуры тоже принадлежат этому модулю
	PushedFx().Empty();
	GAsdaGlowStoreHooked = false;   // при повторной загрузке модуля подписки ставятся заново
}

// ★ Ревизия Ф12 (К2): удалась ли последняя правке транзакция (т.е. работает ли по ней Ctrl+Z).
//   Не «должна работать по замыслу», а результат живой проверки — см. EmitterEditBegin
static bool GAsdaGlowLastEditUndoable = false;
static int32 GAsdaGlowNonUndoableEdits = 0;

bool FAsdaFxGlowConfigModel::LastEmitterEditUndoable() { return GAsdaGlowLastEditUndoable; }
int32 FAsdaFxGlowConfigModel::NumNonUndoableEmitterEdits() { return GAsdaGlowNonUndoableEdits; }

AAsdaEffectActor* FAsdaFxGlowConfigModel::EmitterEditBegin(const FAsdaGlowEmitterRef& R, FAsdaFxEmitterGlow& OutBefore)
{
	AAsdaEffectActor* Cdo = EmitterCdo(R);
	if (!Cdo) { return nullptr; }
	OutBefore = Cdo->Emitters[R.Index].Glow;
	// ★ К2: снимок «как в ассете» — ОДИН раз на эмиттер, до самой первой правки. Он и есть то, куда
	//   возвращает «Сброс»; второй раз его переписывать нельзя, иначе исходное значение потеряется
	// ★ Ф15 (С-1): сперва ищем запись с учётом Rename/Move, и только если её и вправду нет — заводим
	FAsdaGlowFxTouch* Found = FindTouch(R.ClassPath);
	FAsdaGlowFxTouch& Touch = Found ? *Found : TouchedFx().FindOrAdd(R.ClassPath);
	// ★ Ф14 (МЕЛК-4): слабая ссылка на класс — чтобы склад не потерялся при Rename/Move ассета
	Touch.FxClass = Cdo->GetClass();
	if (!Touch.Baseline.Contains(R.Index))
	{
		Touch.Baseline.Add(R.Index, OutBefore);
		// ★ МЕЛК: запоминаем, был ли пакет изменён ДО нас. Если не был, а правки отменены до
		//   последней, признак «изменён» мы обязаны снять (см. EmitterEditEnd)
		if (Touch.Baseline.Num() == 1)
		{
			const UPackage* Pkg = Cdo->GetOutermost();
			Touch.bWasDirtyBeforeTouch = Pkg && Pkg->IsDirty();
		}
	}
	if (!Touch.bSavedBaselineTaken)
	{
		// ★ К1: снимок долговечной копии — по нему проверяется ПОЛНОТА отката
		Touch.SavedBaseline = Cdo->EmitterGlowSaved;
		Touch.bSavedBaselineTaken = true;
	}
	// ═══ ★★★ РЕВИЗИЯ Ф12 (К2): БЕЗ RF_Transactional ОТМЕНЫ НЕ БЫЛО ВОВСЕ ════════════════════════
	//   ЧТО БЫЛО НЕ ТАК. CDO блюпринта создаётся БЕЗ флага RF_Transactional (Class.cpp:5091),
	//   а UObject::SaveToTransactionBuffer без этого флага не пишет в буфер отмены ничего
	//   (UObjectGlobals.cpp:3215-3229). Транзакция оставалась ПУСТОЙ, а пустую транзакцию буфер
	//   выбрасывает (EditorTransaction.cpp:1333-1337). Итог хуже, чем «отмены нет»: следующее
	//   Ctrl+Z владельца отменяло его ПРЕДЫДУЩЕЕ действие — то, о котором он уже не думал.
	//   А мы при этом обещали Ctrl+Z в четырёх местах текстов.
	//   КАК ЛЕЧИТ ДВИЖОК. Ровно так же — ставит флаг руками перед Modify:
	//   WidgetBlueprintEditorUtils.cpp:132-134 (SetDesiredFocus) и :168-170 (ReplaceDesiredFocus).
	//   ★ ПРОВЕРКА, А НЕ ВЕРА: UObject::Modify возвращает, попал ли объект в буфер отмены. Результат
	//     запоминается и виден самопроверке и текстам карточки — «Ctrl+Z есть» больше не заявление.
	// ═══ ★ Ф13 (МЕЛК): ОСТАЁТСЯ ЛИ RF_Transactional В ЗАПИСАННОМ .uasset — ДА, И ЭТО ВЕРНО ════════
	//   RF_Transactional входит в RF_Load (ObjectMacros.h:643), то есть флаг сериализуется и после
	//   «Save All» остаётся в ЗАПИСАННОМ ассете: перезагрузку редактора он переживает.
	// ★★★ Ф18 (МЕЛК-1): ЗДЕСЬ СТОЯЛО «останется у CDO НАВСЕГДА» — ЭТО НЕВЕРНО, И ТА ЖЕ ОШИБКА БЫЛА
	//   СКОПИРОВАНА В AsdaEffectActor.cpp (уборка сирот). ПЕРЕКОМПИЛЯЦИЯ БЛЮПРИНТА флаг снимает:
	//   прежний CDO уезжает в TRASH_* (KismetCompiler.cpp:666-677), новый создаётся с жёстко
	//   заданным набором RF_Public|RF_ClassDefaultObject|RF_ArchetypeObject (Class.cpp:5091), а
	//   CopyPropertiesForUnrelatedObjects переносит ЗНАЧЕНИЯ свойств, не флаги объекта.
	//   На работоспособность это не влияет: оба места, которым флаг нужен (эта функция и
	//   AAsdaEffectActor::DropOldestGlowOrphans), ставят его НЕПОСРЕДСТВЕННО перед Modify и на его
	//   сохранность не рассчитывают. Неверным было только утверждение в тексте.
	//   Снимать флаг обратно НЕЛЬЗЯ по двум причинам:
	//     * следующий Modify снова вернул бы пустую транзакцию — то есть Ctrl+Z опять съедал бы
	//       предыдущее действие владельца (ровно та беда, которую закрывала ревизия Ф12);
	//     * движок сам ставит этот флаг руками и обратно не снимает — WidgetBlueprintEditorUtils.cpp
	//       :132-134 и :168-170; UPackage::SetDirtyFlag делает то же (Package.cpp:183-184).
	//   Что флаг МЕНЯЕТ в сохранённом ассете: CDO становится участником буфера отмены и после
	//   перезагрузки редактора — то есть Ctrl+Z по нему работает сразу, без первой правки через нас.
	//   Побочных действий у флага нет: на сериализацию данных, на кук и на рантайм он не влияет.
	// ★ Ф13 (К2): пометка «изменён», которую сейчас поставит Modify, — НАША. Счётчик не даёт
	//   подписке на PackageMarkedDirtyEvent принять её за чужую правку владельца
	Cdo->SetFlags(RF_Transactional);
	++GAsdaGlowInsideOwnEdit;
	GAsdaGlowLastEditUndoable = Cdo->Modify();
	--GAsdaGlowInsideOwnEdit;
	if (!GAsdaGlowLastEditUndoable)
	{
		++GAsdaGlowNonUndoableEdits;
		UE_LOG(LogAsdaGlowConfig, Warning, TEXT("[конфигуратор] правка эмиттера %d у %s НЕ попала в буфер отмены: Ctrl+Z её не вернёт (пользуйтесь «Вернуть эмиттер как в ассете»)"),
			R.Index, *R.ClassPath);
	}
	// Пакет ассета помечается изменённым СРАЗУ (это делает сам Modify): редактор больше не даст
	// закрыть себя, не спросив про эти правки. Ассет на диск при этом НЕ пишется — запись остаётся
	// решением владельца (Save All), закон owner-asset-edits-never-lose соблюдён.
	return Cdo;
}

void FAsdaFxGlowConfigModel::EmitterEditEnd(AAsdaEffectActor* Cdo, const FAsdaGlowEmitterRef& R, const FAsdaFxEmitterGlow& Before)
{
	if (!Cdo) { return; }
	// ★ К1: АВТОРИТЕТНО. Массив эмиттеров только что правили МЫ, перепечке между правкой и этим
	//   вызовом взяться неоткуда. Значит, пустой свежий снимок — законно пустой, и долговечную копию
	//   надо очистить вместе с эмиттерами. Иначе откат владельца оставался НЕПОЛНЫМ: числа в
	//   Emitters возвращались, а запись с отменёнными числами оставалась в EmitterGlowSaved и
	//   воскресала при следующей загрузке ассета (RestoreEmitterGlow в PostLoad).
	Cdo->SaveEmitterGlow(/*bAuthoritative=*/true);
	const int32 N = PropagateEmitterGlow(Cdo, R.Index, Before);
	if (N > 0)
	{
		UE_LOG(LogAsdaGlowConfig, Verbose, TEXT("[конфигуратор] правка эмиттера %d у %s роздана %d размещённым актор(ам)"),
			R.Index, *R.ClassPath, N);
	}
	// ★ Ф13 (С3): запоминаем, ЧТО именно теперь лежит у экземпляров. После Ctrl+Z это и будет их
	//   «прежнее значение архетипа», без которого откат раздать по закону движка невозможно.
	// ★★★ Ф14 (С-1): ПИШЕМ В ОТДЕЛЬНЫЙ СКЛАД РАЗДАЧ, а не в запись о тронутом ассете. Прежде это
	//   поле лежало внутри FAsdaGlowFxTouch, и стоявший НИЖЕ ReleaseFxAssetIfUnchanged при полном
	//   откате уносил его вместе со всей записью (TouchedFx().Remove). Цепочка «правка → Вернуть
	//   как в ассете → Ctrl+Z» — путь штатный, его предлагает сам модальный диалог
	//   (SAsdaFxGlowConfigurator.cpp, WarnIfEmitterEditNotUndoable) — оставалась без
	//   пересинхронизации: у экземпляров розданное число, у архетипа откат, дельта в .umap.
	if (Cdo->Emitters.IsValidIndex(R.Index))
	{
		// ★ Ф15 (С-1): то же и для склада раздач
		FAsdaGlowPushed* FoundPush = FindPushed(R.ClassPath);
		FAsdaGlowPushed& Push = FoundPush ? *FoundPush : PushedFx().FindOrAdd(R.ClassPath);
		Push.FxClass = Cdo->GetClass();
		Push.ByEmitter.Add(R.Index, Cdo->Emitters[R.Index].Glow);
	}
	// ★ Ревизия Ф12 (МЕЛК): РАЗНИЦА СОШЛАСЬ В НОЛЬ — ПАКЕТ БОЛЬШЕ НЕ ИЗМЕНЁН. «Вернуть как в ассете»
	//   всё равно проходит через Modify (иначе отмены у самого возврата не было бы), и ассет, в
	//   котором в итоге ничего не изменилось, оставался бы в списке «Save All» и в вопросе при выходе.
	//   Снимаем признак ТОЛЬКО если: все снимки этого ассета совпали с тем, что в нём сейчас,
	//   И до нашей первой правки пакет изменённым не был (чужую пометку трогать нельзя).
	ReleaseFxAssetIfUnchanged(R.ClassPath);
	UAsdaFxLightSubsystem::NotifyGlowTablesChanged();
}

// ★ Ревизия Ф12 (МЕЛК): если ассет вернулся к состоянию снимка — снять признак «изменён» и забыть
//   о нём. Возвращает true, если признак был снят (то есть ассет и вправду вернулся к исходному)
bool FAsdaFxGlowConfigModel::ReleaseFxAssetIfUnchanged(const FString& ClassPath)
{
	FAsdaGlowFxTouch* T = FindTouch(ClassPath);
	if (!T) { return false; }
	const AAsdaEffectActor* Cdo = nullptr;
	// ★ Ф14 (МЕЛК-4): класс — по пути ИЛИ по слабой ссылке. Прежде после Rename/Move ассета функция
	//   молча выходила: признак «изменён» не снимался никогда, ассет навсегда висел в «Save All»
	if (UClass* Cls = AsdaGlowResolveFxClass(ClassPath, T->FxClass)) { Cdo = Cast<AAsdaEffectActor>(Cls->GetDefaultObject(false)); }
	if (!Cdo) { return false; }
	UScriptStruct* GlowStruct = FAsdaFxEmitterGlow::StaticStruct();
	for (const TPair<int32, FAsdaFxEmitterGlow>& P : T->Baseline)
	{
		if (!Cdo->Emitters.IsValidIndex(P.Key)) { return false; }
		if (!GlowStruct->CompareScriptStruct(&P.Value, &Cdo->Emitters[P.Key].Glow, PPF_None)) { return false; }
	}
	// ★ К1: и ДОЛГОВЕЧНАЯ КОПИЯ тоже. Проверять один только массив эмиттеров мало: неполный откат
	//   живёт именно в копии — эмиттеры вернулись, а запись с отменёнными числами осталась и
	//   воскреснет при следующей загрузке ассета
	if (Cdo->EmitterGlowSaved.Num() != T->SavedBaseline.Num()) { return false; }
	UScriptStruct* SavedStruct = FAsdaFxEmitterGlowSaved::StaticStruct();
	for (int32 i = 0; i < T->SavedBaseline.Num(); ++i)
	{
		if (!SavedStruct->CompareScriptStruct(&T->SavedBaseline[i], &Cdo->EmitterGlowSaved[i], PPF_None)) { return false; }
	}
	// ═══ ★★★ ВОЛНА Ф13 (К2, 20.09.2026): ЧУЖОЙ ПРИЗНАК «ИЗМЕНЁН» НЕ СНИМАЕМ НИКОГДА ═════════════
	//   ЧТО БЫЛО НЕ ТАК. Сверялись ТОЛЬКО наши поля (Baseline и SavedBaseline), а «пакет был чистым»
	//   запоминалось ОДИН раз, при первом касании. Сценарий, который предлагает сама карточка
	//   эмиттера («Переключите у САМОГО эффекта поле …», SAsdaFxGlowConfigurator.cpp:610):
	//   правка эмиттера при чистом пакете → владелец правит тот же BP_Fx в панели деталей →
	//   «Вернуть эмиттер как в ассете» → SetDirtyFlag(false) → правки владельца в этом ассете
	//   пропадают из «Save All» и из вопроса при выходе. Молча.
	//   ЛЕЧЕНИЕ. Признак снимается, только если пакет за всё время трогали ИСКЛЮЧИТЕЛЬНО мы: это
	//   видно по подписке на UPackage::PackageMarkedDirtyEvent, которая ловит каждую пометку,
	//   пришедшую не из нашей правки (см. TouchedFx). Сверять состояние всего пакета побайтно мы
	//   не можем — в BP_Fx лежат граф, компоненты и миниатюра, — а вот «трогал ли кто-то ещё»
	//   движок сообщает сам, и этого достаточно.
	//   ★ И ЗАПИСЬ О ТРОНУТОМ АССЕТЕ БОЛЬШЕ НЕ УДАЛЯЕТСЯ, когда признак снять нельзя: по ней
	//   работает «Вернуть эмиттер как в ассете», и терять её из-за чужой правки незачем.
	if (T->bForeignDirty)
	{
		UE_LOG(LogAsdaGlowConfig, Log, TEXT("[конфигуратор] %s вернулся к состоянию ассета, но его правили и ПОМИМО конфигуратора — признак «изменён» ОСТАВЛЕН (чужие правки не теряем)"), *ClassPath);
		return false;
	}
	if (T->bWasDirtyBeforeTouch)
	{
		// Пакет был изменён ещё до нас — пометка не наша, снимать нечего
		return false;
	}
	TouchedFx().Remove(ClassPath);
	if (UPackage* Pkg = Cdo->GetOutermost())
	{
		Pkg->SetDirtyFlag(false);
		UE_LOG(LogAsdaGlowConfig, Log, TEXT("[конфигуратор] %s вернулся к состоянию ассета — признак «изменён» снят"), *Pkg->GetName());
	}
	return true;
}

bool FAsdaFxGlowConfigModel::IsFxAssetForeignDirty(const FString& ClassPath) const
{
	const FAsdaGlowFxTouch* T = FindTouch(ClassPath);
	return T && (T->bForeignDirty || T->bWasDirtyBeforeTouch);
}

int32 FAsdaFxGlowConfigModel::NumFxAssetsForeignDirty() const
{
	int32 N = 0;
	for (const TPair<FString, FAsdaGlowFxTouch>& P : TouchedFx())
	{
		if (P.Value.bForeignDirty || P.Value.bWasDirtyBeforeTouch) { ++N; }
	}
	return N;
}

void FAsdaFxGlowConfigModel::SetEmitterSource(const FAsdaGlowEmitterRef& R, EAsdaFxEmitterGlowSource Src)
{
	if (EmitterSource(R) == Src) { return; }
	const FScopedTransaction Tr(FText::FromString(TEXT("Свечение эмиттера: откуда числа")));
	FAsdaFxEmitterGlow Before;
	AAsdaEffectActor* Cdo = EmitterEditBegin(R, Before);
	if (!Cdo) { return; }
	FAsdaFxEmitterGlow& G = Cdo->Emitters[R.Index].Glow;
	G.Source = Src;
	// Подхват чисел эффекта ОДИН раз — ровно как в панели деталей (AAsdaEffectActor::PostEditChangeProperty):
	// иначе в момент перевода в «вручную» картинка прыгнула бы на C++-умолчания структуры
	if (Src == EAsdaFxEmitterGlowSource::Manual && !G.bSpecSeeded)
	{
		G.Spec = Cdo->FxLight;
		// ★★★ ВОЛНА Ф14 (МЕЛК-2, 20.09.2026): ЗАМОК «ЗАДАНО ВРУЧНУЮ» СТАВИМ И МЫ.
		//   Панель деталей при том же подхвате ставит его (AsdaEffectActor.cpp, ветка Emitters:
		//   `Em.Glow.Spec = FxLight; Em.Glow.Spec.bManual = true;`), а конфигуратор — не ставил.
		//   Один и тот же эмиттер получался разным смотря по тому, ГДЕ его перевели в «вручную»:
		//   после конфигуратора его числа подпадали под следующий прогон генератора дефолтов
		//   (Tools/asda_apply_fx_light_defaults.py пропускает только bManual), после панели — нет.
		//   Приводим к одному: числа теперь СВОИ, генератор их не трогает.
		G.Spec.bManual = true;
		G.bSpecSeeded = true;
	}
	// ★ Ф13 (МЕЛК): ВОЗВРАТ В «ОТ ЭФФЕКТА» СНИМАЕТ ПРИЗНАК ПОДХВАТА — как и панель деталей
	//   (AAsdaEffectActor::PostEditChangeProperty, AsdaEffectActor.cpp:536). Без этого один и тот же
	//   эмиттер вёл себя по-разному смотря по тому, где его переключали: после нашего возврата в
	//   «от эффекта» следующий переход в «вручную» подхватывал бы СТАРЫЕ числа эффекта, а после
	//   переключения в панели — актуальные.
	if (Src == EAsdaFxEmitterGlowSource::FromEffect) { G.bSpecSeeded = false; }
	EmitterEditEnd(Cdo, R, Before);
}

void FAsdaFxGlowConfigModel::SetEmitterField(const FAsdaGlowEmitterRef& R, const FAsdaGlowField& F, const FAsdaFxLightSpec& Src)
{
	if (!F.Prop || F.bReadOnly) { return; }
	const FScopedTransaction Tr(FText::FromString(FString::Printf(TEXT("Свечение эмиттера: %s"), *F.Label)));
	FAsdaFxEmitterGlow Before;
	AAsdaEffectActor* Cdo = EmitterEditBegin(R, Before);
	if (!Cdo) { return; }
	FAsdaFxEmitterGlow& G = Cdo->Emitters[R.Index].Glow;
	// Правка числа при «взято от эффекта» не имела бы куда лечь — переводим в «вручную» с подхватом,
	// тот же приём, что у галочки «Сила свечения: задать вручную»
	if (G.Source != EAsdaFxEmitterGlowSource::Manual)
	{
		G.Spec = Cdo->FxLight;
		G.Spec.bManual = true;   // ★ Ф14 (МЕЛК-2): как и панель деталей — см. SetEmitterSource
		G.bSpecSeeded = true;
		G.Source = EAsdaFxEmitterGlowSource::Manual;
	}
	F.Prop->CopyCompleteValue_InContainer(&G.Spec, &Src);
	EmitterEditEnd(Cdo, R, Before);
}

// ★★★ РЕВИЗИЯ Ф11 (К2): «СБРОС» У ЭМИТТЕРА ВОЗВРАЩАЕТ ТО, ЧТО ЛЕЖАЛО В АССЕТЕ.
//   БЫЛО: всегда копировалось число ЭФФЕКТА — то есть собственное число эмиттера, ради которого
//   владелец его и настраивал, исчезало без следа (бэкапа ассета нет, на диск мы не пишем).
//   СТАЛО: если в снимке ассета у эмиттера были СВОИ числа — возвращается число из снимка;
//   если своих чисел в ассете не было (Source ≠ «вручную»), то «как в ассете» и означает
//   «берётся от эффекта» — возвращается число эффекта, как прежде.
void FAsdaFxGlowConfigModel::ResetEmitterField(const FAsdaGlowEmitterRef& R, const FAsdaGlowField& F)
{
	if (!F.Prop || F.bReadOnly) { return; }
	if (EmitterSource(R) != EAsdaFxEmitterGlowSource::Manual) { return; }
	const FScopedTransaction Tr(FText::FromString(FString::Printf(TEXT("Свечение эмиттера: сброс «%s»"), *F.Label)));
	FAsdaFxEmitterGlow Before;
	AAsdaEffectActor* Cdo = EmitterEditBegin(R, Before);
	if (!Cdo) { return; }
	FAsdaFxEmitterGlow& G = Cdo->Emitters[R.Index].Glow;
	const FAsdaGlowFxTouch* Touch = FindTouch(R.ClassPath);
	const FAsdaFxEmitterGlow* Base = Touch ? Touch->Baseline.Find(R.Index) : nullptr;
	const bool bAssetHadOwn = Base && Base->Source == EAsdaFxEmitterGlowSource::Manual;
	const FAsdaFxLightSpec From = bAssetHadOwn ? Base->Spec : Cdo->FxLight;
	F.Prop->CopyCompleteValue_InContainer(&G.Spec, &From);
	EmitterEditEnd(Cdo, R, Before);
}

// ★ К2: вернуть эмиттер ЦЕЛИКОМ к состоянию ассета (включая «откуда числа»)
void FAsdaFxGlowConfigModel::RevertEmitterToAsset(const FAsdaGlowEmitterRef& R)
{
	const FAsdaGlowFxTouch* Touch = FindTouch(R.ClassPath);
	const FAsdaFxEmitterGlow* Base = Touch ? Touch->Baseline.Find(R.Index) : nullptr;
	if (!Base) { return; }
	const FAsdaFxEmitterGlow Restore = *Base;   // копия: дальше склад снимков может дополняться
	const FScopedTransaction Tr(FText::FromString(TEXT("Свечение эмиттера: вернуть как в ассете")));
	FAsdaFxEmitterGlow Before;
	AAsdaEffectActor* Cdo = EmitterEditBegin(R, Before);
	if (!Cdo) { return; }
	Cdo->Emitters[R.Index].Glow = Restore;
	EmitterEditEnd(Cdo, R, Before);
}

bool FAsdaFxGlowConfigModel::IsEmitterFieldOwn(const FAsdaGlowEmitterRef& R, const FAsdaGlowField& F) const
{
	const AAsdaEffectActor* Cdo = EmitterCdo(R);
	if (!Cdo || !F.Prop) { return false; }
	const FAsdaFxEmitterGlow& G = Cdo->Emitters[R.Index].Glow;
	if (G.Source != EAsdaFxEmitterGlowSource::Manual) { return false; }
	// «Своё» = отличается от числа эффекта. Сравнение самим свойством, а не побайтно: у FString и FColor
	// побайтное сравнение дало бы ложное «отличается»
	return !F.Prop->Identical_InContainer(&G.Spec, &Cdo->FxLight);
}

// ── ★ ревизия Ф11 (К2, К3, С8): честный учёт тронутых ассетов ────────────────────────────────────
bool FAsdaFxGlowConfigModel::HasEmitterBaseline(const FAsdaGlowEmitterRef& R) const
{
	const FAsdaGlowFxTouch* T = FindTouch(R.ClassPath);
	return T && T->Baseline.Contains(R.Index);
}

// Пакет ассета по пути класса БЕЗ загрузки: если класс не в памяти, значит, и правок у него нет.
// ★ Ф14 (МЕЛК-4): второй заход — по слабой ссылке записи склада, иначе после Rename/Move ассета
//   пакет «терялся», отметка * навсегда застревала в «не записано», а «Пометить все тронутые»
//   молча считала ассет выгруженным
static UPackage* AsdaGlowFindLoadedFxPackage(const FString& ClassPath, const TWeakObjectPtr<UClass>& Weak = nullptr)
{
	if (UClass* Cls = AsdaGlowResolveFxClass(ClassPath, Weak)) { return Cls->GetOutermost(); }
	return nullptr;
}

bool FAsdaFxGlowConfigModel::IsFxAssetUnsaved(const FString& ClassPath) const
{
	const FAsdaGlowFxTouch* T = FindTouch(ClassPath);
	if (!T) { return false; }
	const UPackage* Pkg = AsdaGlowFindLoadedFxPackage(ClassPath, T->FxClass);
	// ★ «Не записано» — живой признак пакета, а не наш набор: после «Save All» владельца отметка *
	//   обязана исчезнуть сама, иначе она снова начнёт врать
	return Pkg ? Pkg->IsDirty() : true;
}

int32 FAsdaFxGlowConfigModel::NumFxAssetsUnsaved() const
{
	int32 N = 0;
	for (const TPair<FString, FAsdaGlowFxTouch>& P : TouchedFx()) { if (IsFxAssetUnsaved(P.Key)) { ++N; } }
	return N;
}

int32 FAsdaFxGlowConfigModel::NumFxAssetsUnmarked() const
{
	int32 N = 0;
	for (const TPair<FString, FAsdaGlowFxTouch>& P : TouchedFx())
	{
		const UPackage* Pkg = AsdaGlowFindLoadedFxPackage(P.Key, P.Value.FxClass);
		if (Pkg && !Pkg->IsDirty()) { ++N; }
	}
	return N;
}

void FAsdaFxGlowConfigModel::GetUnsavedFxAssets(TArray<FString>& Out) const
{
	Out.Reset();
	for (const TPair<FString, FAsdaGlowFxTouch>& P : TouchedFx())
	{
		if (!IsFxAssetUnsaved(P.Key)) { continue; }
		const UPackage* Pkg = AsdaGlowFindLoadedFxPackage(P.Key, P.Value.FxClass);
		Out.Add(Pkg ? Pkg->GetName() : P.Key);
	}
	Out.Sort();
}

bool FAsdaFxGlowConfigModel::IsEmitterPending(const FAsdaGlowEmitterRef& R) const
{
	if (!IsFxAssetUnsaved(R.ClassPath)) { return false; }
	const FAsdaGlowFxTouch* T = FindTouch(R.ClassPath);
	const FAsdaFxEmitterGlow* Base = T ? T->Baseline.Find(R.Index) : nullptr;
	const AAsdaEffectActor* Cdo = EmitterCdo(R);
	if (!Base || !Cdo) { return false; }
	// ★ С8: отметка * только у ТЕХ эмиттеров, что и правда отличаются от ассета, а не у всех эмиттеров
	//   тронутого ассета (до правки * получали все — и по ним нельзя было понять, что трогал сам)
	return !FAsdaFxEmitterGlow::StaticStruct()->CompareScriptStruct(Base, &Cdo->Emitters[R.Index].Glow, PPF_None);
}

bool FAsdaFxGlowConfigModel::IsEmitterFieldPending(const FAsdaGlowEmitterRef& R, const FAsdaGlowField& F) const
{
	if (!F.Prop || !IsFxAssetUnsaved(R.ClassPath)) { return false; }
	const FAsdaGlowFxTouch* T = FindTouch(R.ClassPath);
	const FAsdaFxEmitterGlow* Base = T ? T->Baseline.Find(R.Index) : nullptr;
	const AAsdaEffectActor* Cdo = EmitterCdo(R);
	if (!Base || !Cdo) { return false; }
	const FAsdaFxEmitterGlow& G = Cdo->Emitters[R.Index].Glow;
	// Сменился сам признак «откуда числа» — не записано всё, что у эмиттера есть
	if (Base->Source != G.Source) { return true; }
	if (G.Source != EAsdaFxEmitterGlowSource::Manual) { return false; }
	return !F.Prop->Identical_InContainer(&G.Spec, &Base->Spec);
}

// ★★★ РЕВИЗИЯ Ф11 (К3): ПОМЕТИТЬ ИЗМЕНЁННЫМИ ВСЕ ТРОНУТЫЕ АССЕТЫ, А НЕ ОДИН ВЫБРАННЫЙ.
//   Прежняя кнопка показывала число всех тронутых, а помечала только выбранный — четыре из пяти
//   правок уходили в небытие при закрытии редактора. Теперь пометку ставит сама правка (Modify в
//   EmitterEditBegin), а эта кнопка — страховка: проходит по всем тронутым и помечает тех, у кого
//   признак почему-то снят (например, владелец сохранил ассет, а потом правил снова).
int32 FAsdaFxGlowConfigModel::MarkAllTouchedFxDirty(FString& OutMsg)
{
	int32 Marked = 0, Missing = 0;
	TArray<FString> Names;
	// ★ Ревизия Ф12 (МЕЛК): выгруженные классы больше НЕ ПРОПУСКАЮТСЯ МОЛЧА — их имена уходят
	//   владельцу. Молчание здесь означало бы «всё помечено», а на деле правки такого ассета
	//   пометить нечем: пакета в памяти нет, и при закрытии редактора о них не спросят
	TArray<FString> Lost;
	for (const TPair<FString, FAsdaGlowFxTouch>& P : TouchedFx())
	{
		UPackage* Pkg = AsdaGlowFindLoadedFxPackage(P.Key, P.Value.FxClass);
		if (!Pkg) { ++Missing; Lost.Add(AsdaGlowPackageNameFromClassPath(P.Key)); continue; }
		if (!Pkg->IsDirty())
		{
			// ═══ ★★★ ВОЛНА Ф14 (С-2, 20.09.2026): ЭТА ПОМЕТКА — НАША, А НЕ ЧУЖАЯ ═══════════════
			//   Счётчик GAsdaGlowInsideOwnEdit охватывал только Modify в EmitterEditBegin, а эта
			//   кнопка звала MarkPackageDirty() ВНЕ его. Подписка на PackageMarkedDirtyEvent
			//   принимала собственную пометку конфигуратора за правку владельца помимо него:
			//   bForeignDirty вставал навсегда, «Вернуть эмиттер как в ассете» больше не снимала
			//   признак «изменён», а владельцу показывалось ложное «этот ассет правили и помимо
			//   конфигуратора». Лечится тем же приёмом, что и Modify, — счётчиком вокруг вызова.
			++GAsdaGlowInsideOwnEdit;
			Pkg->MarkPackageDirty();
			--GAsdaGlowInsideOwnEdit;
			++Marked;
		}
		Names.Add(Pkg->GetName());
	}
	Names.Sort();
	if (TouchedFx().Num() == 0)
	{
		OutMsg = TEXT("Поэмиттерных правок в этом сеансе не было — помечать нечего.");
	}
	else
	{
		OutMsg = FString::Printf(TEXT("Тронуто ассетов: %d, пометка понадобилась %d, не найдено в памяти %d; ещё не записано на диск: %d. Сохраните их штатным «Save All» редактора."),
			TouchedFx().Num(), Marked, Missing, NumFxAssetsUnsaved());
		if (Lost.Num() > 0)
		{
			Lost.Sort();
			// ★ Ревизия Ф12 (МЕЛК): честно называем то, что пометить не удалось
			OutMsg += FString::Printf(TEXT("\n\n★ ВНИМАНИЕ: %d ассет(ов) выгружены из памяти, пометить их НЕЧЕМ — правки в них при закрытии редактора пропадут без вопроса: %s"),
				Lost.Num(), *FString::Join(Lost, TEXT(", ")));
		}
	}
	UE_LOG(LogAsdaGlowConfig, Log, TEXT("[конфигуратор] %s (%s)"), *OutMsg, *FString::Join(Names, TEXT(", ")));
	return Marked;
}

int32 FAsdaFxGlowConfigModel::NumDirty() const
{
	int32 N = 0;
	for (const TPair<FString, FAsdaGlowEditState>& P : States) { if (P.Value.IsDirty()) { ++N; } }
	return N;
}

// ── сохранение ──────────────────────────────────────────────────────────────
bool FAsdaFxGlowConfigModel::Save(FString& OutMsg)
{
	TArray<TSharedPtr<FJsonValue>> Ops;
	for (const TPair<FString, FAsdaGlowEditState>& P : States)
	{
		const FAsdaGlowEditState& St = P.Value;
		if (!St.IsDirty()) { continue; }
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		if (St.Target.bFamily)
		{
			O->SetStringField(TEXT("kind"), TEXT("family"));
			O->SetStringField(TEXT("id"), St.Target.FamilyId);
		}
		else
		{
			O->SetStringField(TEXT("kind"), TEXT("override"));
			O->SetStringField(TEXT("jeb"), St.Target.Jeb);
			O->SetNumberField(TEXT("item_id"), St.Target.ItemId);
			O->SetStringField(TEXT("label"), St.Target.Label);
			O->SetBoolField(TEXT("remove"), St.bPendingRemove);
		}
		TSharedPtr<FJsonObject> SetObj = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> ResetArr;
		for (const FAsdaGlowField& F : Fields)
		{
			if (St.PendingSet.Contains(F.CppName)) { SetObj->SetField(F.CppName.ToString(), ValueJson(F, St.Work)); }
			if (St.PendingReset.Contains(F.CppName)) { ResetArr.Add(MakeShared<FJsonValueString>(F.CppName.ToString())); }
		}
		O->SetObjectField(TEXT("set"), SetObj);
		O->SetArrayField(TEXT("reset"), ResetArr);
		Ops.Add(MakeShared<FJsonValueObject>(O));
	}
	if (Ops.Num() == 0) { OutMsg = TEXT("несохранённых правок нет"); return true; }

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("ops"), Ops);
	FString Text;
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
	const FString OpsPath = FPaths::Combine(WorkDir(), TEXT("ops.json"));
	const FString ResPath = FPaths::Combine(WorkDir(), TEXT("result.json"));
	IFileManager::Get().Delete(*ResPath, false, true, true);
	if (!FFileHelper::SaveStringToFile(Text, *OpsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutMsg = FString::Printf(TEXT("не записался %s"), *OpsPath);
		return false;
	}
	FString PyErr;
	RunGlowPython({ TEXT("apply"), OpsPath, ResPath }, PyErr);
	FString ResText;
	TSharedPtr<FJsonObject> Res;
	if (!FFileHelper::LoadFileToString(ResText, *ResPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ResText), Res) || !Res.IsValid())
	{
		OutMsg = FString::Printf(TEXT("Python не вернул результат (%s). %s"), *ResPath, *PyErr);
		return false;
	}
	// ★ Ревизия 13.09 (п.2): три исхода разведены. written=false — ни один файл не изменён (проверки в памяти,
	//   право записи, откат); written=true и ok — всё чисто; written=true без ok или с warning — сохранено,
	//   но сверка что-то нашла (бюджеты DefaultScalability.ini вне этой записи — лишь предупреждение)
	if (!Res->GetBoolField(TEXT("written")))
	{
		OutMsg = FString::Printf(TEXT("НЕ СОХРАНЕНО (файлы не изменены): %s"), *Res->GetStringField(TEXT("error")));
		UE_LOG(LogAsdaGlowConfig, Warning, TEXT("[конфигуратор] %s"), *OutMsg);
		return false;
	}
	// Файлы записаны — игра прочтёт ini, и редактор обязан действовать ровно по нему же, а не по живому CDO
	const int32 NOps = Ops.Num();
	FString Err;
	const bool bReloaded = GetMutableDefault<UAsdaFxLightSettings>()->ReloadGlowTablesFromDefaultIni(Err);
	if (bReloaded) { UAsdaFxLightSubsystem::NotifyGlowTablesChanged(); }
	Reload();
	FString Backup;
	Res->TryGetStringField(TEXT("backup"), Backup);
	const FString Warning = Res->GetStringField(TEXT("warning"));
	const FString ResError = Res->GetStringField(TEXT("error"));
	OutMsg = FString::Printf(TEXT("Сохранено: правок %d → Tools/asda_fx_light.json → Config/DefaultGame.ini.%s%s"),
		NOps, Backup.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" Бэкап: %s."), *Backup),
		Res->GetBoolField(TEXT("ok")) && Warning.IsEmpty() ? TEXT(" Проверка генератора чиста.") : TEXT(""));
	if (!Warning.IsEmpty()) { OutMsg += FString::Printf(TEXT("\nПРЕДУПРЕЖДЕНИЕ сверки: %s"), *Warning); }
	if (!ResError.IsEmpty()) { OutMsg += FString::Printf(TEXT("\nПРЕДУПРЕЖДЕНИЕ: %s"), *ResError); }
	if (!bReloaded) { OutMsg += FString::Printf(TEXT("\nФайлы записаны, но перечитать ini в редакторе не удалось (%s) — действует после перезапуска."), *Err); }
	UE_LOG(LogAsdaGlowConfig, Log, TEXT("[конфигуратор] %s"), *OutMsg);
	return true;
}

void FAsdaFxGlowConfigModel::Revert()
{
	FString Err;
	if (GetMutableDefault<UAsdaFxLightSettings>()->ReloadGlowTablesFromDefaultIni(Err))
	{
		UAsdaFxLightSubsystem::NotifyGlowTablesChanged();
	}
	else
	{
		UE_LOG(LogAsdaGlowConfig, Warning, TEXT("[конфигуратор] отмена: перечитать ini не удалось: %s"), *Err);
	}
	// ★ Ревизия Ф11 (МЕЛК): склад снимков здесь НЕ чистится намеренно. «Отменить несохранённое» — про
	//   таблицу, а снимки «как в ассете» принадлежат правкам эмиттеров, которые живут в самих ассетах
	//   и этой кнопкой не отменяются. Выбросить снимки значило бы отнять у владельца единственный путь
	//   назад («Вернуть эмиттер как в ассете»). Считать «не записано» набор всё равно не даёт —
	//   это делает живой признак пакета (IsFxAssetUnsaved)
	States.Reset();
}

// ═══ ★ Ревизия 13.09 (п.5): пример эффекта семейства и текущая сила свечения ════════════════════════════
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"   // FMaterialParameterInfo

FString FAsdaFxGlowConfigModel::FindFamilyExample(const FString& FamilyId, FString& OutJeb) const
{
	OutJeb.Empty();
	const UAsdaFxLightSettings* S = UAsdaFxLightSettings::Get();
	const FAsdaFxLightRule* R = S->Families.FindByPredicate([&FamilyId](const FAsdaFxLightRule& X) { return X.Id == FamilyId; });
	if (!R) { return FString(); }
	// Блёстки дропа живут не в BP_Fx_*, а в BP_LootDropFx — у семейства дропа пример он
	if (!LootFxClassPath.IsEmpty() && S->FindRule(LootJeb) == R)
	{
		OutJeb = LootJeb;
		return LootFxClassPath;
	}
	// ★★★ РЕВИЗИЯ Ф11 (МЕЛК, 20.09.2026): БЕЗ ЗАГРУЗКИ КЛАССОВ В ЦИКЛЕ.
	//   БЫЛО: перебор всех ассетов реестра с LoadClass на каждом, пока не совпадёт семейство, —
	//   у семейства с редким шаблоном имени это десятки загруженных классов BP_Fx_* ради ОДНОГО
	//   превью (а вкладка зовёт это при каждом выборе узла семейства).
	//   СТАЛО: перепись FxEffects, построенная один раз при Reload, уже содержит имя .jeb (обратная
	//   формула реестра путей), путь класса и семейство — ровно то, что здесь проверялось. Ни одной
	//   загрузки: класс грузит сам вызывающий, когда он ему и правда нужен.
	//   ★ Разница с прежним: имя .jeb берётся формулой, а не полем EffectName загруженного CDO.
	//     Для ВЫБОРА семейства это то же самое — прежний цикл всё равно требовал совпадения семейства
	//     по обоим написаниям (FindRule(N) и FindRule(Jeb)).
	for (const FFxEffectEntry& E : FxEffects)   // отсортирована по .jeb ⇒ пример устойчив от запуска к запуску
	{
		if (E.FamilyId != FamilyId || S->FindRule(E.Jeb) != R) { continue; }
		OutJeb = E.Jeb;
		return E.ClassPath;
	}
	return FString();
}

float FAsdaFxGlowConfigModel::CurrentMaterialBoost(const FAsdaGlowTarget& T) const
{
	FString Jeb = T.Jeb;
	FString ClassPath;
	if (T.bFamily) { ClassPath = FindFamilyExample(T.FamilyId, Jeb); }
	else if (Jeb.Equals(LootJeb, ESearchCase::IgnoreCase)) { ClassPath = LootFxClassPath; }
	else
	{
		ClassPath = AsdaAssetPaths::FxClassPathFromJeb(Jeb);   // ★ волна 2 п. 7: правило реестра путей
	}
	const UClass* Cls = ClassPath.IsEmpty() ? nullptr : LoadClass<AActor>(nullptr, *ClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	const AAsdaEffectActor* Cdo = Cls ? Cast<AAsdaEffectActor>(Cls->GetDefaultObject()) : nullptr;
	if (!T.bFamily)
	{
		// Запись эффекта/предмета стоит выше ассета и семейства: «сейчас» эффект рисуется тем, что ниже неё
		if (Cdo && Cdo->FxLight.bOverrideEmissive) { return Cdo->FxLight.EmissiveBoost; }
		if (const FAsdaFxLightRule* R = UAsdaFxLightSettings::Get()->FindRule(Jeb))
		{
			if (R->Spec.bOverrideEmissive) { return R->Spec.EmissiveBoost; }
		}
	}
	// Материал первого эмиттера, у которого параметр есть (MIC-вариант _fxbNNN таблицы буста; у Modulate его нет)
	if (Cdo)
	{
		for (const FAsdaFxEmitter& E : Cdo->Emitters)
		{
			float V = 0.f;
			if (E.Material && E.Material->GetScalarParameterValue(FMaterialParameterInfo(AsdaMat::kParamEmissiveBoost), V)) { return V; }   // ★ В1-C F10
		}
	}
	// Пустое имя — правила не проверяются (прежний `break` на первой итерации); иначе первое подходящее правило
	if (!Jeb.IsEmpty())
	{
		if (const FBoostRule* R = FindBoostRule(Jeb)) { return R->Boost; }
	}
	return 1.f;
}
