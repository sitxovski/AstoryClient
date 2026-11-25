// Copyright (c) 2026 @sitxovski (Discord)
// FAsdaServerEntry — описание активного login-сервера (группа/адрес/порт/имя мира).
// Формат-первоисточник (login_ui_layout.md, verified байтами): оригинальный system/OPset.bin —
//   весь файл кодирован +0x64 (декод −0x64); поля-строки: group@0x004, IP@0x104, port@0x204 (atoi),
//   name@0x304. Клиент-оригинал: CServerList::Load @0x00981f10.
// ★ РАНТАЙМ ЭТОТ ФАЙЛ НЕ ЧИТАЕТ. OPset.bin разбирается ОДИН РАЗ на этапе импорта
//   (Tools/asda_build_server_list_dt.py) в ассет /Game/AsdaAssets/Data/DT_ServerList; в игре запись
//   берётся оттуда (FAsdaLoginData::LoadActiveServer). Причина: (1) прежний путь был абсолютным
//   хардкодом в каталог оригинальной игры, которого у игрока нет; (2) .bin в Content/ кукер в пак
//   не пакует. ★ 16.10.2026: IP таблицы для подключения не используется — хост всегда домен сервера
//   (AsdaReborn: Login/AsdaServerAddress.h), из таблицы берётся порт.
#pragma once
#include "CoreMinimal.h"

// Без ASDANET_API: структура header-only (все методы inline), в AsdaNet больше нет .cpp, который бы
//   инстанцировал её символы — dllimport дал бы unresolved externals у потребителей.
struct FAsdaServerEntry
{
	FString Group;
	FString IP;
	int32   Port = 0;
	FString Name;
	bool IsValid() const { return !IP.IsEmpty() && Port > 0; }
};
