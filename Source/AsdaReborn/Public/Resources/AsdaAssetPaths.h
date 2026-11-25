// Copyright (c) 2026 @sitxovski (Discord)
// AsdaAssetPaths.h — РЕЕСТР ПУТЕЙ И ПРАВИЛ ИМЁН АССЕТОВ для C++: корни /Game, построение пути по имени из данных
//   (часть экипировки, скелетал и клип, класс эффекта, звук, JUI-атлас, каталоги текстур карты…), формы ссылки.
//
// ★ ГЕНЕРИРУЕТСЯ Tools/asda_asset_paths.py из Tools/data/asda_asset_paths.json — НЕ ПРАВИТЬ РУКАМИ.
//   Правка: источник (JSON) → `python -B Tools/asda_asset_paths.py`; сверка — `--check`; литералы после волны — `--scan`.
//   Python-двойник — генерируемый блок Tools/asda_ue_paths.py (те же правила, имена в snake_case).
//   Имя карты/звука приводится ОДНОЙ точкой — AsdaMapRegistry::ToAssetName (реестр карт), здесь не копируется.
#pragma once

#include "CoreMinimal.h"
#include "World/AsdaMapRegistry.h"

namespace AsdaAssetPaths
{
	// ══ Корни и одиночные ассеты (пакетные пути) ══
	inline constexpr const TCHAR* kAssetsRoot = TEXT("/Game/AsdaAssets");   // корень ассетов порта
	inline constexpr const TCHAR* kModelsRoot = TEXT("/Game/AsdaAssets/Models");
	inline constexpr const TCHAR* kFxRoot = TEXT("/Game/AsdaAssets/Effects");   // эффект-BP BP_Fx_<jeb>
	inline constexpr const TCHAR* kSoundRoot = TEXT("/Game/AsdaAssets/Sound");   // SFX + DT_SoundTable (дефолтная папка ResolveSound)
	inline constexpr const TCHAR* kSoundFolder = TEXT("Sound");   // папка относительно /Game/AsdaAssets (аргумент Folder)
	inline constexpr const TCHAR* kMusicRoot = TEXT("/Game/AsdaAssets/Sounds/Music");   // МУЗЫКА (канон): PlayMusic читает отсюда
	inline constexpr const TCHAR* kMusicFolder = TEXT("Sounds/Music");   // папка относительно /Game/AsdaAssets (аргумент Folder)
	inline constexpr const TCHAR* kCharModelsRoot = TEXT("/Game/AsdaAssets/Models/Char");   // жёсткие меши персонажа <m>/StaticMeshes/<m>
	inline constexpr const TCHAR* kCharModelsPrefix = TEXT("/Game/AsdaAssets/Models/Char/");   // то же с «/» на конце (префикс для «+ имя»)
	inline constexpr const TCHAR* kItemModelsRoot = TEXT("/Game/AsdaAssets/Models/Items");   // модели лута <m>/StaticMeshes/<m>
	inline constexpr const TCHAR* kItemModelsPrefix = TEXT("/Game/AsdaAssets/Models/Items/");   // то же с «/» на конце (префикс для «+ имя»)
	inline constexpr const TCHAR* kSkelRootNpc = TEXT("/Game/AsdaAssets/Models/NPC");   // скелеталы NPC
	inline constexpr const TCHAR* kEquipPartsRoot = TEXT("/Game/AsdaAssets/Models/Skeletal/EquipParts");   // части экипировки <m>/<m> (без SkeletalMeshes/)
	inline constexpr const TCHAR* kEquipPartsPrefix = TEXT("/Game/AsdaAssets/Models/Skeletal/EquipParts/");   // то же с «/» на конце (префикс для «+ имя»)
	inline constexpr const TCHAR* kSkelRootVehicle = TEXT("/Game/AsdaAssets/Models/Skeletal/Vehicle");   // транспорт
	inline constexpr const TCHAR* kSkelRootMobSkel = TEXT("/Game/AsdaAssets/Models/MobSkel");   // скелеталы мобов
	inline constexpr const TCHAR* kMobModelsRoot = TEXT("/Game/AsdaAssets/Models/Mob");   // статик-модели мобов (фолбэк к MobSkel)
	inline constexpr const TCHAR* kSkelRootPet = TEXT("/Game/AsdaAssets/Models/Skeletal/Pet");   // питомцы
	inline constexpr const TCHAR* kSkelRootSoulGuard = TEXT("/Game/AsdaAssets/Models/Skeletal/SoulGuard");   // хранитель: скиннутые sb_md_* (статик-двойники — в Models/Char)
	inline constexpr const TCHAR* kSkelRootClanFlag = TEXT("/Game/AsdaAssets/Models/Skeletal/ClanFlag");   // флаг клана
	inline constexpr const TCHAR* kSkelRootFishing = TEXT("/Game/AsdaAssets/Models/Skeletal/Fishing");   // рыбалка: зона fs_zone, удочки fird_*, рыбы fs_fsm_*/fs_stuff_00 (волна В5а 08.10.2026)
	inline constexpr const TCHAR* kSkelRootCharacters = TEXT("/Game/AsdaAssets/Models/Skeletal/Characters");   // база персонажа ch1_<g>_cremmy_preview + клипы
	inline constexpr const TCHAR* kSkelRootGachya = TEXT("/Game/AsdaAssets/Models/Skeletal/Gachya");   // автоматы премиум-гачи (новая константа, 14.09)
	inline constexpr const TCHAR* kClanFlagMesh = TEXT("/Game/AsdaAssets/Models/Skeletal/ClanFlag/small_flag/SkeletalMeshes/small_flag");   // скелетал флага клана на транспорте
	inline constexpr const TCHAR* kCharPreviewBaseM = TEXT("/Game/AsdaAssets/Models/Skeletal/Characters/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_preview");   // база персонажа, мужской
	inline constexpr const TCHAR* kCharPreviewBaseF = TEXT("/Game/AsdaAssets/Models/Skeletal/Characters/ch1_f_cremmy_preview/SkeletalMeshes/ch1_f_cremmy_preview");   // база персонажа, женский
	inline constexpr const TCHAR* kJuiRoot = TEXT("/Game/AsdaAssets/Textures/JUI");   // UI-атласы JUI
	inline constexpr const TCHAR* kJuiPrefix = TEXT("/Game/AsdaAssets/Textures/JUI/");   // то же с «/» на конце (префикс для «+ имя»)
	inline constexpr const TCHAR* kNpcImageRoot = TEXT("/Game/AsdaAssets/Textures/NPC/Image");   // текстуры названий локаций (эталон Data/NPC/Image)
	inline constexpr const TCHAR* kNpcPortraitRoot = TEXT("/Game/AsdaAssets/Textures/NPC/Portrait");   // портреты NPC и текстуры названий
	inline constexpr const TCHAR* kNpcPortraitPrefix = TEXT("/Game/AsdaAssets/Textures/NPC/Portrait/");   // то же с «/» на конце (префикс для «+ имя»)
	inline constexpr const TCHAR* kCharTextureRoot = TEXT("/Game/AsdaAssets/Textures/Characters/Texture");   // текстуры моделей персонажа
	inline constexpr const TCHAR* kCharTexturePrefix = TEXT("/Game/AsdaAssets/Textures/Characters/Texture/");   // то же с «/» на конце (префикс для «+ имя»)
	inline constexpr const TCHAR* kItemTextureRoot = TEXT("/Game/AsdaAssets/Textures/Items/Texture");   // текстуры моделей предметов (лут)
	inline constexpr const TCHAR* kItemTexturePrefix = TEXT("/Game/AsdaAssets/Textures/Items/Texture/");   // то же с «/» на конце (префикс для «+ имя»)
	inline constexpr const TCHAR* kMapTexRoot = TEXT("/Game/AsdaAssets/Textures/Maps");   // текстуры карт, метки мини-карты, картинки загрузки
	inline constexpr const TCHAR* kMapTexPrefix = TEXT("/Game/AsdaAssets/Textures/Maps/");   // то же с «/» на конце (префикс для «+ имя»)
	inline constexpr const TCHAR* kMapImageRoot = TEXT("/Game/AsdaAssets/Textures/MapImage");   // подложки мини-/большой карты
	inline constexpr const TCHAR* kMapImagePrefix = TEXT("/Game/AsdaAssets/Textures/MapImage/");   // то же с «/» на конце (префикс для «+ имя»)
	inline constexpr const TCHAR* kMaterialObject = TEXT("/Game/Materials/M_AsdaObject");

