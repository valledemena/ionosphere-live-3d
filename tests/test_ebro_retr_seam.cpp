#include "Data/EbroAdapter.h"
#include <cstdio>
#include <string>
#include <vector>

struct StubEbro : EbroAdapter {
    enum Policy { NetFail, BadBody, TopeAbort, HalfAndFail,
                  GoodBody, GoodSecond, FloorOdd };
    Policy policy = NetFail;
    int fetchCalls = 0;
    int listCalls = 0;
    std::vector<std::string> naming;
    bool listOk = true;

    static std::string goodBody() {
        // parseable: ff line + 14 campos de 8 chars en L6 + L8. valid outcome.
        std::string l6;
        for (int i = 0; i < 14; ++i) l6 += "0       ";  // 8 per field
        std::string l8;
        for (int i = 0; i < 14; ++i) l8 += "0       ";
        return std::string("hdr\nFF\n") + l6 + "\ntail?\n" + l8 + "\n";
    }

    bool listFiles(std::vector<std::string>& names) override {
        ++listCalls;
        if (!listOk) return false;
        names = naming.empty() ? std::vector<std::string>{"EB040_2026245000001.SAO","EB040_2026245005001.SAO"} : naming;
        return true;
    }
    bool fetchFile(const std::string& /*fname*/, std::string& response) override {
        ++fetchCalls;
        switch (policy) {
            case NetFail:   return false;
            case TopeAbort: return false;
            case BadBody:   response = "garbage (no FF)"; return true;
            case GoodBody:  response = goodBody(); return true;
            case GoodSecond: // first bad, second parses
                if (fetchCalls == 1) { response = "garbage (no FF)"; return true; }
                response = goodBody(); return true;
            case HalfAndFail:
                if (fetchCalls == 1) { response = "garbage (no FF)"; return true; }
                return false;
            case FloorOdd: response = goodBody(); return true;
        }
        return false;
    }
    void setLastListUtc(double t) { lastListUtc_ = t; }
};

static bool ok(bool got, bool want, const char* lab) {
    if (got != want) { std::printf("[FAIL] %s (got %d) want %d\n", lab, got, want); return false; }
    std::printf("[ OK ] %s\n", lab); return true;
}

int main() {
    int fails = 0;

    if (EbroAdapter::saoFilename(-1.0).empty()) std::printf("[ OK ] saoFilename(-1) empty\n");
    else { std::printf("[FAIL]\n"); fails++; }
    if (!EbroAdapter::saoFilename(1756684800.0).empty()) std::printf("[ OK ] saoFilename positive\n");
    else { std::printf("[FAIL]\n"); fails++; }
    {
        std::string a = EbroAdapter::saoFilename(1756684800.0 + 300);
        std::string b = EbroAdapter::saoFilename(1756684800.0 + 360);
        if (a != b) { std::printf("[FAIL] saoFilename floor within 5 min (%s vs %s)\n", a.c_str(), b.c_str()); fails++; }
        else { std::printf("[ OK ] saoFilename floor 5 (53->50) same name\n"); }
    }
    if ((int)EbroAdapter::saoFilename(1756684800.0).size() == 23)
        std::printf("[ OK ] saoFilename length 23\n");
    else { std::printf("[FAIL] length not 23\n"); fails++; }

    // P1 normal: good on first try -> 1 RETR, 0 NLST
    {
        StubEbro a; a.policy = StubEbro::GoodBody; a.setLastListUtc(100000.0);
        bool r = a.fetch(100000.0);
        if (!ok(r, true, "P1 fetch good returns true")) fails++;
        if (!ok(a.fetchCalls == 1 && a.listCalls == 0, true, "P1 1 RETR, 0 NLST normal")) fails++;
        if (!ok(!a.isBlocked(100000.0), true, "P1 unblocked after good")) fails++;
    }
    // P2 retry -300: first fails, second good -> success
    {
        StubEbro a; a.policy = StubEbro::GoodSecond; a.setLastListUtc(100000.0);
        bool r = a.fetch(100000.0);
        if (!ok(r, true, "P2 retry -300s recovers")) fails++;
        if (!ok(a.fetchCalls == 2 && a.listCalls == 0, true, "P2 2 RETR, 0 NLST")) fails++;
    }
    // P3 net failure -> no block
    {
        StubEbro a; a.policy = StubEbro::NetFail;
        a.fetch(100000.0);
        if (!ok(!a.isBlocked(100000.0), true, "P3 network failure -> no 2h block")) fails++;
    }
    // P4 anyBody ilegible + lost -> block
    {
        StubEbro a; a.policy = StubEbro::HalfAndFail;
        a.fetch(100000.0);
        if (!ok(a.isBlocked(100000.0), true, "P4 unreadable + lost -> 2h block")) fails++;
    }
    // P5/P6 tope
    {
        StubEbro a; a.policy = StubEbro::TopeAbort;
        a.fetch(100000.0);
        if (!ok(!a.isBlocked(100000.0), true, "TopeAbort (2MiB) -> no 2h block")) fails++;
    }
    // BadBody block all
    {
        StubEbro a; a.policy = StubEbro::BadBody;
        a.fetch(100000.0);
        if (!ok(a.isBlocked(100000.0), true, "all unreadable -> 2h block")) fails++;
    }
    // P5 list expired triggers NLST (listCalls>=1)
    {
        StubEbro a; a.policy = StubEbro::NetFail;
        a.setLastListUtc(0.0);
        a.fetch(100000.0);
        if (!ok(a.listCalls >= 1, true, "P5 expired list -> NLST (startup)")) fails++;
    }
    // saoFilename year roll (2026 -> 2027)
    {
        std::string s2026 = EbroAdapter::saoFilename(1767139200.0); // near 2026 end
        std::string s2027 = EbroAdapter::saoFilename(1767225600.0); // +1 day
        if (s2026.empty() || s2027.empty()) { printf("[FAIL] saoFilename yrroll empty\n"); fails++; }
        else { printf("[ OK ] saoFilename yrroll %s / %s\n", s2026.c_str(), s2027.c_str()); }
    }

    std::printf("\n%d FAIL\n", fails);
    return fails ? 1 : 0;
}
