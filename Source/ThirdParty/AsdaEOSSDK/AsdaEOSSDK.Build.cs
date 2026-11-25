// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEOSSDK — внешний (ThirdParty) модуль EOS SDK 1.19.2.1 для клиентского античита EAC (Э1-Г, 16.10.2026).
//
// ЗАЧЕМ СВОЙ МОДУЛЬ. В движке UE 5.8 есть модуль `EOSSDK` (SDK 1.19.1.2, Engine/Source/ThirdParty/EOSSDK) и плагины
//   EOSShared/OnlineSubsystemEOS. Античит в OSS EOS не входит (eac_docs_research.md §6), а SDK нам нужен свой — 1.19.2.1
//   (план EAC_PLAN.md §1.6). Имя модуля НЕ `EOSSDK`, чтобы не столкнуться с движковым; плагины EOSShared/OSS EOS НЕ включены.
//   Источник файлов — `@@ASTORY_DEV_ROOT@@\ThirdParty\EOS-SDK-58105819-Release-v1.19.2.1\SDK` (копия без изменений: Include, Lib, Bin):
//   md5 DLL a1d5113a8709bd31d070aa5a78a766ef, md5 lib f6f61392814fdbd3448e212e2666fee5.
//
// КАК ГРУЗИТСЯ DLL. Импорт-библиотека линкуется, а сама DLL — ОТЛОЖЕННО (delay-load): клиентский модуль
//   `AsdaAntiCheat` (FAsdaEosSdk::Load) сначала загружает её ПОЛНЫМ путём из папки своего бинарника. Причина — у движка
//   в Engine/Binaries/Win64 лежит одноимённая EOSSDK-Win64-Shipping.dll версии 1.19.1.2; загрузка по имени могла бы взять её.
//   ★ ред. 3 (E1-17, 16.10.2026): DLL кладётся в ПОДПАПКУ `AsdaEOS` рядом с бинарником, который линкует модуль
//   ($(BinaryOutputDir)/AsdaEOS): у редактора — Binaries/Win64/AsdaEOS проекта, у монолитной игры — AsdaReborn/Binaries/
//   Win64/AsdaEOS. Причина: движковый плагин EOSShared (его включают Fab и MetaHumanSDK только для Editor-таргета) ищет DLL
//   по имени в Binaries/Win64 ПРОЕКТА, затем движка (EOSSDKManager.cpp, LoadSDKHandle) — раньше он находил нашу 1.19.2.1 и
//   грузил её вместо движковой 1.19.1.2. Из подпапки он её не видит и берёт свою. Наш модуль грузит DLL ПОЛНЫМ путём из
//   подпапки, а отложенный импорт привязывается к ней хуком delay-load (`__pfnDliNotifyHook2`, AsdaEosSdk.cpp) — две копии
//   с одним именем в одном процессе (редактор) не путаются. В Engine/Binaries ничего не пишется.
//
// ПЛАТФОРМЫ. Только Win64 x64 (решение владельца: клиент — Windows x64). Иначе WITH_ASDA_EOSSDK=0, модуль античита
//   собирается без EOS и сообщает «EAC недоступен».
using System.IO;
using UnrealBuildTool;

public class AsdaEOSSDK : ModuleRules
{
	// Имя DLL SDK (Win64 x64) — общее для delay-load, копирования и загрузки полным путём.
	private const string DllName = "EOSSDK-Win64-Shipping.dll";
	// ★ ред. 3 (E1-17): подпапка DLL рядом с бинарником (EOSShared редактора туда не смотрит).
	private const string DllSubdir = "AsdaEOS";

	public AsdaEOSSDK(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		// Корень копии SDK внутри модуля.
		string SdkDir = Path.Combine(ModuleDirectory, "SDK");
		PublicSystemIncludePaths.Add(Path.Combine(SdkDir, "Include"));

		if (Target.Platform == UnrealTargetPlatform.Win64 && Target.Architecture == UnrealArch.X64)
		{
			PublicDefinitions.Add("WITH_ASDA_EOSSDK=1");
			PublicDefinitions.Add("ASDA_EOSSDK_DLL_NAME=\"" + DllName + "\"");
			PublicDefinitions.Add("ASDA_EOSSDK_DLL_SUBDIR=\"" + DllSubdir + "\"");
			// Lib и Bin SDK в git не входят (их копирует пользователь) — без них понятная ошибка вместо LNK1181.
			string LibPath = Path.Combine(SdkDir, "Lib", "EOSSDK-Win64-Shipping.lib");
			string DllPath = Path.Combine(SdkDir, "Bin", DllName);
			if (!File.Exists(LibPath) || !File.Exists(DllPath))
			{
				throw new BuildException("AsdaEOSSDK: нет " + LibPath + " и/или " + DllPath +
					". Скопируйте папки SDK/Lib и SDK/Bin из EOS SDK 1.19.2.1 в Source/ThirdParty/AsdaEOSSDK/SDK — см. Prerequisites/README.md");
			}
			PublicAdditionalLibraries.Add(LibPath);
			PublicDelayLoadDLLs.Add(DllName);
			// Копия DLL в подпапку рядом с бинарником, который линкует модуль (и стейджинг в упаковку — NonUFS).
			RuntimeDependencies.Add("$(BinaryOutputDir)/" + DllSubdir + "/" + DllName, DllPath, StagedFileType.NonUFS);
		}
		else
		{
			PublicDefinitions.Add("WITH_ASDA_EOSSDK=0");
		}
	}
}
