#pragma once
// AirLabs replays (format "ALR1") and the small text helpers the leaderboard needs.
//
// A replay is a whole preset run, one sample per physics tick (120 per second): the car's
// rotation as the game reported it that tick, and the direction from the car to the ball that
// the tick was scored against. Events mark what happened (a target appearing, a hit and its
// points, a miss, ...). That is everything the run's score depends on, so the leaderboard
// server can score the run again from the samples alone, and the plugin can play it back.
//
// Layout (little-endian):
//   header   64 bytes   see WriteHeader below
//   samples  14 bytes each: u16 ms since the run started, u16 pitch, u16 yaw, u16 roll
//                           (Unreal rotation units, 65536 per turn), i16 x3 ball direction (x32767)
//   events   12 bytes each: u32 tick (= the sample it happened on), u8 type, 3 bytes zero, f32 value
//
// This header has no dependencies beyond the standard library, so the unit tests and the
// tools can use it too.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace airlabs
{
    constexpr std::uint16_t kReplayFormat = 1;
    constexpr std::size_t kReplayHeaderBytes = 64;
    constexpr std::size_t kReplaySampleBytes = 14;
    constexpr std::size_t kReplayEventBytes = 12;
    constexpr std::uint32_t kReplayMaxSamples = 120 * 75 * 2; // a 60 s run is 7200 (more if the game calls the hook more often)

    enum ReplayEvent : std::uint8_t
    {
        kEvSpawn = 1,        // a Flick / Hybrid target appeared; value = the turn to it (deg)
        kEvHit = 2,          // value = points
        kEvMiss = 3,         // value = points lost (500, less if the score was lower)
        kEvHybridStart = 4,  // the ball took off after a Hybrid flick
        kEvHybridEnd = 5,    // value = % of the tracking spent on the ball
        kEvChaseLaunch = 6,  // the Chase ball dashed off (the first one, at tick 0, is the start of the run)
        kEvChaseStop = 7,    // the Chase ball stopped
        kEvChaseCatch = 8,   // value = seconds the ball sat still before you caught it
        kEvChaseMiss = 9,    // not caught in time: it dashes off anyway
    };
    enum ReplayFlag : std::uint8_t
    {
        kRfNoRoll = 1,      // No air roll
        kRfKeyboard = 2,    // looked like keyboard input
        kRfTickTiming = 4,  // every tick was exactly 1/120 s (the normal case)
    };

    struct ReplaySample
    {
        std::uint16_t ms = 0;
        std::int16_t pitch = 0, yaw = 0, roll = 0;
        std::int16_t dx = 0, dy = 0, dz = 0;
    };
    struct ReplayEventRec
    {
        std::uint32_t tick = 0;
        std::uint8_t type = 0;
        float value = 0.f;
    };
    struct Replay
    {
        std::uint8_t mode = 0, preset = 0, range = 0, aim = 0, flags = 0;
        float distance = 0.f, targetDeg = 0.f, runSec = 0.f, tickRate = 0.f;
        std::int32_t score = 0;
        std::uint16_t targets = 0, misses = 0, hybReps = 0;
        std::string version; // plugin version, up to 8 characters
        std::vector<ReplaySample> samples;
        std::vector<ReplayEventRec> events;
    };

    inline std::int16_t PackUnit(float v) { return static_cast<std::int16_t>(std::lround(std::clamp(v, -1.f, 1.f) * 32767.f)); }
    inline float UnpackUnit(std::int16_t v) { return static_cast<float>(v) / 32767.f; }

    namespace replay_detail
    {
        inline void Put8(std::string& o, std::uint8_t v) { o.push_back(static_cast<char>(v)); }
        inline void Put16(std::string& o, std::uint16_t v) { Put8(o, v & 0xFF); Put8(o, v >> 8); }
        inline void Put32(std::string& o, std::uint32_t v) { Put16(o, v & 0xFFFF); Put16(o, v >> 16); }
        inline void PutF(std::string& o, float f) { std::uint32_t u; std::memcpy(&u, &f, 4); Put32(o, u); }
        inline std::uint8_t Get8(const std::string& s, std::size_t i) { return static_cast<std::uint8_t>(s[i]); }
        inline std::uint16_t Get16(const std::string& s, std::size_t i) { return static_cast<std::uint16_t>(Get8(s, i) | (Get8(s, i + 1) << 8)); }
        inline std::uint32_t Get32(const std::string& s, std::size_t i) { return Get16(s, i) | (static_cast<std::uint32_t>(Get16(s, i + 2)) << 16); }
        inline float GetF(const std::string& s, std::size_t i) { std::uint32_t u = Get32(s, i); float f; std::memcpy(&f, &u, 4); return f; }
    }

    inline std::string SerializeReplay(const Replay& r)
    {
        using namespace replay_detail;
        std::string o;
        o.reserve(kReplayHeaderBytes + r.samples.size() * kReplaySampleBytes + r.events.size() * kReplayEventBytes);
        o += "ALR1";
        Put16(o, kReplayFormat);
        Put16(o, static_cast<std::uint16_t>(kReplayHeaderBytes));
        Put8(o, r.mode); Put8(o, r.preset); Put8(o, r.range); Put8(o, r.aim);
        Put8(o, r.flags); Put8(o, 0); Put8(o, 0); Put8(o, 0);
        Put32(o, static_cast<std::uint32_t>(r.samples.size()));
        Put32(o, static_cast<std::uint32_t>(r.events.size()));
        PutF(o, r.distance); PutF(o, r.targetDeg); PutF(o, r.runSec); PutF(o, r.tickRate);
        Put32(o, static_cast<std::uint32_t>(r.score));
        Put16(o, r.targets); Put16(o, r.misses); Put16(o, r.hybReps); Put16(o, 0);
        char ver[8] = {};
        std::memcpy(ver, r.version.data(), std::min<std::size_t>(r.version.size(), 8));
        o.append(ver, 8);
        Put32(o, 0); // 64 bytes
        for (const ReplaySample& s : r.samples)
        {
            Put16(o, s.ms);
            Put16(o, static_cast<std::uint16_t>(s.pitch)); Put16(o, static_cast<std::uint16_t>(s.yaw)); Put16(o, static_cast<std::uint16_t>(s.roll));
            Put16(o, static_cast<std::uint16_t>(s.dx)); Put16(o, static_cast<std::uint16_t>(s.dy)); Put16(o, static_cast<std::uint16_t>(s.dz));
        }
        for (const ReplayEventRec& e : r.events)
        {
            Put32(o, e.tick); Put8(o, e.type); Put8(o, 0); Put16(o, 0); PutF(o, e.value);
        }
        return o;
    }

    inline bool ParseReplay(const std::string& b, Replay& r, std::string* err = nullptr)
    {
        using namespace replay_detail;
        auto fail = [&](const char* m) { if (err) *err = m; return false; };
        if (b.size() < kReplayHeaderBytes || b.compare(0, 4, "ALR1") != 0) return fail("not an AirLabs replay");
        if (Get16(b, 4) != kReplayFormat) return fail("replay format not supported");
        const std::size_t hb = Get16(b, 6);
        if (hb < kReplayHeaderBytes || hb > b.size()) return fail("bad replay header");
        r = Replay{};
        r.mode = Get8(b, 8); r.preset = Get8(b, 9); r.range = Get8(b, 10); r.aim = Get8(b, 11); r.flags = Get8(b, 12);
        const std::uint32_t ns = Get32(b, 16), ne = Get32(b, 20);
        if (ns > kReplayMaxSamples || ne > ns * 4 + 16) return fail("replay too long");
        if (b.size() != hb + ns * kReplaySampleBytes + ne * kReplayEventBytes) return fail("replay size doesn't match");
        r.distance = GetF(b, 24); r.targetDeg = GetF(b, 28); r.runSec = GetF(b, 32); r.tickRate = GetF(b, 36);
        r.score = static_cast<std::int32_t>(Get32(b, 40));
        r.targets = Get16(b, 44); r.misses = Get16(b, 46); r.hybReps = Get16(b, 48);
        r.version.assign(b.data() + 52, 8);
        r.version.erase(std::find(r.version.begin(), r.version.end(), '\0'), r.version.end());
        r.samples.resize(ns);
        std::size_t i = hb;
        for (ReplaySample& s : r.samples)
        {
            s.ms = Get16(b, i);
            s.pitch = static_cast<std::int16_t>(Get16(b, i + 2)); s.yaw = static_cast<std::int16_t>(Get16(b, i + 4)); s.roll = static_cast<std::int16_t>(Get16(b, i + 6));
            s.dx = static_cast<std::int16_t>(Get16(b, i + 8)); s.dy = static_cast<std::int16_t>(Get16(b, i + 10)); s.dz = static_cast<std::int16_t>(Get16(b, i + 12));
            i += kReplaySampleBytes;
        }
        r.events.resize(ne);
        for (ReplayEventRec& e : r.events)
        {
            e.tick = Get32(b, i); e.type = Get8(b, i + 4); e.value = GetF(b, i + 8);
            i += kReplayEventBytes;
        }
        return true;
    }

    // ---- base64 (standard alphabet, padded) ----
    inline std::string Base64Encode(const std::string& in)
    {
        static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string o;
        o.reserve((in.size() + 2) / 3 * 4);
        std::size_t i = 0;
        for (; i + 2 < in.size(); i += 3)
        {
            std::uint32_t v = (static_cast<std::uint8_t>(in[i]) << 16) | (static_cast<std::uint8_t>(in[i + 1]) << 8) | static_cast<std::uint8_t>(in[i + 2]);
            o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
        }
        if (i < in.size())
        {
            std::uint32_t v = static_cast<std::uint8_t>(in[i]) << 16;
            if (i + 1 < in.size()) v |= static_cast<std::uint8_t>(in[i + 1]) << 8;
            o += t[v >> 18]; o += t[(v >> 12) & 63];
            o += i + 1 < in.size() ? t[(v >> 6) & 63] : '=';
            o += '=';
        }
        return o;
    }
    inline bool Base64Decode(const std::string& in, std::string& out)
    {
        auto val = [](char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+' || c == '-') return 62;
            if (c == '/' || c == '_') return 63;
            return -1;
        };
        out.clear();
        out.reserve(in.size() / 4 * 3);
        std::uint32_t acc = 0;
        int bits = 0;
        for (char c : in)
        {
            if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
            int v = val(c);
            if (v < 0) return false;
            acc = (acc << 6) | static_cast<std::uint32_t>(v);
            bits += 6;
            if (bits >= 8) { bits -= 8; out.push_back(static_cast<char>((acc >> bits) & 0xFF)); }
        }
        return true;
    }

    // ---- text helpers for the leaderboard ----

    // A JSON string literal (with the quotes).
    inline std::string JsonString(const std::string& s)
    {
        std::string o = "\"";
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); o += buf; }
                else o += static_cast<char>(c);
            }
        }
        return o + "\"";
    }

    // UTF-16 (Rocket League's player names) to UTF-8.
    inline std::string Utf8FromWide(const std::wstring& w)
    {
        std::string o;
        for (std::size_t i = 0; i < w.size(); ++i)
        {
            std::uint32_t c = static_cast<std::uint32_t>(w[i]);
            if (sizeof(wchar_t) == 2 && c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size())
            {
                std::uint32_t lo = static_cast<std::uint32_t>(w[i + 1]);
                if (lo >= 0xDC00 && lo <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); ++i; }
            }
            if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
            if (c < 0x80) o += static_cast<char>(c);
            else if (c < 0x800) { o += static_cast<char>(0xC0 | (c >> 6)); o += static_cast<char>(0x80 | (c & 63)); }
            else if (c < 0x10000) { o += static_cast<char>(0xE0 | (c >> 12)); o += static_cast<char>(0x80 | ((c >> 6) & 63)); o += static_cast<char>(0x80 | (c & 63)); }
            else { o += static_cast<char>(0xF0 | (c >> 18)); o += static_cast<char>(0x80 | ((c >> 12) & 63)); o += static_cast<char>(0x80 | ((c >> 6) & 63)); o += static_cast<char>(0x80 | (c & 63)); }
        }
        return o;
    }

    // The server's plain-text replies: one "key value" pair per line ("row" lines repeat).
    struct TextReply
    {
        std::vector<std::pair<std::string, std::string>> lines;
        std::string Get(const std::string& key, const std::string& def = "") const
        {
            for (const auto& l : lines) if (l.first == key) return l.second;
            return def;
        }
        int GetInt(const std::string& key, int def = 0) const
        {
            std::string v = Get(key);
            return v.empty() ? def : std::atoi(v.c_str());
        }
    };
    inline TextReply ParseTextReply(const std::string& body)
    {
        TextReply r;
        std::size_t i = 0;
        while (i < body.size())
        {
            std::size_t e = body.find('\n', i);
            if (e == std::string::npos) e = body.size();
            std::string line = body.substr(i, e - i);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::size_t sp = line.find(' ');
            if (!line.empty()) r.lines.emplace_back(line.substr(0, sp), sp == std::string::npos ? "" : line.substr(sp + 1));
            i = e + 1;
        }
        return r;
    }
    inline std::vector<std::string> SplitTabs(const std::string& s)
    {
        std::vector<std::string> out;
        std::size_t i = 0;
        while (true)
        {
            std::size_t e = s.find('\t', i);
            out.push_back(s.substr(i, e == std::string::npos ? std::string::npos : e - i));
            if (e == std::string::npos) break;
            i = e + 1;
        }
        return out;
    }
}