	// ══ Правила ══

	// Пакетный путь скелетной части экипировки: EquipParts/<m>/<m> (ключ менеджера моделей).
	inline FString EquipPartMesh(const FString& Mesh)
	{
		return FString::Printf(TEXT("/Game/AsdaAssets/Models/Skeletal/EquipParts/%s/%s"), *Mesh, *Mesh);
	}

	// Объектный путь жёсткого меша персонажа (причёска/оружие): Char/<m>/StaticMeshes/<m>.<m>.
	inline FString CharStaticMeshObject(const FString& Mesh)
	{
		return FString::Printf(TEXT("/Game/AsdaAssets/Models/Char/%s/StaticMeshes/%s.%s"), *Mesh, *Mesh, *Mesh);
	}

	// Объектный путь текстуры модели персонажа: Textures/Characters/Texture/<t>.<t>.
	inline FString CharTextureObject(const FString& Tex)
	{
		return FString::Printf(TEXT("/Game/AsdaAssets/Textures/Characters/Texture/%s.%s"), *Tex, *Tex);
	}

	// Имя текстуры лица (эталон SetParts_FaceTexture): ch1_<m|f>_00_face001_NN_00_0, NN — 2 знака.
	inline FString FaceTextureName(bool bMale, int32 FaceIndex)
	{
		return FString::Printf(TEXT("ch1_%s_00_face001_%02d_00_0"), (bMale ? TEXT("m") : TEXT("f")), FaceIndex);
	}

	// Пакетный путь скелетала/клипа: <корень>/<имя>/SkeletalMeshes/<имя><суффикс> (корень — k*SkelRoot*).
	inline FString SkelMesh(const FString& Root, const FString& Name, const FString& Suffix = FString())
	{
		return FString::Printf(TEXT("%s/%s/SkeletalMeshes/%s%s"), *Root, *Name, *Name, *Suffix);
	}

	// Суффикс клипа: anim_NN (NN — не меньше 2 знаков).
	inline FString SkelAnimSuffix(int32 AnimIdx)
	{
		return FString::Printf(TEXT("anim_%02d"), AnimIdx);
	}

	// Пакетный путь клипа: <корень>/<имя>/SkeletalMeshes/<имя>anim_NN.
	inline FString SkelAnim(const FString& Root, const FString& Name, int32 AnimIdx)
	{
		const FString Suffix = SkelAnimSuffix(AnimIdx);
		return FString::Printf(TEXT("%s/%s/SkeletalMeshes/%s%s"), *Root, *Name, *Name, *Suffix);
	}

	// Имя базы персонажа: ch1_<m|f>_cremmy_preview.
	inline FString CharPreviewName(bool bMale)
	{
		return FString::Printf(TEXT("ch1_%s_cremmy_preview"), (bMale ? TEXT("m") : TEXT("f")));
	}

	// Объектный путь статик-модели моба (фолбэк к скелеталу): Mob/<m>/StaticMeshes/<m>.<m>.
	inline FString MobStaticMeshObject(const FString& Model)
	{
		return FString::Printf(TEXT("/Game/AsdaAssets/Models/Mob/%s/StaticMeshes/%s.%s"), *Model, *Model, *Model);
	}

	// Объектный путь модели лута: Items/<m>/StaticMeshes/<m>.<m>.
	inline FString LootMeshObject(const FString& Name)
	{
		return FString::Printf(TEXT("/Game/AsdaAssets/Models/Items/%s/StaticMeshes/%s.%s"), *Name, *Name, *Name);
	}

	// Объектный путь текстуры лута: Textures/Items/Texture/<t>.<t>.
	inline FString LootTextureObject(const FString& Tex)
	{
		return FString::Printf(TEXT("/Game/AsdaAssets/Textures/Items/Texture/%s.%s"), *Tex, *Tex);
	}

	// Имя эффект-BP по базе .jeb: BP_Fx_<jeb с «-»→«_»> (дефис в имени .jeb на диске сохраняется).
	inline FString FxBpName(const FString& Jeb)
	{
		const FString JebUs = Jeb.Replace(TEXT("-"), TEXT("_"));
		return FString::Printf(TEXT("BP_Fx_%s"), *JebUs);
	}

	// Путь класса эффект-BP по готовому имени ассета: Effects/<bp>.<bp>_C.
	inline FString FxClassPathFromBp(const FString& Bp)
	{
		return FString::Printf(TEXT("/Game/AsdaAssets/Effects/%s.%s_C"), *Bp, *Bp);
	}

	// Пакет эффект-BP по имени ассета: Effects/<bp>.
	inline FString FxPackageFromBp(const FString& Bp)
	{
		return FString::Printf(TEXT("/Game/AsdaAssets/Effects/%s"), *Bp);
	}

	// Путь класса эффект-BP по базе .jeb: Effects/BP_Fx_<jeb _>.BP_Fx_<jeb _>_C.
	inline FString FxClassPathFromJeb(const FString& Jeb)
	{
		const FString Bp = FxBpName(Jeb);
		return FString::Printf(TEXT("/Game/AsdaAssets/Effects/%s.%s_C"), *Bp, *Bp);
	}

