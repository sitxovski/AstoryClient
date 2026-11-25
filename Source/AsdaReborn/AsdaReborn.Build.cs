// Copyright (c) 2026 @sitxovski (Discord)
// AsdaReborn — игровой модуль (представление + бридж к ядру/сети).
using UnrealBuildTool;

public class AsdaReborn : ModuleRules
{
	public AsdaReborn(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// ★ Сет B (05.09.2026): unity-сборка ВЫКЛЮЧЕНА. Добавление одного нового .cpp (World/AsdaCapsuleShadow.cpp) сдвинуло
		//   раскладку unity-бакетов, и 10 бакетов Module.AsdaReborn.N.cpp упали на 129 коллизиях file-static символов из разных
		//   UI-.cpp (Colored/SetVis/kWindowId/… — C2084/C2374/C2872; перечень ошибок — Reports/render_sets_2026-09-05/setB_apply_phase2.out и setB_apply_report.md §4;
		//   setB_build.log — это уже успешная попытка 3).
		//   Одноимённые static-хелперы в разных TU — легальный C++; ломает их только склейка unity. Без unity сборка
		//   детерминирована и не зависит от состава файлов; цена — дольше полная пересборка (123 .cpp по одному, PCH остаётся).
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore",
			// ★ AsdaFormats УБРАН: парсеры оригинальных форматов (std::ifstream) — только для импорта,
			//   модуль переведён в Type="Editor"; его единственный потребитель AAsdaMapActor уехал
			//   в AsdaImporters. Игровой модуль файлов оригинала не читает вовсе.
			"AsdaCore", "AsdaNet", "ProceduralMeshComponent",
			// ★ Сет E-4 (07.09.2026): UAsdaRenderSettings : UDeveloperSettings (Project Settings → Asda → Render; пресеты локальных светов)
			"DeveloperSettings",
			// ★ Пасхалка «покататься»: штатная система машин UE (Chaos Vehicles).
			//   Физику и настройку берём из готовых Blueprint-ов Vehicle Variety Pack, а ВВОД даём
			//   свой — проект на КЛАССИЧЕСКОМ Input (`BindAxisKey`), Enhanced Input тут не включён.
			"ChaosVehicles"
		});
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate", "SlateCore",
			"UMG",                 // редактируемый UI: UUserWidget/Widget Blueprint (правило ui-editable-in-ue)
			"RenderCore", "RHI",   // EPixelFormat / CreateTransient / BulkData mip lock
			// ★★★ ВОЛНА Ф8 (10.09.2026): МОДУЛЬ AsdaFlare ЗДЕСЬ СОЗНАТЕЛЬНО НЕ ПОДКЛЮЧЁН.
			//   Остановленная волна собственного блика оставила ДЕРЕВО НЕСОБИРАЕМЫМ: `AsdaFlareSubsystem.cpp`
			//   лежал в этом модуле и включал `AsdaFlareRender.h` из модуля AsdaFlare, который не был
			//   зарегистрирован ни в .uproject, ни здесь (fatal C1083). Попытка его подключить вскрыла,
			//   что волна не дошла до конца вообще: подсистема читает три несуществующих поля
			//   (`bLensFlare`, `FlareIntensity`, `FlareSet`), а расширение вида — снятый в UE 5.8 `FSceneView::ViewRect`.
			//   Доделывать блик этой волной было прямо запрещено, поэтому его файлы ЦЕЛИКОМ
			//   СОХРАНЕНЫ в `_wip/flare/` (та же структура каталогов) и убраны из сборки — дерево
			//   собирается, работа не потеряна. На месте остались только `AsdaFlareSettings.*`
			//   (они собираются и служат моделью данных блика), а признак «испускает блик» уже
			//   заложен ПОЭМИТТЕРНЫМ (FAsdaFxEmitterGlow::bEmitsFlare), как и требовалось.
			"ImageWrapper",        // PNG/BMP fallback в AsdaTexLoader
			"LevelSequence", "MovieScene",  // перелёт камеры логина = LS_LoginFlight (Editor-Friendly, правится в Sequencer)
			"AssetRegistry",       // LoadManager: перечисление load-текстур карт (PickLoadingTexture)
			// ★ Волна В2 античита (12.10.2026): FPakPlatformFile::GetPakSigningFailureHandlerData — обработчик
			//   отказа подписи pak/IoStore (Private/Crypto/AsdaPakIntegrity.cpp).
			"PakFile",
			// ★ EAC Э1 (16.10.2026): клиентский Easy Anti-Cheat — UAsdaAntiCheatSubsystem (вызовы из AsdaNetSubsystem: хост LoginManager,
			//   MD5 пароля для AsdaId, сброс сессии, разрыв по KICK/нарушению). Модуль: Source/AsdaAntiCheat; отчёт eac/CLIENT_REPORT.md.
			"AsdaAntiCheat",
			// ★ 18.10.2026 проверка новой версии на ходу (UAsdaUpdateCheckSubsystem): GET keyring.json/version.json с
			//   @@ASTORY_UPDATE_HOST@@ (HTTP), разбор конвертов (Json), подпись Ed25519 и SHA-256 — OpenSSL 1.1.1t движка (как лаунчер).
			"HTTP", "Json", "OpenSSL"
		});
		if (Target.bBuildEditor)
		{
			// AsdaEffectActor: позиция камеры editor-вьюпорта для билборда частиц.
			// ★ UMGEditor/Kismet/AssetTools больше не нужны: генератор JUI→UMG (UAsdaUmgBuilder) уехал
			//   в editor-модуль AsdaImporters — иначе target Game не собирался вовсе (UHT не знает
			//   UWidgetBlueprint без UMGEditor).
			PrivateDependencyModuleNames.Add("UnrealEd");
			// ★ 27.10.2026: буфер обмена для кнопок «Копировать/Вставить свет» AAsdaEffectActor (FPlatformApplicationMisc).
			PrivateDependencyModuleNames.Add("ApplicationCore");
		}
		// ★ 19.10.2026 (журналы релиза, часть A): в Shipping UE_LOG/UE_CLOG/UE_LOG_REF наших модулей пишут в запечатанный журнал
		//   сессии — движок установлен с NO_LOGGING, поэтому макросы переопределяет принудительно подключаемый заголовок
		//   (разбор — Source/AsdaCore/Public/AsdaShippingLog.h). Вне Shipping ничего не меняется.
		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			ForceIncludeFiles.Add(System.IO.Path.GetFullPath(System.IO.Path.Combine(ModuleDirectory, "..", "AsdaCore", "Public", "AsdaShippingLog.h")));
		}
	}
}
