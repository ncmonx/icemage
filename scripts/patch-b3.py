import sys
p = 'D:/Data Kerja/Personal/AI/icm-graph/src/cli/commands/savings_cmd.cpp'
with open(p, 'rb') as f:
    s = f.read()

# Find broken block (current bad version with literal LF) + replace
start = s.find(b'if (hasFlag(args, "--per-cmd"))')
if start < 0:
    print('OLD not found'); sys.exit(1)
end = s.find(b'\r\n        }\r\n', start) + len(b'\r\n        }\r\n')
old_block = s[start:end]

# Build correct C++ with proper \n escapes (2 chars backslash+n in source)
new_block = (
    b'if (hasFlag(args, "--per-cmd")) {\r\n'
    b'            auto& cfg2 = core::Config::instance();\r\n'
    b'            core::Db db2(cfg2.projectDbPath("."));\r\n'
    b'            int64_t cutoff2 = (int64_t)std::time(nullptr) - (int64_t)window_days * 86400;\r\n'
    b'            std::cout << "=== Per-command savings (last " << window_days << "d) ===" << "\\' + b'n";\r\n'
    b'            std::cout << "  TOOL          | CALLS | SAVED_TOK | RAW_TOK" << "\\' + b'n";\r\n'
    b'            db2.query("SELECT tool_name, COUNT(*), COALESCE(SUM(saved_tokens),0), COALESCE(SUM(raw_bytes),0)/4 FROM tool_invocations WHERE timestamp > ? GROUP BY tool_name ORDER BY SUM(saved_tokens) DESC LIMIT 30",\r\n'
    b'                {std::to_string(cutoff2)},\r\n'
    b'                [&](const core::Row& r){\r\n'
    b'                    if (r.size() < 4) return;\r\n'
    b'                    std::cout << "  " << r[0] << " | " << r[1] << " | saved=" << r[2] << " | raw=" << r[3] << "\\' + b'n";\r\n'
    b'                });\r\n'
    b'            return 0;\r\n'
    b'        }\r\n'
)
open(p,'wb').write(s[:start] + new_block + s[end:])
print('B3 rewritten clean')
