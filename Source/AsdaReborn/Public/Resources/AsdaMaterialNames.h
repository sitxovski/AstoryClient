// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ИМЕНА МАТЕРИАЛОВ И ПАРАМЕТРОВ ПОРТА + ЭКРАННЫЕ АССЕТЫ ЗАГРУЗКИ (дедуп №2, В1-C, 11.10.2026). [ПОРТ]
//   План — `Reports/dedup_audit2/00_PLAN.md` §5 В1, находки F10/F16 — `03_fx_render.md`, отчёт — `wave1_C.md`.
//
//   ЗАЧЕМ. Пути мастеров, их имена и имена параметров жили строками в 4–13 местах каждый. Опасно молча:
//   `UAsdaVideoSettingsSubsystem::GlowAlphaOfMesh` узнаёт полупрозрачное тело ПО ИМЕНИ мастера, а
//   `SetScalarParameterValue` с неверным именем параметра ничего не сообщает — опечатка = эффект пропал без лога.
//   Здесь — одна строка на имя; значения побайтно прежние (сверка — самотест `asda.Dedup.W1CSelfTest`).
//   Одиночные мастера в реестр путей `AsdaAssetPaths` не переносятся (политика реестра `singletons_policy`: scan_only),
//   поэтому — отдельный рукописный заголовок. Пути — объектные («Пакет.Объект»), как в прежних вызовах `LoadObject`.
#pragma once

#include "CoreMinimal.h"

namespace AsdaMat
{
	// ══ Объектные пути ══
	inline constexpr const TCHAR* kPreviewFadePath = TEXT("/Game/Materials/M_AsdaPreviewFade.M_AsdaPreviewFade");   // фейд появления/ухода (translucent)
	inline constexpr const TCHAR* kCharLitSoulPath = TEXT("/Game/Materials/M_AsdaCharLitSoul.M_AsdaCharLitSoul");   // «душа»/GM-hide (translucent)
	// ★ 31.10.2026 (волна acce_alpha): варианты мастера персонажа для частей, которые оригинал рисует прозрачными
	//   (сборщик Tools/asda_build_charlit_alpha_mats.py, выбор — UAsdaCharAssembly::ApplyItemTexture).
	inline constexpr const TCHAR* kCharLitBlendPath = TEXT("/Game/Materials/M_AsdaCharLitBlend.M_AsdaCharLitBlend");   // мягкая альфа (translucent)
	inline constexpr const TCHAR* kCharLitMaskPath  = TEXT("/Game/Materials/M_AsdaCharLitMask.M_AsdaCharLitMask");     // жёсткая вырезка (masked 8/255)
	inline constexpr const TCHAR* kNormalsMpcPath  = TEXT("/Game/Materials/MPC_AsdaNormals.MPC_AsdaNormals");       // коллекция «Рельеф» (меню разработчика)
	inline constexpr const TCHAR* kLoadingBgPath   = TEXT("/Game/AsdaAssets/UI/Dev/M_LoadingBG.M_LoadingBG");       // фон загрузочного экрана (UV V 0..0.75)

	// ══ Имена мастеров (сравнение `GetName()` родителя MID) ══
	inline constexpr const TCHAR* kPreviewFadeName = TEXT("M_AsdaPreviewFade");   // мастер фейда
	inline constexpr const TCHAR* kCharLitSoulName = TEXT("M_AsdaCharLitSoul");   // мастер «души»

	// ══ Имена параметров ══
	inline constexpr const TCHAR* kParamTex           = TEXT("Tex");             // диффуз-текстура мастеров M_AsdaObject/CharLit/фейда/«души»
	inline constexpr const TCHAR* kParamFadeIn        = TEXT("FadeIn");          // альфа фейда (M_AsdaPreviewFade)
	inline constexpr const TCHAR* kParamSoulOpacity   = TEXT("SoulOpacity");     // альфа «души» (M_AsdaCharLitSoul)
	inline constexpr const TCHAR* kParamEmissiveBoost = TEXT("EmissiveBoost");   // множитель свечения мастеров эффектов

	// ══ Слоты частей экипировки ══
	// ★ 31.10.2026 (баг 8 bug_chest_invisible): хвост имени слота, в который UAsdaAlphaSplitLib (AsdaImporters) перенёс
	//   подсетки .jbm с битом очереди mr+0x004 & 1. Прозрачный мастер получает только такой слот, остальные — непрозрачные.
	inline constexpr const TCHAR* kBitSlotSuffix = TEXT("_asdabit");
}

// Экранные ассеты загрузки и стартовой заставки (`Start_logo` — одна текстура на логин и загрузчик).
namespace AsdaLoadScreen
{
	inline constexpr const TCHAR* kStartLogoTex = TEXT("/Game/AsdaAssets/Textures/Maps/load/Start_logo.Start_logo");   // заставка/фолбэк загрузки
	// ★ 12.10.2026 (владелец): фоны экрана загрузки ТЕСТА ПРОИЗВОДИТЕЛЬНОСТИ по языку сессии (импорт —
	//   Tools/asda_import_bench_load_textures.py из @@ASTORY_DEV_ROOT@@\Data\OwnerArt\pt_RU.png / pt_EN.png; формат штатных картинок загрузки:
	//   контент в верхних 0.75, крап материалом M_LoadingBG). Ставит UAsdaBenchSubsystem через SetNextLoadingTexture.
	inline constexpr const TCHAR* kBenchLoadTexRu = TEXT("/Game/AsdaAssets/Textures/Maps/load/pt_RU.pt_RU");
	inline constexpr const TCHAR* kBenchLoadTexEn = TEXT("/Game/AsdaAssets/Textures/Maps/load/pt_EN.pt_EN");
}
