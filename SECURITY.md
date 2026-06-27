# Security policy

## Supported versions

| Version | Supported |
| --- | --- |
| 1.x | Yes |
| Pre-1.0 prototypes | No |

Only the latest published release receives security fixes.

## Reporting a vulnerability

Do **not** open a public issue for a security vulnerability.

Use GitHub's private vulnerability-reporting flow from the repository's **Security** tab. Include:

- A clear description of the problem
- The affected awfan version
- Reproduction steps
- Expected and actual behavior
- The security impact
- Relevant logs or proof of concept with personal information removed

If private vulnerability reporting is unavailable, contact the maintainer through the GitHub profile associated with this repository and ask for a private reporting channel. Do not include exploit details in the initial public message.

## Scope

Security reports may include:

- Unsafe or unintended AWCC firmware writes
- Bypasses of the `--yes` write-confirmation requirement
- Command or path injection in packaging scripts
- Insecure update or release behavior
- Privilege escalation
- Sensitive-data exposure
- Malicious package or checksum substitution

Ordinary compatibility problems, unsupported hardware, incorrect sensor labels, and feature requests should use the public issue templates instead.

## Response

Reports will be acknowledged as soon as practical. Confirmed vulnerabilities will be investigated privately, fixed on a dedicated branch, and disclosed after a patched release is available.

Please allow reasonable time for investigation before public disclosure.