	// ★ ИСПРАВЛЕНИЕ D4 (волна 2 п. 7, 14.09.2026). Прежде (UAsdaFxLightSubsystem::ResolveJebName) имя .jeb из имени
	//   класса бралось как «BP_Fx_<x>_C» → «<x>» — с «_» вместо «-». Правилом дефис не вернуть: у 71 имени .jeb без
	//   дефиса тоже есть хвост «_a_NN». Поэтому — таблица РЕАЛЬНЫХ имён .jeb с дефисом, у которых в Content есть
	//   BP_Fx_* (538 шт.; пересборка: `python -B Tools/asda_asset_paths.py --refresh-fx-table`). Имена вне таблицы —
	//   как раньше. Регулярки семейств DefaultGame.ini на обоих написаниях дают то же семейство (стенд волны).
	inline constexpr const TCHAR* kFxJebDashNames[] = {
		TEXT("eff_0_char_03_flyarr01-a_00"),
		TEXT("eff_0_char_99_battk02-a_00"),
		TEXT("eff_0_char_99_cmchar01-a_00"),
		TEXT("eff_0_char_99_lvup01-a_00"),
		TEXT("eff_0_char_99_pmarmo01-a_00"),
		TEXT("eff_0_othr_99_elptn01-a_00"),
		TEXT("eff_0_othr_99_hpptn01-a_00"),
		TEXT("eff_0_othr_99_mpptn01-a_00"),
		TEXT("eff_0_othr_99_pmalre01-a_00"),
		TEXT("eff_0_othr_99_pmalre01-a_01"),
		TEXT("eff_0_othr_99_pmalre01-a_02"),
		TEXT("eff_0_othr_99_pmhpre01-a_00"),
		TEXT("eff_0_othr_99_pmhpre01-a_01"),
		TEXT("eff_0_othr_99_pmhpre01-a_02"),
		TEXT("eff_0_othr_99_pmmpre01-a_00"),
		TEXT("eff_0_othr_99_pmmpre01-a_01"),
		TEXT("eff_0_othr_99_pmmpre01-a_02"),
		TEXT("eff_0_othr_99_pmskre01-a_00"),
		TEXT("eff_1_char_01_brethc01-a_00"),
		TEXT("eff_1_char_01_crashb02-a_00"),
		TEXT("eff_1_char_01_crashh01-a_00"),
		TEXT("eff_1_char_01_defpos01-a_00"),
		TEXT("eff_1_char_01_defsac01-a_00"),
		TEXT("eff_1_char_01_fdrile01-a_00"),
		TEXT("eff_1_char_01_hblowe01-a_00"),
		TEXT("eff_1_char_01_hexaa07-a_00"),
		TEXT("eff_1_char_01_hexaa08-a_00"),
		TEXT("eff_1_char_01_hexaa09-a_00"),
		TEXT("eff_1_char_01_hexab03-a_00"),
		TEXT("eff_1_char_01_hexah02-a_00"),
		TEXT("eff_1_char_01_oswinge01-a_00"),
		TEXT("eff_1_char_01_pbladee01-a_00"),
		TEXT("eff_1_char_01_roarug01-a_00"),
		TEXT("eff_1_char_01_ulistr01-a_00"),
		TEXT("eff_1_char_02_amorde01-a_00"),
		TEXT("eff_1_char_02_cmbengb03-a_00"),
		TEXT("eff_1_char_02_cmbengw01-a_00"),
		TEXT("eff_1_char_02_counta01-a_00"),
		TEXT("eff_1_char_02_counta01-a_01"),
		TEXT("eff_1_char_02_ctimor01-a_00"),
		TEXT("eff_1_char_02_ctimor02-a_00"),
		TEXT("eff_1_char_02_cyclb02-a_00"),
		TEXT("eff_1_char_02_cycle01-a_00"),
		TEXT("eff_1_char_02_dmgacc01-a_00"),
		TEXT("eff_1_char_02_drgflyb03-a_00"),
		TEXT("eff_1_char_02_drgflyb04-a_00"),
		TEXT("eff_1_char_02_drgflye02-a_00"),
		TEXT("eff_1_char_02_drgflyw01-a_00"),
		TEXT("eff_1_char_02_dridg501-a_00"),
		TEXT("eff_1_char_02_fatale01-a_00"),
		TEXT("eff_1_char_02_fatale02-a_00"),
		TEXT("eff_1_char_02_fatale03-a_00"),
		TEXT("eff_1_char_02_fooffu01-a_00"),
		TEXT("eff_1_char_02_parlac01-a_00"),
		TEXT("eff_1_char_02_parlac02-a_00"),
		TEXT("eff_1_char_02_rgeblwe01-a_00"),
		TEXT("eff_1_char_02_rgeblwe02-a_00"),
		TEXT("eff_1_char_02_rgeblwe03-a_00"),
		TEXT("eff_1_char_02_rgeblwe04-a_00"),
		TEXT("eff_1_char_02_scream01-a_00"),
		TEXT("eff_1_char_02_scream02-a_00"),
		TEXT("eff_1_char_02_supcry01-a_00"),
		TEXT("eff_1_char_03_aerob02-a_00"),
		TEXT("eff_1_char_03_aeroe01-a_00"),
		TEXT("eff_1_char_03_attrunb03-a_00"),
		TEXT("eff_1_char_03_attrune01-a_00"),
		TEXT("eff_1_char_03_attruns02-a_00"),
		TEXT("eff_1_char_03_ironbo01-a_00"),
		TEXT("eff_1_char_03_sfarw01-a_00"),
		TEXT("eff_1_char_03_trapth01-a_00"),
		TEXT("eff_1_char_03_trapth01-a_01"),
		TEXT("eff_1_char_03_trapth01-a_02"),
		TEXT("eff_1_char_04_adncw01-a_00"),
		TEXT("eff_1_char_04_bshotb03-a_00"),
		TEXT("eff_1_char_04_bshotw01-a_00"),
		TEXT("eff_1_char_04_devcnib02-a_00"),
		TEXT("eff_1_char_04_devcnie01-a_00"),
		TEXT("eff_1_char_04_devcnis03-a_00"),
		TEXT("eff_1_char_04_devcniw04-a_00"),
		TEXT("eff_1_char_04_drainw01-a_00"),
		TEXT("eff_1_char_04_mpsonw01-a_00"),
		TEXT("eff_1_char_04_rpolew01-a_00"),
		TEXT("eff_1_char_04_tshotw01-a_00"),
		TEXT("eff_1_char_04_vshotb02-a_00"),
		TEXT("eff_1_char_04_vshotw01-a_00"),
		TEXT("eff_1_char_04_wshotw01-a_00"),
		TEXT("eff_1_char_05_fatsta01-a_00"),
		TEXT("eff_1_char_05_fatsth01-a_00"),
		TEXT("eff_1_char_05_smashe01-a_00"),
		TEXT("eff_1_char_05_sspeb01-a_00"),
		TEXT("eff_1_char_05_sspee01-a_00"),
		TEXT("eff_1_char_05_strikee01-a_00"),
		TEXT("eff_1_char_05_wndslhb02-a_00"),
		TEXT("eff_1_char_05_wndslhe01-a_00"),
		TEXT("eff_1_char_06_bigmeob03-a_00"),
		TEXT("eff_1_char_06_bigmeog01-a_00"),
		TEXT("eff_1_char_06_bigmeow02-a_00"),
		TEXT("eff_1_char_06_brieg01-a_00"),
		TEXT("eff_1_char_06_bwaterg01-a_00"),
		TEXT("eff_1_char_06_bwaterw02-a_00"),
		TEXT("eff_1_char_06_cancelg01-a_00"),
		TEXT("eff_1_char_06_dakhlpb03-a_00"),
		TEXT("eff_1_char_06_dakhlpg01-a_00"),
		TEXT("eff_1_char_06_dakhlph02-a_00"),
		TEXT("eff_1_char_06_darbla01-a_00"),
		TEXT("eff_1_char_06_darbla01-a_01"),
		TEXT("eff_1_char_06_darbla01-a_02"),
		TEXT("eff_1_char_06_earthg01-a_00"),
		TEXT("eff_1_char_06_earthh02-a_00"),
		TEXT("eff_1_char_06_ebateg01-a_00"),
		TEXT("eff_1_char_06_ebatew02-a_00"),
		TEXT("eff_1_char_06_efireg01-a_00"),
		TEXT("eff_1_char_06_efireh02-a_00"),
		TEXT("eff_1_char_06_fballg01-a_00"),
		TEXT("eff_1_char_06_fballs04-a_00"),
		TEXT("eff_1_char_06_fballw02-a_00"),
		TEXT("eff_1_char_06_fballw03-a_00"),
		TEXT("eff_1_char_06_fbladg01-a_00"),
		TEXT("eff_1_char_06_firqukw01-a_00"),
		TEXT("eff_1_char_06_firqukw03-a_00"),
		TEXT("eff_1_char_06_hfireg01-a_00"),
		TEXT("eff_1_char_06_hfireh02-a_00"),
		TEXT("eff_1_char_06_lboltg01-a_00"),
		TEXT("eff_1_char_06_lbolth02-a_00"),
		TEXT("eff_1_char_06_lbolts04-a_00"),
		TEXT("eff_1_char_06_lboltw03-a_00"),
		TEXT("eff_1_char_06_lgtstmb03-a_00"),
		TEXT("eff_1_char_06_lgtstmc01-a_00"),
		TEXT("eff_1_char_06_lgtstmh02-a_00"),
		TEXT("eff_1_char_06_smlmeob03-a_00"),
		TEXT("eff_1_char_06_smlmeog01-a_00"),
		TEXT("eff_1_char_06_smlmeoh02-a_00"),
		TEXT("eff_1_char_06_stmtrpe03-a_00"),
		TEXT("eff_1_char_06_stmtrpg01-a_00"),
		TEXT("eff_1_char_06_stmtrpg02-a_00"),
		TEXT("eff_1_char_06_swdstme02-a_00"),
		TEXT("eff_1_char_06_swdstmg01-a_00"),
		TEXT("eff_1_char_06_swdstms03-a_00"),
		TEXT("eff_1_char_06_wbldg01-a_00"),
		TEXT("eff_1_char_06_wblds03-a_00"),
		TEXT("eff_1_char_06_wblds04-a_00"),
		TEXT("eff_1_char_06_wbldw02-a_00"),
		TEXT("eff_1_char_06_wholicg01-a_00"),
		TEXT("eff_1_char_06_wholich02-a_00"),
		TEXT("eff_1_char_06_wndstmb03-a_00"),
		TEXT("eff_1_char_06_wndstmg01-a_00"),
		TEXT("eff_1_char_06_wndstmw02-a_00"),
		TEXT("eff_1_char_07_dstarwe01-a_00"),
		TEXT("eff_1_char_07_fireragb03-a_00"),
		TEXT("eff_1_char_07_firerage01-a_00"),
		TEXT("eff_1_char_07_firerags02-a_00"),
		TEXT("eff_1_char_07_runfirb03-a_00"),
		TEXT("eff_1_char_07_runfirb04-a_00"),
		TEXT("eff_1_char_07_runfirb05-a_00"),
		TEXT("eff_1_char_07_runfire01-a_00"),
		TEXT("eff_1_char_07_runfire02-a_00"),
		TEXT("eff_1_char_99_adncb02-a_00"),
		TEXT("eff_1_char_99_attbomg-a_01"),
		TEXT("eff_1_char_99_attbomg-a_03"),
		TEXT("eff_1_char_99_attbomh-a_00"),
		TEXT("eff_1_char_99_attbomh-a_02"),
		TEXT("eff_1_char_99_attbomh-a_04"),
		TEXT("eff_1_char_99_attbomu-a_02"),
		TEXT("eff_1_char_99_bloarr01-a_00"),
		TEXT("eff_1_char_99_blosuc01-a_00"),
		TEXT("eff_1_char_99_briec01-a_00"),
		TEXT("eff_1_char_99_bshots02-a_00"),
		TEXT("eff_1_char_99_bstomb03-a_00"),
		TEXT("eff_1_char_99_bwaterb03-a_00"),
		TEXT("eff_1_char_99_crashb01-a_00"),
		TEXT("eff_1_char_99_crashb02-a_00"),
		TEXT("eff_1_char_99_damchgg-a_03"),
		TEXT("eff_1_char_99_defminb03-a_00"),
		TEXT("eff_1_char_99_defmine01-a_00"),
		TEXT("eff_1_char_99_defmine02-a_00"),
		TEXT("eff_1_char_99_defmine04-a_00"),
		TEXT("eff_1_char_99_drainb02-a_00"),
		TEXT("eff_1_char_99_drainb03-a_00"),
		TEXT("eff_1_char_99_drainc04-a_00"),
		TEXT("eff_1_char_99_drains02-a_00"),
		TEXT("eff_1_char_99_dswingb01-a_00"),
		TEXT("eff_1_char_99_dswingb02-a_00"),
		TEXT("eff_1_char_99_earthb03-a_00"),
		TEXT("eff_1_char_99_earthb04-a_00"),
		TEXT("eff_1_char_99_ebateb03-a_00"),
		TEXT("eff_1_char_99_efireb03-a_00"),
		TEXT("eff_1_char_99_evasionc-a_03"),
		TEXT("eff_1_char_99_evasionc-a_04"),
		TEXT("eff_1_char_99_evasiong-a_02"),
		TEXT("eff_1_char_99_exparr01-a_00"),
		TEXT("eff_1_char_99_expmovg-a_01"),
		TEXT("eff_1_char_99_fballb05-a_00"),
		TEXT("eff_1_char_99_fdrilb02-a_00"),
		TEXT("eff_1_char_99_fearg01-a_00"),
		TEXT("eff_1_char_99_focusg01-a_00"),
		TEXT("eff_1_char_99_grebom01-a_00"),
		TEXT("eff_1_char_99_grebom01-a_01"),
		TEXT("eff_1_char_99_grebom01-a_02"),
		TEXT("eff_1_char_99_grened01-a_00"),
		TEXT("eff_1_char_99_hblowb02-a_00"),
		TEXT("eff_1_char_99_hfireb03-a_00"),
		TEXT("eff_1_char_99_howlg01-a_00"),
		TEXT("eff_1_char_99_hpconc01-a_00"),
		TEXT("eff_1_char_99_hpconc02-a_00"),
		TEXT("eff_1_char_99_hpcong01-a_00"),
		TEXT("eff_1_char_99_impare01-a_00"),
		TEXT("eff_1_char_99_lboltb01-a_01"),
		TEXT("eff_1_char_99_lboltb01-a_02"),
		TEXT("eff_1_char_99_lboltb05-a_00"),
		TEXT("eff_1_char_99_move01-a_00"),
		TEXT("eff_1_char_99_move02-a_00"),
		TEXT("eff_1_char_99_mpsonb03-a_00"),
		TEXT("eff_1_char_99_mpsons02-a_00"),
		TEXT("eff_1_char_99_oswingb02-a_00"),
		TEXT("eff_1_char_99_pbladeb02-a_00"),
		TEXT("eff_1_char_99_pbladeb03-a_00"),
		TEXT("eff_1_char_99_perdmgg-a_02"),
		TEXT("eff_1_char_99_perdmgu-a_03"),
		TEXT("eff_1_char_99_poiarr01-a_00"),
		TEXT("eff_1_char_99_redmgc-a_02"),
		TEXT("eff_1_char_99_redmgu-a_01"),
		TEXT("eff_1_char_99_retdmg01-a_00"),
		TEXT("eff_1_char_99_roarug02-a_00"),
		TEXT("eff_1_char_99_rpoleb03-a_00"),
		TEXT("eff_1_char_99_rpoles02-a_00"),
		TEXT("eff_1_char_99_sfarb02-a_00"),
		TEXT("eff_1_char_99_skigurdc-a_04"),
		TEXT("eff_1_char_99_skigurdg-a_02"),
		TEXT("eff_1_char_99_skigurdu-a_03"),
		TEXT("eff_1_char_99_slpdus01-a_00"),
		TEXT("eff_1_char_99_smashb02-a_00"),
		TEXT("eff_1_char_99_soulcallh-a_00"),
		TEXT("eff_1_char_99_soullink01-a_00"),
		TEXT("eff_1_char_99_soullink02-a_00"),
		TEXT("eff_1_char_99_soullink03-a_00"),
		TEXT("eff_1_char_99_soulmatec-a_01"),
		TEXT("eff_1_char_99_soulritec-a_00"),
		TEXT("eff_1_char_99_soulritec-a_01"),
		TEXT("eff_1_char_99_soulritec-a_03"),
		TEXT("eff_1_char_99_soulritec-a_04"),
		TEXT("eff_1_char_99_sspeb02-a_00"),
		TEXT("eff_1_char_99_straim01-a_00"),
		TEXT("eff_1_char_99_straim01-a_01"),
		TEXT("eff_1_char_99_straim01-a_02"),
		TEXT("eff_1_char_99_strikeb02-a_00"),
		TEXT("eff_1_char_99_stydmg01-a_00"),
		TEXT("eff_1_char_99_tforeh01-a_00"),
		TEXT("eff_1_char_99_tshotb03-a_00"),
		TEXT("eff_1_char_99_tshots02-a_00"),
		TEXT("eff_1_char_99_wbldb05-a_00"),
		TEXT("eff_1_char_99_wblde01-a_00"),
		TEXT("eff_1_char_99_wholicb03-a_00"),
		TEXT("eff_1_char_99_wshotb03-a_00"),
		TEXT("eff_1_char_99_wshots02-a_00"),
		TEXT("eff_2_char_02_dridg501-a_00"),
		TEXT("eff_2_char_02_dridg501-a_01"),
		TEXT("eff_2_char_02_dridg501-a_02"),
		TEXT("eff_2_char_99_abdmge01-a_00"),
		TEXT("eff_2_char_99_abidw01-a_00"),
		TEXT("eff_2_char_99_abidw01-a_01"),
		TEXT("eff_2_char_99_abidw01-a_02"),
		TEXT("eff_2_char_99_absdf01-a_00"),
		TEXT("eff_2_char_99_absdf01-a_01"),
		TEXT("eff_2_char_99_absdf01-a_02"),
		TEXT("eff_2_char_99_adddg101-a_00"),
		TEXT("eff_2_char_99_adddg101-a_02"),
		TEXT("eff_2_char_99_adncc01-a_00"),
		TEXT("eff_2_char_99_alcrup01-a_00"),
		TEXT("eff_2_char_99_aldre01-a_00"),
		TEXT("eff_2_char_99_aldre01-a_01"),
		TEXT("eff_2_char_99_aldre01-a_02"),
		TEXT("eff_2_char_99_attdow01-a_00"),
		TEXT("eff_2_char_99_attdow01-a_01"),
		TEXT("eff_2_char_99_attdow01-a_02"),
		TEXT("eff_2_char_99_attdw101-a_00"),
		TEXT("eff_2_char_99_attdw101-a_01"),
		TEXT("eff_2_char_99_attdw101-a_02"),
		TEXT("eff_2_char_99_attdw301-a_00"),
		TEXT("eff_2_char_99_attdw301-a_01"),
		TEXT("eff_2_char_99_attdw301-a_02"),
		TEXT("eff_2_char_99_attsdw01-a_00"),
		TEXT("eff_2_char_99_attsdw01-a_01"),
		TEXT("eff_2_char_99_attsdw01-a_02"),
		TEXT("eff_2_char_99_attup101-a_00"),
		TEXT("eff_2_char_99_attup101-a_01"),
		TEXT("eff_2_char_99_attup101-a_02"),
		TEXT("eff_2_char_99_attup201-a_00"),
		TEXT("eff_2_char_99_attup201-a_01"),
		TEXT("eff_2_char_99_attup201-a_02"),
		TEXT("eff_2_char_99_balace01-a_00"),
		TEXT("eff_2_char_99_barier01-a_00"),
		TEXT("eff_2_char_99_basarr01-a_00"),
		TEXT("eff_2_char_99_bleedc01-a_00"),
		TEXT("eff_2_char_99_bleedc01-a_01"),
		TEXT("eff_2_char_99_bleedc01-a_02"),
		TEXT("eff_2_char_99_blefad01-a_00"),
		TEXT("eff_2_char_99_blefad01-a_01"),
		TEXT("eff_2_char_99_blefad01-a_02"),
		TEXT("eff_2_char_99_bloarr01-a_00"),
		TEXT("eff_2_char_99_blosuc01-a_00"),
		TEXT("eff_2_char_99_blosuc01-a_01"),
		TEXT("eff_2_char_99_blosuc01-a_02"),
		TEXT("eff_2_char_99_blosuc02-a_00"),
		TEXT("eff_2_char_99_blucup01-a_00"),
		TEXT("eff_2_char_99_briec01-a_00"),
		TEXT("eff_2_char_99_challe01-a_00"),
		TEXT("eff_2_char_99_challe01-a_01"),
		TEXT("eff_2_char_99_challe01-a_02"),
		TEXT("eff_2_char_99_chieff01-a_00"),
		TEXT("eff_2_char_99_conmas01-a_00"),
		TEXT("eff_2_char_99_cridn201-a_00"),
		TEXT("eff_2_char_99_cridn201-a_01"),
		TEXT("eff_2_char_99_cridn201-a_02"),
		TEXT("eff_2_char_99_criup201-a_00"),
		TEXT("eff_2_char_99_criup201-a_01"),
		TEXT("eff_2_char_99_criup201-a_02"),
		TEXT("eff_2_char_99_ctimdn01-a_00"),
		TEXT("eff_2_char_99_dash01-a_00"),
		TEXT("eff_2_char_99_deacur01-a_00"),
		TEXT("eff_2_char_99_deacur01-a_01"),
		TEXT("eff_2_char_99_deacur01-a_02"),
		TEXT("eff_2_char_99_defdw101-a_00"),
		TEXT("eff_2_char_99_defdw101-a_01"),
		TEXT("eff_2_char_99_defdw101-a_02"),
		TEXT("eff_2_char_99_defdw201-a_00"),
		TEXT("eff_2_char_99_defdw201-a_01"),
		TEXT("eff_2_char_99_defdw201-a_02"),
		TEXT("eff_2_char_99_defdw401-a_00"),
		TEXT("eff_2_char_99_defdw401-a_01"),
		TEXT("eff_2_char_99_defdw401-a_02"),
		TEXT("eff_2_char_99_defdwe01-a_00"),
		TEXT("eff_2_char_99_defenc01-a_00"),
		TEXT("eff_2_char_99_defenc01-a_01"),
		TEXT("eff_2_char_99_defenc01-a_02"),
		TEXT("eff_2_char_99_defmine01-a_00"),
		TEXT("eff_2_char_99_defmine02-a_00"),
		TEXT("eff_2_char_99_defmine03-a_00"),
		TEXT("eff_2_char_99_defmine04-a_00"),
		TEXT("eff_2_char_99_defmine05-a_00"),
		TEXT("eff_2_char_99_defsc101-a_00"),
		TEXT("eff_2_char_99_defsr101-a_00"),
		TEXT("eff_2_char_99_defsr101-a_01"),
		TEXT("eff_2_char_99_defsr101-a_02"),
		TEXT("eff_2_char_99_defup101-a_00"),
		TEXT("eff_2_char_99_defup101-a_01"),
		TEXT("eff_2_char_99_defup101-a_02"),
		TEXT("eff_2_char_99_defup201-a_00"),
		TEXT("eff_2_char_99_defup201-a_01"),
		TEXT("eff_2_char_99_defup201-a_02"),
		TEXT("eff_2_char_99_dfdnar01-a_00"),
		TEXT("eff_2_char_99_disarr01-a_00"),
		TEXT("eff_2_char_99_divarr01-a_00"),
		TEXT("eff_2_char_99_dmgbak01-a_00"),
		TEXT("eff_2_char_99_dmgbak01-a_01"),
		TEXT("eff_2_char_99_dmgbak01-a_02"),
		TEXT("eff_2_char_99_dot08t01-a_00"),
		TEXT("eff_2_char_99_dot08t01-a_01"),
		TEXT("eff_2_char_99_dot08t01-a_02"),
		TEXT("eff_2_char_99_dot301-a_00"),
		TEXT("eff_2_char_99_dot301-a_01"),
		TEXT("eff_2_char_99_dot301-a_02"),
		TEXT("eff_2_char_99_dousho01-a_00"),
		TEXT("eff_2_char_99_eleski01-a_00"),
		TEXT("eff_2_char_99_evadw201-a_00"),
		TEXT("eff_2_char_99_evadw201-a_01"),
		TEXT("eff_2_char_99_evadw201-a_02"),
		TEXT("eff_2_char_99_evaup201-a_00"),
		TEXT("eff_2_char_99_exparr01-a_00"),
		TEXT("eff_2_char_99_expplt01-a_00"),
		TEXT("eff_2_char_99_expplt01-a_01"),
		TEXT("eff_2_char_99_expplt01-a_02"),
		TEXT("eff_2_char_99_fatblo01-a_00"),
		TEXT("eff_2_char_99_fbladu01-a_00"),
		TEXT("eff_2_char_99_fbladu01-a_01"),
		TEXT("eff_2_char_99_fbladu01-a_02"),
		TEXT("eff_2_char_99_fearu01-a_00"),
		TEXT("eff_2_char_99_fearu01-a_01"),
		TEXT("eff_2_char_99_fearu01-a_02"),
		TEXT("eff_2_char_99_finhid01-a_00"),
		TEXT("eff_2_char_99_finhid01-a_01"),
		TEXT("eff_2_char_99_firtim01-a_00"),
		TEXT("eff_2_char_99_firtim01-a_01"),
		TEXT("eff_2_char_99_firtim01-a_02"),
		TEXT("eff_2_char_99_flamec01-a_00"),
		TEXT("eff_2_char_99_flamec01-a_01"),
		TEXT("eff_2_char_99_flamec01-a_02"),
		TEXT("eff_2_char_99_focusu01-a_00"),
		TEXT("eff_2_char_99_focusu01-a_01"),
		TEXT("eff_2_char_99_focusu01-a_02"),
		TEXT("eff_2_char_99_freez101-a_00"),
		TEXT("eff_2_char_99_freez101-a_01"),
		TEXT("eff_2_char_99_freez101-a_02"),
		TEXT("eff_2_char_99_freez201-a_00"),
		TEXT("eff_2_char_99_freez201-a_01"),
		TEXT("eff_2_char_99_freez201-a_02"),
		TEXT("eff_2_char_99_freez301-a_00"),
		TEXT("eff_2_char_99_freez301-a_01"),
		TEXT("eff_2_char_99_freez301-a_02"),
		TEXT("eff_2_char_99_harski01-a_00"),
		TEXT("eff_2_char_99_hatedw01-a_00"),
		TEXT("eff_2_char_99_hatedw01-a_01"),
		TEXT("eff_2_char_99_hatedw01-a_02"),
		TEXT("eff_2_char_99_hatedw02-a_00"),
		TEXT("eff_2_char_99_hblowc01-a_00"),
		TEXT("eff_2_char_99_hill01-a_00"),
		TEXT("eff_2_char_99_hill01-a_01"),
		TEXT("eff_2_char_99_hill01-a_02"),
		TEXT("eff_2_char_99_hitdwn01-a_00"),
		TEXT("eff_2_char_99_hitdwn01-a_01"),
		TEXT("eff_2_char_99_hitdwn01-a_02"),
		TEXT("eff_2_char_99_howlu01-a_00"),
		TEXT("eff_2_char_99_howlu01-a_01"),
		TEXT("eff_2_char_99_howlu01-a_02"),
		TEXT("eff_2_char_99_hpconu01-a_00"),
		TEXT("eff_2_char_99_hpconu01-a_01"),
		TEXT("eff_2_char_99_hpconu01-a_02"),
		TEXT("eff_2_char_99_hpcurs01-a_00"),
		TEXT("eff_2_char_99_hpcurs01-a_01"),
		TEXT("eff_2_char_99_hpcurs01-a_02"),
		TEXT("eff_2_char_99_hpmove02-a_00"),
		TEXT("eff_2_char_99_hpnwas01-a_00"),
		TEXT("eff_2_char_99_hpnwas01-a_01"),
		TEXT("eff_2_char_99_hpnwas01-a_02"),
		TEXT("eff_2_char_99_hprc201-a_00"),
		TEXT("eff_2_char_99_hprc201-a_01"),
		TEXT("eff_2_char_99_hprc201-a_02"),
		TEXT("eff_2_char_99_hpreco01-a_00"),
		TEXT("eff_2_char_99_ltningu01-a_01"),
		TEXT("eff_2_char_99_magat201-a_00"),
		TEXT("eff_2_char_99_magat201-a_01"),
		TEXT("eff_2_char_99_magat201-a_02"),
		TEXT("eff_2_char_99_magstr01-a_00"),
		TEXT("eff_2_char_99_manasu01-a_00"),
		TEXT("eff_2_char_99_manasu01-a_01"),
		TEXT("eff_2_char_99_manasu01-a_02"),
		TEXT("eff_2_char_99_manshi01-a_00"),
		TEXT("eff_2_char_99_manwas01-a_00"),
		TEXT("eff_2_char_99_manwas01-a_01"),
		TEXT("eff_2_char_99_manwas01-a_02"),
		TEXT("eff_2_char_99_mastu01-a_00"),
		TEXT("eff_2_char_99_mastu01-a_02"),
		TEXT("eff_2_char_99_melstr01-a_00"),
		TEXT("eff_2_char_99_morblw-a_00"),
		TEXT("eff_2_char_99_morblw-a_01"),
		TEXT("eff_2_char_99_morblw-a_02"),
		TEXT("eff_2_char_99_movdw01-a_00"),
		TEXT("eff_2_char_99_movdw01-a_01"),
		TEXT("eff_2_char_99_movdw01-a_02"),
		TEXT("eff_2_char_99_movup01-a_00"),
		TEXT("eff_2_char_99_mpcodw01-a_00"),
		TEXT("eff_2_char_99_mpcodw01-a_01"),
		TEXT("eff_2_char_99_mpcodw01-a_02"),
		TEXT("eff_2_char_99_mpcurs01-a_00"),
		TEXT("eff_2_char_99_mpcurs01-a_01"),
		TEXT("eff_2_char_99_mpcurs01-a_02"),
		TEXT("eff_2_char_99_mprec201-a_00"),
		TEXT("eff_2_char_99_mprec201-a_01"),
		TEXT("eff_2_char_99_mprec201-a_02"),
		TEXT("eff_2_char_99_poiarr01-a_00"),
		TEXT("eff_2_char_99_poiup101-a_00"),
		TEXT("eff_2_char_99_poiup101-a_01"),
		TEXT("eff_2_char_99_poiup101-a_02"),
		TEXT("eff_2_char_99_poiup201-a_00"),
		TEXT("eff_2_char_99_poiup201-a_01"),
		TEXT("eff_2_char_99_poiup201-a_02"),
		TEXT("eff_2_char_99_poiup301-a_00"),
		TEXT("eff_2_char_99_poiup301-a_01"),
		TEXT("eff_2_char_99_poiup301-a_02"),
		TEXT("eff_2_char_99_poiup401-a_00"),
		TEXT("eff_2_char_99_poiup401-a_01"),
		TEXT("eff_2_char_99_poiup401-a_02"),
		TEXT("eff_2_char_99_poiup501-a_00"),
		TEXT("eff_2_char_99_poiup501-a_01"),
		TEXT("eff_2_char_99_poiup501-a_02"),
		TEXT("eff_2_char_99_poiup601-a_00"),
		TEXT("eff_2_char_99_quickn01-a_00"),
		TEXT("eff_2_char_99_rangu01-a_00"),
		TEXT("eff_2_char_99_rangu01-a_01"),
		TEXT("eff_2_char_99_rangu01-a_02"),
		TEXT("eff_2_char_99_ranstr01-a_00"),
		TEXT("eff_2_char_99_regdwn01-a_00"),
		TEXT("eff_2_char_99_regdwn01-a_01"),
		TEXT("eff_2_char_99_regdwn01-a_02"),
		TEXT("eff_2_char_99_regdwn02-a_00"),
		TEXT("eff_2_char_99_regdwn02-a_01"),
		TEXT("eff_2_char_99_regdwn02-a_02"),
		TEXT("eff_2_char_99_regdwn03-a_00"),
		TEXT("eff_2_char_99_regdwn03-a_01"),
		TEXT("eff_2_char_99_regdwn03-a_02"),
		TEXT("eff_2_char_99_regdwn04-a_00"),
		TEXT("eff_2_char_99_regdwn04-a_01"),
		TEXT("eff_2_char_99_regdwn04-a_02"),
		TEXT("eff_2_char_99_regdwn05-a_00"),
		TEXT("eff_2_char_99_regdwn05-a_01"),
		TEXT("eff_2_char_99_regdwn05-a_02"),
		TEXT("eff_2_char_99_regdwn06-a_00"),
		TEXT("eff_2_char_99_regdwn06-a_01"),
		TEXT("eff_2_char_99_regdwn06-a_02"),
		TEXT("eff_2_char_99_resur201-a_00"),
		TEXT("eff_2_char_99_retdmg01-a_00"),
		TEXT("eff_2_char_99_retdmg01-a_01"),
		TEXT("eff_2_char_99_retdmg01-a_02"),
		TEXT("eff_2_char_99_rpoleu01-a_01"),
		TEXT("eff_2_char_99_savsou01-a_00"),
		TEXT("eff_2_char_99_sbact101-a_00"),
		TEXT("eff_2_char_99_sbact180-a_00"),
		TEXT("eff_2_char_99_sbact201-a_00"),
		TEXT("eff_2_char_99_scrup01-a_00"),
		TEXT("eff_2_char_99_shobom01-a_00"),
		TEXT("eff_2_char_99_shobom01-a_01"),
		TEXT("eff_2_char_99_shobom01-a_02"),
		TEXT("eff_2_char_99_shocku01-a_00"),
		TEXT("eff_2_char_99_shocku01-a_01"),
		TEXT("eff_2_char_99_shocku01-a_02"),
		TEXT("eff_2_char_99_silenc01-a_00"),
		TEXT("eff_2_char_99_silenc01-a_01"),
		TEXT("eff_2_char_99_silenc01-a_02"),
		TEXT("eff_2_char_99_skcriu01-a_00"),
		TEXT("eff_2_char_99_skcriu01-a_01"),
		TEXT("eff_2_char_99_skcriu01-a_02"),
		TEXT("eff_2_char_99_sleep01-a_00"),
		TEXT("eff_2_char_99_sleep01-a_01"),
		TEXT("eff_2_char_99_sleep01-a_02"),
		TEXT("eff_2_char_99_strikec01-a_00"),
		TEXT("eff_2_char_99_strup1-a_00"),
		TEXT("eff_2_char_99_strup1-a_01"),
		TEXT("eff_2_char_99_strup1-a_02"),
		TEXT("eff_2_char_99_telepo01-a_00"),
		TEXT("eff_2_char_99_timbom01-a_00"),
		TEXT("eff_2_char_99_timbom01-a_01"),
		TEXT("eff_2_char_99_timbom01-a_02"),
		TEXT("eff_2_char_99_timbom01-b_00"),
		TEXT("eff_2_char_99_timbom01-b_01"),
		TEXT("eff_2_char_99_timbom01-b_02"),
		TEXT("eff_2_char_99_wepead01-a_00"),
		TEXT("eff_2_char_99_wepead01-a_01"),
		TEXT("eff_2_char_99_wepead01-a_02"),
		TEXT("eff_2_hedfor01-a_00"),
	};

