# Detection remediation: evidence and practical route

Research date: 2026-10-10. This is a remediation plan, not a safety certification
or a declaration that existing findings are false positives.

## What a scan can establish

VirusTotal aggregates vendors' results; it cannot change their verdicts. A small
detection count is not proof of malware, and zero is not proof of safety. Their
[official guidance](https://docs.virustotal.com/docs/false-positive-contacts)
is to rescan, then contact the responsible vendor with the report if it persists.
Record every engine's result, including timeouts and unsupported formats. An
archive result does not replace individual EXE/DLL scans.

The review.2 findings remain preserved in `virustotal-20261010-r2.json`. Its EXE
was flagged by Elastic, Skyhigh and Zillya; core by Cynet; unchanged loader by
Cynet and Symantec. The new review.3 bytes require their own scans. A code fix
does not prove what caused an antivirus verdict or guarantee its removal.

## Concrete improvements made

The proxy now initializes Windows forwarding lazily rather than calling
LoadLibrary/CreateThread inside its DllMain. The patch engine checks suspension,
context, cache and protection failures, rolls failed patches back, preserves
foreign patches, reclaims unpublished allocations and attempts to restore owned
hooks before IL2CPP shutdown. These are actual correctness fixes based on source
review and controlled tests, not binary changes made merely to alter scan hashes.

Distribution remains an inspectable folder build in a standard manual ZIP,
with UPX disabled, pinned build inputs and hashes for all runtime files. No
obfuscation, antivirus exclusion, or protection-disable workaround is proposed.

## Vendor review destinations

| Vendor | Verified route | Evidence to submit |
| --- | --- | --- |
| Elastic | [Official instructions and linked form](https://discuss.elastic.co/t/submitting-false-positives/232322) | EXE/ZIP reports and hashes; source/build instructions; bootloader provenance |
| Symantec | [SymSubmit instructions](https://knowledge.broadcom.com/external/article/173729/how-to-submit-false-positives-on-content.html) | Exact loader hash/report; lazy loader change; benign loader fixture results |
| Zillya | [Official support page](https://zillya.com/support) (`support@zillya.com`) | EXE hash/report, XWorm label, complete application/source context |
| Cynet | [VirusTotal's current vendor directory](https://docs.virustotal.com/docs/false-positive-contacts) and [Cynet support/service route](https://www.cynet.com/services/) | Core/loader reports, intentional offline hooking behavior and native source |
| Skyhigh | [VirusTotal's current vendor directory](https://docs.virustotal.com/docs/false-positive-contacts) and [official support](https://www.skyhighsecurity.com/support.html) | EXE hash/report and Dropper label; folder-build inventory and installer behavior |

Elastic no longer accepts community false-positive reports by email; use the
form linked in its official instructions. For Cynet/Skyhigh, verify the directory
address with the vendor before disclosing samples. Do not use unrelated CASB
"mark anomaly as false positive" settings as a file-detection submission method.

The request must ask the vendor to investigate, without claiming its verdict is
wrong before analysis. Keep the exact submitted bytes stable, save case IDs and
responses, then rescan those same hashes after the vendor reports an update.
`vendor-review-request.md` is a draft. No vendor messages were sent this turn.

## Signing: useful, with limits

Authenticode identifies the publisher and protects signed-file integrity. It
does not certify that a program is harmless or force vendors to remove findings.
[Microsoft's current SmartScreen guidance](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)
says even new signed files may trigger reputation warnings, and EV certificates
no longer receive an automatic SmartScreen bypass. Self-signing is not a
substitute for a publicly trusted publisher certificate.

Sign the project EXE, proxy and core before generating manifests and ZIPs. Verify
the signature/timestamp, regenerate hashes, then scan and submit those exact
signed bytes. Use the same verified publisher identity for subsequent releases.
Do not modify signed binaries afterward. Required upstream runtimes retain their
upstream identity/licensing; do not pretend they are project-authored code.

There are two eligibility constraints relevant here:

- [SignPath Foundation](https://signpath.org/terms.html) requires an OSI-approved
  license and other project/release/security conditions. EpochPact's current
  `docs/nexus/DISTRIBUTION-PERMISSIONS.txt` expressly does not grant an open-source
  license. Public source is not sufficient; free signing eligibility is not
  established. No license was changed or application submitted.
- [Microsoft Artifact Signing](https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart)
  limits public-trust individual validation to US/Canada and lists separate
  eligible organization countries. Do not assume availability from an Azure
  region or from where the developer's PC happens to be located. An eligible
  certificate/service needs identity validation and owner approval/payment.

## Remaining work that code/tests alone cannot establish

The real-game hover behavior and exit-crash comparison remain unverified for
review.3. A first forwarded call made by another DLL's DllMain can still occur
under that caller's loader lock; lazy resolution does not universally prove
loader-lock safety for arbitrary hosts. Stable thread snapshots reduce detected
churn but cannot forbid thread creation after preflight.

The explicit administrator installer still runs the bundled Python application
tree; it needs a separate minimal trusted helper/installation location before
the review's tamper-before-elevation concern can be closed. OS-known result
paths, junction rejection and exclusive result creation improve output handling
but are not an atomic directory-handle defense against every local race.

No zero-detection claim, automatic vendor clearance, clean-release promotion,
or explanation of a specific engine's heuristics follows from these changes.
