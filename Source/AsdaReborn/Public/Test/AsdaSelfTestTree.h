// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТЫ: ПАПКА ПРОЕКТА В ДЕРЕВЕ (11.11.2026, D4 — разбор красных самотестов эталона 7.6.0). [ПОРТ]
//
//   ЗАЧЕМ. Часть самотестов сверяет игру с ведомостями-оракулами, которые лежат в дереве проекта и в сборку не входят:
//   `Tools/data/*.json`, `RawAssets/…`, `tests/…`, `../Releases/keys`, `../AstoryServer/…`. Пути строились от `FPaths::ProjectDir()`.
//   В редакторе (`-game` на дереве) это и есть дерево, а в СОБРАННОЙ игре `ProjectDir()` = `<сборка>/Windows/AsdaReborn/` —
//   файлов там нет, и самотест красный не из-за игры (эталон: QuestFollow, QuestDialogReward, UpdateCheck, PremiumCardQty).
//
//   КАК. Одна общая точка: если задана переменная окружения `ASDA_ROOT` (корень раскладки; та же переменная, что у
//   `Tools/asda_paths.py` и у `asda.Fish.ZoneSelfTest`) и в ней есть `AstoryClient/` — берётся она; иначе, как раньше,
//   `FPaths::ProjectDir()`. Переменную ставит прогон регресс-набора (`Tools/asda_regress_pack.py`); без неё поведение
//   прежнее. Только для ЧТЕНИЯ ведомостей: кадры и отчёты самотесты пишут по-старому.
#pragma once
#include "CoreMinimal.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"

namespace AsdaSelfTestTree
{
	// Папка проекта в дереве (с косой чертой на конце): `<ASDA_ROOT>/AstoryClient/`, если она есть, иначе `FPaths::ProjectDir()`.
	inline FString ProjectDir()
	{
		const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_ROOT"));
		if (!Root.IsEmpty())
		{
			const FString Dir = Root / TEXT("AstoryClient/");
			if (FPaths::DirectoryExists(Dir)) { return Dir; }
		}
		return FPaths::ProjectDir();
	}
}
