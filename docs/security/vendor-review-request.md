# Candidate detection review — draft, not submitted

Use [VirusTotal's vendor-contact directory](https://docs.virustotal.com/docs/false-positive-contacts)
and verify each destination against the vendor's official site before submission.
Send a vendor only the findings relevant to its engine. No vendor submission has
been made as part of review.2; the existing Nexus support request is a separate
case. This message requests analysis without declaring a false positive.

## Responsible vendors / current evidence

| Artifact | Findings | Exact SHA-256 |
| --- | --- | --- |
| EpochPact.exe | Elastic: Malicious (moderate Confidence); Skyhigh (SWG): BehavesLike.Win64.Dropper.fh; Zillya: Backdoor.XWorm.Win32.3294 | `64eee8abe749bfda5d03bab8de072aa39ea9ad5b35a78a3e9d98ab6cb7a8cf49` |
| EpochPact.Core.dll | Cynet: Malicious (score: 100) | `e391eb9bde4d4174fa3b5411623c3d22a94061fccc3aeec0b64b629c3895ccc5` |
| version.dll, unchanged | Cynet: Malicious (score: 100); Symantec: ML.Attribute.HighConfidence | `bee68619c1869f7cb2b7c5e0cde123137e58c08fc8ec4f5fca237fb1c107370d` |
| Review.2 Manual ZIP | Elastic: Malicious (moderate Confidence) | `eb9c6deccfbf8a3f5442ad6ee8f5ebd9cdd4385816c0c4717b19a885c7320874` |

Microsoft is Undetected on the new EXE report. The earlier Microsoft detection
and original Nexus files are retained in [the review.1 history](review-20261010.md);
this is not evidence that all security findings are resolved.

## Message

Subject: Detection review request — EpochPact offline Last Epoch tool

Hello,

I maintain EpochPact, an open-source Windows tool for offline Last Epoch. Please
review your engine's findings on candidate 0.1.1-alpha.2-review.2. The cause is
not established, and users are not asked to disable security products or add
exclusions. This candidate has not replaced the quarantined Nexus files.

Relevant file hashes and engine labels are listed above. Public VirusTotal
reports and exact byte sizes are in:
https://github.com/falorfrozen-cmd/EpochPact/blob/round1-xp-multiplier/docs/security/virustotal-20261010-r2.json

Source, build changes, validation scopes and remaining limitations:
https://github.com/falorfrozen-cmd/EpochPact/blob/round1-xp-multiplier/docs/security/review-20261010-r2.md

The Windows EXE is a PyInstaller 6.20.0 folder build with required runtime files
separately visible. Its executable .text section matches the audited official
runw.exe bootloader; this comparison does not audit the appended application.
The build uses 23 pinned official PyPI inputs; 1,978 installed files matched the
upstream artifacts. UPX is disabled. Full loader/core source is in the same
repository. The native core intentionally hooks offline game functions.

Review.2 pauses automatic pickup during item hover, preserves installation
access-denied errors, and enumerates process names through Windows Toolhelp
without spawning tasklist. Generated trampolines are built RW and then sealed
RX before publication; target-function patching still temporarily changes code
protection. The loader remains byte-identical to review.1. Its unresolved
System32 LoadLibrary call in DllMain is disclosed in the review.

Local Microsoft Defender custom scans of the exact candidate folder and ZIP
reported no threats, with signatures 1.459.645.0 and real-time protection enabled.
VirusTotal still reports the findings above. These observations are recorded
separately; neither is presented as conclusive evidence of safety.

The ZIP is a Windows application/manual installation bundle, not a PyPI package
submission. VirusTotal's archive size warning prevents treating the ZIP report
as a complete scan of every embedded file; the EXE/core were scanned separately
and the unchanged loader's earlier completed report is retained.

Please identify which component or behavior triggered your finding and whether
you need the runtime bundle, source or further evidence. The exact archives and
original quarantined files are preserved for review.

Thank you,
Falor
