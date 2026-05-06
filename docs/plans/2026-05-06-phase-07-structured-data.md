# Phase 07: Structured Data (model/view/behavior/schema)

> **For Claude:** REQUIRED SUB-SKILL: Use `executing-plans` or `subagent-driven-development` to implement this plan.

**Goal:** Simpan dan query structured knowledge: data models, UI views, business behaviors, dan schemas — scoped per path.
**Architecture:** StructuredDataStore CRUD. Version tracking setiap update. BM25 search di name + content + tags.
**Tech Stack:** C++17, SQLite
**Assumptions:** Phase 01 + Scorer dari Phase 02 tersedia.

---

### Task 1: StructuredData struct + Store

**Files:**
- Create: `src/data/structured_data.hpp`
- Create: `src/data/data_store.hpp`
- Create: `src/data/data_store.cpp`

```cpp
struct StructuredData {
    int64_t id = 0;
    std::string data_type;  // model|view|behavior|schema
    std::string name;
    std::string scope_path;
    std::string content;
    std::string version = "1.0";
    std::string tags;
    int64_t created_at = 0;
    int64_t updated_at = 0;
};

class DataStore {
public:
    explicit DataStore(core::Db& db);
    int64_t add(const StructuredData& data);
    bool update(const std::string& name, const std::string& content, const std::string& note = "");
    bool remove(const std::string& name);
    std::optional<StructuredData> get(const std::string& name) const;
    std::vector<StructuredData> list(const std::string& type = "", const std::string& scope = "") const;
    std::vector<StructuredData> search(const std::string& query, int limit = 10) const;
};
```

---

### Task 2: CLI commands: data add + list + show + update + remove

**Files:**
- Create: `src/cli/commands/data_cmd.cpp`

```
icmg data add model User "id:int, email:string, role:enum[admin,user], created_at:datetime"
icmg data add view Dashboard "sidebar+header+main, state:loading|ready|error" --scope src/ui/
icmg data add behavior AuthFlow "login->validate->issue_token->store_session->redirect"
icmg data add schema ApiResponse "status:int, data:any, error:string|null"
icmg data add model User --file user_model.txt   # dari file

icmg data list
icmg data list --type model
icmg data list --scope src/api/
icmg data show User
icmg data show User --json
icmg data update User "id:int, email:string, role:enum[admin,user,guest]"
icmg data remove User
icmg data search "user authentication"
```

**data show output:**
```
[model] User  v1.2  (scope: src/)
Created: 2026-05-06  Updated: 2026-05-06

id:int
email:string
role:enum[admin,user]
created_at:datetime

Tags: core, identity
```

---

### Task 3: Integration dengan graph context

Saat `icmg graph context <file>`, jika ada structured_data dengan scope_path yang match file → tampilkan di info panel.

**Verify:**
```bash
./build/icmg data add model User "id:int, email:string" --scope src/
./build/icmg graph context src/api/user_handler.cpp
```
Expected: "Related models: User" muncul di output graph context.

---

### Task 4: Final verify + commit

**Step 1:**
```bash
./build/icmg data add model Invoice "id:int, bkm_id:int, amount:decimal, status:enum[draft,paid,void]"
./build/icmg data add behavior PaymentFlow "create_invoice->validate->process_payment->update_status"
./build/icmg data add schema InvoiceResponse "invoice:Invoice, payment_url:string"
./build/icmg data list
./build/icmg data list --type model
./build/icmg data show Invoice
./build/icmg data search "invoice payment"
```

**Step 2: Commit**
```bash
git add src/data/ src/cli/commands/data_cmd.cpp
git commit -m "feat: phase-07 structured data store (model/view/behavior/schema)"
```

---

## Amendments from Architecture Review

### HIGH Fix

**A1 — data_versions table (seperti sp_versions)**
Tambahkan ke schema:
```sql
CREATE TABLE data_versions (
    id          INTEGER PRIMARY KEY,
    data_id     INTEGER REFERENCES structured_data(id) ON DELETE CASCADE,
    version     TEXT NOT NULL,
    content     TEXT NOT NULL,
    change_note TEXT,
    created_at  INTEGER
);
```
`DataStore::update()` wajib insert ke `data_versions` sebelum update main record.
```
icmg data history User          # lihat semua versi
icmg data revert User --to 1.0  # rollback ke versi lama
```

**A2 — scope_path validation**
Saat `icmg data add ... --scope <path>`:
- Warn (bukan error) jika path tidak ada di filesystem
- Tampilkan: "Warning: scope path 'src/nonexistent/' does not exist. Data will still be stored."
