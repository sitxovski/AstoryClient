// Copyright (c) 2026 @sitxovski (Discord)
#include "Login/AsdaLoginConfig.h"

UAsdaLoginConfig::UAsdaLoginConfig()
{
	// Эталонные дефолты (login_charselect.c charSelect_PlaySlotAnim@0xF9F090 + SFT.BIN id 45..54).
	// Все переопределяемы в редакторе; здесь — стартовые значения свежего DataAsset.

	// Открытие/idle (case0): f0→creation01+03, f19→04, f33→05, f55→02
	OpenSoundCues = {
		{ 0,  1 }, { 0,  3 },
		{ 19, 4 },
		{ 33, 5 },
		{ 55, 2 },
	};
	// Поворот L/R (case2/3): f0→creation09, f10→creation08, f51→creation10
	TurnSoundCues = {
		{ 0,  9 },
		{ 10, 8 },
		{ 51, 10 },
	};

	// ★ НАША строка помощника для НАШЕЙ ветки «поле адреса мира пустое» (обоснование — в заголовке).
	//   Стиль взят с эталонных записей 10 и 11 того же помощника («<FB>Необходимо ввести ID</F>.» /
	//   «<FB>Необходимо ввести пароль</F>.»): тот же оборот, тот же тег, та же точка, тот же
	//   god-robot mode 2 (warning). Своего текста не сочиняем сверх необходимого.
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): сам текст переехал в DT_GameText (id 990400, сырой с тегами —
	//   язык сессии); поле осталось ПЕРЕОПРЕДЕЛЕНИЕМ дизайнера в редакторе: пусто = строка 990400.
	ServerAddrEmptyText = FString();
	ServerAddrEmptyRobotMode = 2;
}
