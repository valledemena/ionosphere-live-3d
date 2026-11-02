#include "Data/EbroAdapter.h"
#include <cstdio>
#include <string>
#include <vector>

// Subclase con transporte inyectado por seam. Sin red ni curl.
struct StubEbro : EbroAdapter {
    enum Policy { NetFail, BadBody, TopeAbort, HalfAndFail };
    Policy policy = NetFail;
    int fetchCount = 0;
    bool listOk = true;
    std::vector<std::string> listing;
    bool listFiles(std::vector<std::string>& names) override {
        if (!listOk) { return false; }
        if (listing.empty()) {
            names.push_back("EB040_2026245000001.SAO");
            names.push_back("EB040_2026245005001.SAO");
        } else {
            names = listing;
        }
        return true;
    }
    bool fetchFile(const std::string& /*fname*/, std::string& response) override {
        ++fetchCount;
        switch (policy) {
            case NetFail:    return false;                      // RETR caido por red
            case TopeAbort:  return false;                      // simula aborto por tope 2 MiB (fetch failed)
            case BadBody:    response = "no FF markers\n"; return true; // cuerpo ilegible
            case HalfAndFail:            // 1 ilegible + 1 caido
                if (fetchCount == 1) { response = "unreadable half-written"; return true; }
                return false;
        }
        return false;
    }
};

static bool eq(bool got, bool want, const char* label) {
    if (got != want) {
        std::printf("[FAIL] %s (got=%d want=%d)\n", label, got, want);
        return false;
    }
    std::printf("[ OK ] %s\n", label);
    return true;
}

int main() {
    int fails = 0;

    // Caso 1: red caida -> ningun cuerpo llega -> sin bloqueo 2h.
    {
        StubEbro a; a.policy = StubEbro::NetFail;
        a.fetch(100000.0);
        if (!eq(!a.isBlocked(100000.0), true, "net failure leaves Ebro unblocked")) fails++;
        // la lista falla y no se llama a fetchFile: sino no hay candidatos
    }

    // Caso 2: cuerpo ilegible en todos -> bloqueo 2h (noteUnreadable invocado).
    {
        StubEbro a; a.policy = StubEbro::BadBody;
        a.fetch(100000.0);
        if (!eq(a.isBlocked(100000.0), true, "unreadable body blocks 2h")) fails++;
        if (a.getErrorMessage().find("ilegible") == std::string::npos)
            { std::printf("[FAIL] error_ no legible\n"); fails++; }
        else { std::printf("[ OK ] error_ legible\n"); }
    }

    // Caso 3: aborto por tope (senyalado por fetchFile como fallo) -> sin bloqueo.
    {
        StubEbro a; a.policy = StubEbro::TopeAbort;
        a.fetch(100000.0);
        if (!eq(!a.isBlocked(100000.0), true, "2MiB tope abort -> no 2h block")) fails++;
    }

    // Caso 4: un leer-ilegible y otro RETR caido -> anyBody queda true -> bloqueo
    // (solo se bloquea si algun cuerpo llego verdaderamente).
    {
        StubEbro a; a.policy = StubEbro::HalfAndFail;
        // list ok -> probar primer fetch (ilegible body), segundo RETR cae
        a.fetch(100000.0);
        if (!eq(a.isBlocked(100000.0), true, "unreadable + lost -> 2h block")) fails++;
    }

    std::printf("\n%d FAIL\n", fails);
    return fails ? 1 : 0;
}
