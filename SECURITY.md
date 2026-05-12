# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| Latest release | ✅ |
| Older releases | ❌ |

## Reporting a Vulnerability

**Do not report security vulnerabilities via public GitHub issues.**

Email: ncmonx@hotmail.com

Include:
- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix (optional)

Response time: within 7 days.

## Security Measures

- SHA256 sidecar verification on every `update --apply` (mismatch = auto-rollback)
- URL sanitization: rejects shell metacharacters, http/https schemes only
- Directory traversal hardening on all file operations
- Apache 2.0 license with explicit patent grant