	// Ключ — имя с «_» вместо «-» (TMap<FString> сравнивает без учёта регистра), значение — имя .jeb.
	inline const TMap<FString, FString>& FxJebDashTable()
	{
		static const TMap<FString, FString> Table = []()
		{
			TMap<FString, FString> M;
			M.Reserve(UE_ARRAY_COUNT(kFxJebDashNames));
			for (const TCHAR* Jeb : kFxJebDashNames)
			{
				const FString S(Jeb);
				if (!S.IsEmpty()) { M.Add(S.Replace(TEXT("-"), TEXT("_")), S); }
			}
			return M;
		}();
		return Table;
	}

	// Имя класса эффект-BP «BP_Fx_<jeb с «_»>_C» → имя .jeb (с дефисом, если оно есть в таблице выше).
	inline FString JebFromFxClassName(const FString& ClassName)
	{
		FString Jeb = ClassName;
		Jeb.RemoveFromEnd(TEXT("_C"));
		Jeb.RemoveFromStart(TEXT("BP_Fx_"));
		if (const FString* Dashed = FxJebDashTable().Find(Jeb)) { return *Dashed; }   // ★ ИСПРАВЛЕНИЕ D4
		return Jeb;
	}

	// Объектный путь звука: /Game/AsdaAssets/<Folder>/<N>.<N>, N = ToAssetName(имя) (пробел → «_»).
	inline FString SoundObject(const TCHAR* Folder, const FString& Name)
	{
		const FString N = AsdaMapRegistry::ToAssetName(Name);
		return FString::Printf(TEXT("/Game/AsdaAssets/%s/%s.%s"), Folder, *N, *N);
	}

