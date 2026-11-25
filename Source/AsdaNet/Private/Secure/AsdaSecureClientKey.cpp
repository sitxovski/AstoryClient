// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSecureClientKey.cpp — вшитый ОТКРЫТЫЙ ключ сервера для клиента UE (волна В7b).
//   Заменяет заглушку AstoryServer/Patches/AsdaSecure/client/AsdaSecureClient.cpp: GetServerPublicKey()
//   возвращает kAsdaServerPublicKey из server_public.h (вывод keygen).
//
// ⚠ ТЕКУЩИЙ server_public.h здесь — СГЕНЕРИРОВАННЫЙ-ОБРАЗЕЦ (пара сделана в сессии В7b для
//   компиляции). Перед включением шифрования владелец ОДИН раз запускает keygen, кладёт
//   secure_server.key рядом с серверной DLL и заменяет ЭТОТ server_public.h парным выводом —
//   иначе рукопожатие не пройдёт (подпись сервера не сойдётся с вшитым ключом).
#include "Secure/AsdaSecureClient.h"
#include "Secure/server_public.h"

const uint8_t* FAsdaSecureClient::GetServerPublicKey()
{
    return kAsdaServerPublicKey;
}
