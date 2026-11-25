// Copyright (c) 2026 @sitxovski (Discord)
// State.cpp — состояние клиента (см. State.h).
#include "State.h"

#include <windows.h>

#undef GetObject  // макрос windows.h (GetObjectW) ломает метод RapidJSON GetObject
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "Crypto.h"
#include "FileSys.h"
#include "Format.h"

namespace asda {

#if defined(ASDA_LAUNCHER_TEST_BUILD)
static const wchar_t* kRegBase = L"Software\\AStoryRemakeTest\\Updater";   // тесты не трогают ключ игроков
#else
static const wchar_t* kRegBase = L"Software\\AStoryRemake\\Updater";
#endif

State::State(const std::wstring& root, const std::function<void(const std::string&)>& log) {
    root_ = root;
    dir = root + L"\\" + Widen(kStateDirName);
    objects = dir + L"\\objects";
    CreateDirectoryW(dir.c_str(), nullptr);
    CreateDirectoryW(objects.c_str(), nullptr);
    std::string low = LowerAscii(Narrow(root));
    regKey_ = std::wstring(kRegBase) + L"\\" + Widen(Sha256Hex(low.data(), low.size()).substr(0, 16));
    Load(log);
    LoadRegistry();
}

// Разобрать JSON-файл (или его .bak) в документ-объект
static bool LoadJson(const std::wstring& path, rapidjson::Document& d) {
    for (const std::wstring& p : {path, path + L".bak"}) {
        Bytes b;
        if (!ReadAll(p, b, 256u << 20)) continue;
        d.Parse<rapidjson::kParseValidateEncodingFlag>((const char*)b.data(), b.size());
        if (!d.HasParseError() && d.IsObject()) return true;
    }
    return false;
}

void State::Load(const std::function<void(const std::string&)>& log) {
    rapidjson::Document d;
    std::wstring sp = dir + L"\\state.json";
    if (LoadJson(sp, d)) {
        channel = JStr(d, "channel");
        version = JStr(d, "version");
        manifest = JStr(d, "manifest");
        minVersion = JStr(d, "min_version");
        JInt(d, "seq", seq);
        JInt(d, "keyring_seq", keyringSeq);
        auto sm = d.FindMember("seq_max");
        if (sm != d.MemberEnd() && sm->value.IsObject())
            for (auto& m : sm->value.GetObject())
                if (m.value.IsInt64()) seqMax[m.name.GetString()] = m.value.GetInt64();
        auto se = d.FindMember("seen_manifest");
        if (se != d.MemberEnd() && se->value.IsObject())
            for (auto& m : se->value.GetObject())
                if (m.value.IsString()) seenManifest[m.name.GetString()] = m.value.GetString();
        auto p = d.FindMember("pending");
        if (p != d.MemberEnd() && p->value.IsObject()) {
            Pending pd;
            pd.version = JStr(p->value, "version");
            pd.manifest = JStr(p->value, "manifest");
            JInt(p->value, "seq", pd.seq);
            pending = pd;
        }
    } else if (log && (GetFileAttributesW(sp.c_str()) != INVALID_FILE_ATTRIBUTES ||
                       GetFileAttributesW((sp + L".bak").c_str()) != INVALID_FILE_ATTRIBUTES)) {
        log("! состояние клиента повреждено — начинаю с чистого (минимальные seq из лаунчера остаются)");
    }
    rapidjson::Document c;
    if (LoadJson(dir + L"\\hashcache.json", c)) {
        for (auto& m : c.GetObject()) {
            const auto& v = m.value;
            if (v.IsArray() && v.Size() == 3 && v[0].IsUint64() && v[1].IsUint64() && v[2].IsString())
                cache[m.name.GetString()] = {v[0].GetUint64(), v[1].GetUint64(), v[2].GetString()};
        }
    }
}

void State::LoadRegistry() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, regKey_.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    wchar_t name[256];
    for (DWORD i = 0;; ++i) {
        DWORD nlen = 256, type = 0, sz = sizeof(ULONGLONG);
        ULONGLONG v = 0;
        if (RegEnumValueW(k, i, name, &nlen, nullptr, &type, (LPBYTE)&v, &sz) != ERROR_SUCCESS) break;
        if (type != REG_QWORD) continue;
        std::string n = Narrow(name);
        if (n == "keyring_seq") keyringSeq = std::max<int64_t>(keyringSeq, (int64_t)v);
        else if (n.rfind("seq_max_", 0) == 0) {
            int64_t& cur = seqMax[n.substr(8)];
            cur = std::max<int64_t>(cur, (int64_t)v);
        }
    }
    RegCloseKey(k);
}

