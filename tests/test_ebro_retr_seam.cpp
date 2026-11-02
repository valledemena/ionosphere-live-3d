#include "Data/EbroAdapter.h"
#include <cstdio>
#include <string>
#include <vector>
#include <cmath>

struct StubEbro : EbroAdapter {
    enum Policy { NetFail, BadBody, TopeAbort, HalfAndFail };
    Policy policy = NetFail;
    int fetchCalls = 0;
    int listCalls = 0;
    std::vector<std::string> naming;
    bool listOk = true;
    bool listFiles(std::vector<std::string>& names) override {
        ++listCalls;
        if (!listOk) return false;
        names = listing.empty() ? std::vector<std::string>{"EB040_2026245000001.SAO","EB040_2026245005001.SAO"} : listing;
        return true;
    }
    bool fetchFile(const std::string& /*f*/, std::string& body) override {
        ++fetchCalls;
        switch (policy) {
            case NetFail:    return false;               // RETR caido por red
            case TopeAbort:  return false;               // aborto por tope => sin cuerpo
            case BadBody:    body = "garbage (no FF)"; return true;
            case HalfAndFail: {
                if (fetchCalls == 1) { body = "garbage"; return true; }
                return false;
            }
        }
        return false;
    }
    void setLastListUtc(double t) { lastListUtc_ = t; }
    std::vector<std::string> listing;
};

static bool ok(bool got, bool want, const char* lab, int* fails) {
    if (got != want) { printf("[FAIL] %s (got %d) want %d\n", lab, got, want); (*fails)++; return false; }
    printf("[ OK ] %s\n", lab); return true;
}

int main() {
    int fails = 0;

    // S1.1 floor to nearest 5 min
    eqd: {
        std::string a = EbroAdapter::saoFilename(1756684800.0 + 300); // 53 -> 55? actually floor
        // just check length + nonempty + negative empty
    }
    if (EbroAdapter::saoFilename(-1.0).empty()) { printf("[ OK ] saoFilename(-1)\n"); }
    else { printf("[FAIL]\n"); fails++; }
    if (!EbroAdapter::saoFilename(1756684800.0).empty()) { printf("[ OK ] saoFilename positive\n"); }
    else { printf("[FAIL]\n"); fails++; }
    // round-trip-ish
    std::string name = EbroAdapter::saoFilename(1756684800.0);
    if ((int)name.size() == 23) { printf("[ OK ] saoFilename length 23\n"); }
    else { printf("[FAIL] len=%zu\n", name.size()); fails++; }

    // S2.1 Normal exit: list at startup, 2 RETRs, first good
    {
        StubEbro a; a.policy = StubEbro::BadBody;
        // provide valid? fetch will fail parse SAO, list occurs; RETR 2 times
        a.fetch(100000.0);
        if (!ok(a.listCalls >= 1, true, "P1 P1 netfail retry uses list", &fails)) {}
    }

    // P: NetFail case (no body, no block)
    {
        StubEbro a; a.policy = StubEbro::NetFail;
        a.fetch(100000.0);
        ok(!a.isBlocked(100000.0), true, "NetFail -> no block", &fails);
        ok(a.listCalls >= 1, true, "NetFail calls list (startup)", &fails);
    }

    // BadBody: 2h block
    {
        StubEbro a; a.policy = StubEbro::BadBody;
        a.fetch(100000.0);
        ok(a.isBlocked(100000.0), true, "BadBody -> block", &fails);
    }

    // Tope abort -> no block even with list
    {
        StubEbro a; a.policy = StubEbro::TopeAbort;
        a.fetch(100000.0);
        ok(!a.isBlocked(100000.0), true, "TopeAbort -> no block", &fails);
    }

    // HalfAndFail: ilegible first + lost second -> block
    {
        StubEbro a; a.policy = StubEbro::HalfAndFail;
        a.fetch(100000.0);
        ok(a.isBlocked(100000.0), true, "HalfAndFail -> block", &fails);
    }

    // P1: normal = zero list - we emulate by marking list already fresh
    {
        StubEbro a; a.policy = StubEbro::NetFail;
        a.setLastListUtc(100000.0); // list already recent
        a.fetch(100000.0);
        // treated as network drop: should not block
        ok(!a.isBlocked(100000.0), true, "recent list + net failure -> no block", &fails);
    }

    // P5/P6 gate control based on expired list (older than 1h)
    {
        StubEbro a; a.policy = StubEbro::NetFail;
        a.setLastListUtc(0.0);
        a.fetch(100000.0);
        ok(a.listCalls == 1, true, "P5/P6: expired list triggered NLST", &fails);
    }

    printf("\n%d FAIL\n", fails);
    return fails ? 1 : 0;
}
