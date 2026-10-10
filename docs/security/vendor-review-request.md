# Candidate detection review — draft, not submitted

Use [VirusTotal's vendor-contact directory](https://docs.virustotal.com/docs/false-positive-contacts)
and verify each destination against the vendor's official site before submission.
Send a vendor only the findings relevant to its engine. No vendor submission has
been made as part of review.2; the existing Nexus support request is a separate
case. This message requests analysis without declaring a false positive.

## Current candidate: review.3

The source fixes and new candidate have their own hashes. Do not attach review.2
engine labels to these new bytes. The fresh loader scan still reports two
findings; EXE, core and ZIP submissions currently require CAPTCHA completion.
Use the current scan record to refresh the relevant rows before sending.

| Artifact | Current scan status | Exact SHA-256 |
| --- | --- | --- |
| EpochPact.exe | Pending CAPTCHA; no current verdict | `5d0c15eb6d70d82a7cd4f4cfd0bfae3f43c28a84e3f04a2a60136f32cc8f1106` |
| EpochPact.Core.dll | Pending CAPTCHA; no current verdict | `7b00bf42ac09ef003eb3a7c67e7b297e39868789b46b2bf7fc77296ab175c13e` |
| version.dll | Fresh scan: Cynet Malicious (score: 100); Symantec ML.Attribute.HighConfidence; 2/71 | `3e695920df290cb7b6d146db728cdbdb3def7f359ffd7c2032e0f219b109e181` |
| Review.3 Manual ZIP | Pending CAPTCHA; archive scan limitations apply | `f0ad5d29217d47d57a0bc35d97867550c19d5d90229c079682b77188aba4f30e` |

Current source review, exact build evidence and scan status:

- https://github.com/falorfrozen-cmd/EpochPact/blob/round1-xp-multiplier/docs/security/review-20261010-r3.md
- https://github.com/falorfrozen-cmd/EpochPact/blob/round1-xp-multiplier/docs/security/candidate-results-20261010-r3.json
- https://github.com/falorfrozen-cmd/EpochPact/blob/round1-xp-multiplier/docs/security/virustotal-20261010-r3.json

## Historical review.2 evidence (different bytes)

| Artifact | Findings | Exact SHA-256 |
| --- | --- | --- |
| EpochPact.exe | Elastic: Malicious (moderate Confidence); Skyhigh (SWG): BehavesLike.Win64.Dropper.fh; Zillya: Backdoor.XWorm.Win32.3294 | `64eee8abe749bfda5d03bab8de072aa39ea9ad5b35a78a3e9d98ab6cb7a8cf49` |
| EpochPact.Core.dll | Cynet: Malicious (score: 100) | `e391eb9bde4d4174fa3b5411623c3d22a94061fccc3aeec0b64b629c3895ccc5` |
| version.dll, unchanged | Cynet: Malicious (score: 100); Symantec: ML.Attribute.HighConfidence | `bee68619c1869f7cb2b7c5e0cde123137e58c08fc8ec4f5fca237fb1c107370d` |
| Review.2 Manual ZIP | Elastic: Malicious (moderate Confidence) | `eb9c6deccfbf8a3f5442ad6ee8f5ebd9cdd4385816c0c4717b19a885c7320874` |

Microsoft is Undetected on the new EXE report. The earlier Microsoft detection
and original Nexus files are retained in [the review.1 history](review-20261010.md);
this is not evidence that all security findings are resolved.

## Message for review.3 (draft, unsent)

Subject: Detection review request — EpochPact offline Last Epoch tool

Hello,

I maintain EpochPact, a Windows tool with publicly available source for offline Last Epoch. Please
review your engine's findings on candidate 0.1.1-alpha.2-review.3. The cause is
not established, and users are not asked to disable security products or add
exclusions. This candidate has not replaced the quarantined Nexus files.

Relevant current file hashes and scan status are listed above. Public VirusTotal
reports and exact byte sizes are in:
https://github.com/falorfrozen-cmd/EpochPact/blob/round1-xp-multiplier/docs/security/virustotal-20261010-r3.json

Source, build changes, validation scopes and remaining limitations:
https://github.com/falorfrozen-cmd/EpochPact/blob/round1-xp-multiplier/docs/security/review-20261010-r3.md

The Windows EXE is a PyInstaller 6.20.0 folder build with required runtime files
separately visible. Its executable .text section matches the audited official
runw.exe bootloader; this comparison does not audit the appended application.
The build uses 23 pinned official PyPI inputs; 1,978 installed files matched the
upstream artifacts. UPX is disabled. Full loader/core source is in the same
repository. The native core intentionally hooks offline game functions.

Review.3 preserves unknown tooltip reads, preserves installation access-denied
errors, and enumerates processes through Windows Toolhelp without tasklist.
DllMain now only records its module and disables thread notifications. Version
forwarding uses InitOnce at the first API call with System32-restricted loading;
register/stack forwarding and dependency search were checked with benign compiled
fixtures. The core patch engine now checks thread/cache/protection failures,
rolls failed writes back, preserves foreign patches and attempts to restore all
owned hooks before IL2CPP shutdown. Published trampolines are RX and immutable;
target-function patching still temporarily changes code protection.

Local Microsoft Defender custom scans of the exact candidate folder and ZIP
reported no threats, with signatures 1.459.645.0 and real-time protection enabled.
The completed new loader report still has the findings above. Other current
submissions are pending CAPTCHA and have no new verdict recorded. These
observations are recorded separately; none is conclusive evidence of safety.

The ZIP is a Windows application/manual installation bundle, not a PyPI package
submission. VirusTotal's archive size warning prevents treating the ZIP report
as a complete scan of every embedded file. Review.2's completed individual
reports are retained as history, not substituted for the new binaries' scans.

Remaining limitations include the user-writable app's explicit administrator
installation path, live tooltip semantics and the previously observed game exit
crash. These are disclosed rather than represented as fixed by fixture tests.

Please identify which component or behavior triggered your finding and whether
you need the runtime bundle, source or further evidence. The exact archives and
original quarantined files are preserved for review.

Thank you,
Falor
