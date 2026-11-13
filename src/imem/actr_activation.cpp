// 2026-11-11: see actr_activation.hpp.
#include "actr_activation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>

namespace icmg::imem {

double actrBaseLevel(int n, double ageHours, double lifetimeHours) {
    const double d = kActrDecay;
    const double t = std::max(ageHours, kActrMinAgeHours);
    const double L = std::max(lifetimeHours, t);
    double sum = std::pow(t, -d);
    if (n > 1 && L > t)
        sum += (double)(n - 1) * (std::pow(L, 1.0 - d) - std::pow(t, 1.0 - d)) /
               ((1.0 - d) * (L - t));
    else if (n > 1)
        sum += (double)(n - 1) * std::pow(t, -d);   // all uses at the same moment
    return std::log(sum);
}

double actrRecencyMult(int n, double ageHours, double lifetimeHours) {
    return 1.0 / (1.0 + std::exp(-actrBaseLevel(n, ageHours, lifetimeHours)));
}

double actrRecencyMultAt(int n, int64_t lastUsed, int64_t createdAt, int64_t now) {
    if (lastUsed <= 0) lastUsed = createdAt;
    if (createdAt <= 0) createdAt = lastUsed;
    if (lastUsed <= 0) return kActrUnknownMult;
    const double age  = (double)(now - lastUsed) / 3600.0;
    const double life = (double)(now - std::min(createdAt, lastUsed)) / 3600.0;
    return actrRecencyMult(n, age, life);
}

bool actrRecencyFromEnv() {
    const char* e = std::getenv(kActrRecencyEnv);
    return e && std::string(e) == kActrRecencyOn;
}

int64_t actrNowSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace icmg::imem