void State::SaveRegistry() const {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, regKey_.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return;
    ULONGLONG v = (ULONGLONG)keyringSeq;
    RegSetValueExW(k, L"keyring_seq", 0, REG_QWORD, (const BYTE*)&v, sizeof(v));
    for (const auto& [ch, s] : seqMax) {
        v = (ULONGLONG)s;
        RegSetValueExW(k, Widen("seq_max_" + ch).c_str(), 0, REG_QWORD, (const BYTE*)&v, sizeof(v));
    }
    RegCloseKey(k);
}

// Записать JSON-документ атомарно с .bak
static void SaveJsonFile(const std::wstring& root, const std::wstring& path, const rapidjson::StringBuffer& sb) {
    if (IsRegularFile(path)) ReplaceFile(root, path, path + L".bak");
    WriteAtomic(root, path, sb.GetString(), sb.GetSize());
}

void State::Save() {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    w.StartObject();
    auto kv = [&](const char* k, const std::string& v) {
        w.Key(k);
        w.String(v.c_str(), (rapidjson::SizeType)v.size());
    };
    if (!channel.empty()) kv("channel", channel);
    if (!version.empty()) kv("version", version);
    if (!manifest.empty()) kv("manifest", manifest);
    if (!minVersion.empty()) kv("min_version", minVersion);
    w.Key("seq");
    w.Int64(seq);
    w.Key("keyring_seq");
    w.Int64(keyringSeq);
    w.Key("seq_max");
    w.StartObject();
    for (const auto& [ch, s] : seqMax) {
        w.Key(ch.c_str());
        w.Int64(s);
    }
    w.EndObject();
    w.Key("seen_manifest");
    w.StartObject();
    for (const auto& [ch, s] : seenManifest) kv(ch.c_str(), s);
    w.EndObject();
    if (pending) {
        w.Key("pending");
        w.StartObject();
        kv("version", pending->version);
        kv("manifest", pending->manifest);
        w.Key("seq");
        w.Int64(pending->seq);
        w.EndObject();
    }
    w.EndObject();
    SaveJsonFile(root_, dir + L"\\state.json", sb);
    rapidjson::StringBuffer cb;
    rapidjson::Writer<rapidjson::StringBuffer> cw(cb);
    cw.StartObject();
    for (const auto& [path, e] : cache) {
        cw.Key(path.c_str());
        cw.StartArray();
        cw.Uint64(e.size);
        cw.Uint64(e.mtime);
        cw.String(e.sha.c_str());
        cw.EndArray();
    }
    cw.EndObject();
    SaveJsonFile(root_, dir + L"\\hashcache.json", cb);
    SaveRegistry();
}

bool State::Plst(const std::string& sha, Bytes& out) const {
    return ReadAll(objects + L"\\" + Widen(sha) + L".plst", out, 64u << 20) && Sha256Hex(out.data(), out.size()) == sha;
}

bool State::ManifestBlob(Bytes& out) const {
    return !manifest.empty() && ReadAll(dir + L"\\manifest.json.zst", out, 64u << 20) &&
           Sha256Hex(out.data(), out.size()) == manifest;
}

bool IsReady(const std::wstring& gameDir) {
    State s(RootPath(gameDir), nullptr);
    return !s.version.empty() && !s.pending;
}

}  // namespace asda