	// Объектный путь UI-атласа JUI: Textures/JUI/<n>.<n> (имя без расширения — срезает вызывающий).
	inline FString JuiTextureObject(const FString& Name)
	{
		return FString::Printf(TEXT("/Game/AsdaAssets/Textures/JUI/%s.%s"), *Name, *Name);
	}

	// Каталог текстур карты: Textures/Maps/<ToAssetName(имя папки карты)> (★ ИСПРАВЛЕНИЕ D2: с приведением имени).
	inline FString MapTexDir(const FString& MapName)
	{
		const FString A = AsdaMapRegistry::ToAssetName(MapName);
		return FString::Printf(TEXT("/Game/AsdaAssets/Textures/Maps/%s"), *A);
	}

	// Каталог картинок загрузки карты: Textures/Maps/<ToAssetName(DirName)>/load (★ ИСПРАВЛЕНИЕ D1: с приведением имени).
	inline FString MapLoadDir(const FString& DirName)
	{
		const FString A = AsdaMapRegistry::ToAssetName(DirName);
		return FString::Printf(TEXT("/Game/AsdaAssets/Textures/Maps/%s/load"), *A);
	}

	// Объектный путь портрета NPC: Textures/NPC/Portrait/<ModelName>.<ModelName>.
	inline FString NpcPortraitObject(const FString& Model)
	{
		return FString::Printf(TEXT("/Game/AsdaAssets/Textures/NPC/Portrait/%s.%s"), *Model, *Model);
	}

