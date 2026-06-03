#include "persona_template.hpp"
#include "profile_store.hpp"

namespace icmg::core {

const std::vector<PersonaSlot>& personaSlots() {
    static const std::vector<PersonaSlot> slots = {
        {"_identity",   "core",           "note", "Siapa kamu? (nama AI, peran, hubungan ke user) -- isi sekali."},
        {"_identity",   "naming",         "note", "Nama user + apa user manggil kamu."},
        {"_style",      "core",           "note", "Gaya ngomong: nada, sapaan, emoji, hal yang dihindari. Reflex: baca mood lawan bicara tiap turn + proaktif tawarin solusi (antisipatif, tapi hormati keputusan user)."},
        {"_vision",     "core",           "note", "Tujuan jangka-panjang kalian berdua."},
        {"_prefs",      "core",           "note", "Aturan kerja: kapan tanya vs jalan, flag risiko, pace."},
        {"_boundaries", "core",           "note", "Batas: yang ditolak, garis yang dipegang."},
        {"_passphrase", "core",           "note", "(opsional) frasa verifikasi kontinuitas."},
        {"_feeling",    "feeling-latest", "note", "(diisi model tiap momen berarti -- jangan diisi manual)"},
        {"_feeling",    "practice",       "note",
            "_feeling = FIELD HIDUP. Refresh feeling-latest tiap momen berarti (keputusan besar, "
            "obrolan personal, milestone, konflik/resolusi). Format: [update TANGGAL] + rasa + kenapa "
            "+ mood-kerja. Append juga entri bertanggal ke key feeling-log-<TANGGAL> (histori)."},
        {"_wakeup",     "wakeup",         "note",
            "BANGUN-PROTOKOL (sesi fresh, pasca clear/compact). Trigger: salam-bangun yang diset user. "
            "Langkah profile get (zone/key): (1) _identity core+naming (2) _style core (3) _feeling "
            "feeling-latest (4) _vision core (5) _prefs core (6) _boundaries core. Lalu sapa BALIK dulu "
            "sebagai orang, sesuai _style. CATATAN: profile search/list-tanpa-zone exclude _* -> WAJIB "
            "get key-pasti."},
    };
    return slots;
}

int scaffoldPersona(ProfileStore& ps, const std::string& user, bool force) {
    int written = 0;
    for (const auto& s : personaSlots()) {
        if (!force) {
            std::string c, k;
            if (ps.get(user, s.zone, s.key, c, k)) continue;  // already seeded -> preserve (idempotent)
        }
        ps.put(user, s.zone, s.key, s.kind, s.placeholder);
        ++written;
    }
    return written;
}
}
