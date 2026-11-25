// Copyright (c) 2026 @sitxovski (Discord)
// server_public.h - long-term PUBLIC key of YOUR game server (ECDSA P-256, X||Y). The client checks the ServerHello signature with it.
// Replace this whole file with the one made for your server:
//   AstoryServer\Prerequisites\setup_server.ps1 prints <ASTORY_SERVER_ROOT>\keys\server_public.h
//   (or: python AstoryServer\Prerequisites\gen_secure_key.py secure_server.key server_public.h).
// The private key (secure_server.key) stays only on the server, never in the sources.
#pragma once
#include <cstdint>
#error "server_public.h: put the public key of your server here (AstoryServer\Prerequisites\setup_server.ps1 prints keys\server_public.h)"
static const uint8_t kAsdaServerPublicKey[64] = { 0 };