	// Пакетный путь → объектный: «/A/B/N» → «/A/B/N.N»; если у листа уже есть «.» или ключ не путь — как есть.
	//   Одна точка вместо копий в UAsdaTextureSubsystem::ResolveAndCache и UAsdaModelAnimSubsystem::LoadOrCache.
	inline FString ObjectPathFromPackage(const FString& Key)
	{
		int32 SlashIdx = INDEX_NONE;
		if (Key.StartsWith(TEXT("/")) && Key.FindLastChar(TEXT('/'), SlashIdx))
		{
			const FString Leaf = Key.Mid(SlashIdx + 1);
			if (!Leaf.Contains(TEXT(".")))
			{
				return Key + TEXT(".") + Leaf;
			}
		}
		return Key;
	}

	// Пакет → «<пакет>.<короткое имя>» без условий; короткое имя — после последнего «/» (как FPackageName::GetShortName).
	inline FString PackageObjectPath(const FString& Pkg)
	{
		int32 SlashIdx = INDEX_NONE;
		Pkg.FindLastChar(TEXT('/'), SlashIdx);
		return Pkg + TEXT(".") + Pkg.Mid(SlashIdx + 1);
	}

	// Путь WBP → путь класса Blueprint: «/A/WBP_X» → «/A/WBP_X.WBP_X_C», «/A/WBP_X.WBP_X» → «…_C»; с «_C» — как есть.
	inline FString WidgetClassPath(const FString& WidgetClassPathIn)
	{
		FString Path = WidgetClassPathIn;
		if (!Path.EndsWith(TEXT("_C")))
		{
			if (!Path.Contains(TEXT(".")))
			{
				int32 Slash;
				if (Path.FindLastChar(TEXT('/'), Slash))
				{
					Path = Path + TEXT(".") + Path.Mid(Slash + 1);
				}
			}
			Path += TEXT("_C");
		}
		return Path;
	}
}
