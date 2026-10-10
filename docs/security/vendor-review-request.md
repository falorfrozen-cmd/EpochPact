# Candidate detection review — draft, not submitted

Use the vendor responsible for each detection, as described in
[VirusTotal's vendor-contact guidance](https://docs.virustotal.com/docs/false-positive-contacts).
VirusTotal aggregates engine results; a small detection count or a clean result
does not settle whether a file is safe. This draft requests analysis without
declaring a false positive.

Current candidate findings: EXE — Elastic, Microsoft and Skyhigh (SWG);
loader — Cynet and Symantec; unchanged core — Cynet; ZIP — Elastic.
Microsoft's official submission page is
https://www.microsoft.com/en-us/wdsi/filesubmission.
The contact directory links Elastic's review form and lists the relevant other
vendors. Review the official destination before submitting. No vendor request
has been sent as part of this candidate review.

Submit each responsible vendor's relevant files/report links rather than
claiming that one vendor flagged every component. The complete measurements
and immutable hashes are in [the fresh reports](virustotal-20261010.json).

| Component | SHA-256 | Finding |
| --- | --- | --- |
| version.dll | `bee68619c1869f7cb2b7c5e0cde123137e58c08fc8ec4f5fca237fb1c107370d` | Cynet: Malicious (score: 100); Symantec: ML.Attribute.HighConfidence |
| EpochPact.Core.dll | `6265d3e4f15a609f174a3b577dd81e6df2e59dea2cfbbecef66f43d8d52f7a9f` | Cynet: Malicious (score: 100) |

Loader report: https://www.virustotal.com/gui/file/bee68619c1869f7cb2b7c5e0cde123137e58c08fc8ec4f5fca237fb1c107370d

Core report: https://www.virustotal.com/gui/file/6265d3e4f15a609f174a3b577dd81e6df2e59dea2cfbbecef66f43d8d52f7a9f

## Message

Subject: Detection review request — EpochPact offline game tool, candidate EXE

Hello,

I maintain EpochPact, an open-source Windows tool for offline Last Epoch. Please
review your detection of this local security-review candidate. I have not
established the cause and am not asking users to disable security products.

File: EpochPact.exe, folder-build candidate 0.1.1-alpha.2-review.1, 370,889 bytes.
SHA-256: 068bc76120b2a8d17c8d4f87a51f3ebaf6e2696829e80ec2d4492498c6590435.
VirusTotal: https://www.virustotal.com/gui/file/068bc76120b2a8d17c8d4f87a51f3ebaf6e2696829e80ec2d4492498c6590435

Observed detections:

- Elastic: Malicious (moderate Confidence)
- Microsoft: Trojan:Win32/Wacatac.B!ml
- Skyhigh (SWG): BehavesLike.Win64.Dropper.fh

The local Microsoft Defender custom scan reported no threats both before and
after the VirusTotal analysis, with real-time protection enabled and signatures
1.459.641.0 / 1.459.645.0 respectively. This discrepancy is recorded without
claiming either result is conclusive.

Source and build audit:
https://github.com/falorfrozen-cmd/EpochPact/blob/round1-xp-multiplier/docs/security/review-20261010.md

The EXE is built with official PyInstaller 6.20.0. Its executable .text section
matches the audited official runw.exe bootloader. Required Python/runtime files
are separately visible in the app folder. Exact PyPI inputs are pinned and
hashed; 1,978 installed source/data/native files matched official artifacts.
There is no UPX packing. The native game loader/core are separate required files,
with source available in the repository. The game core uses hooks for offline
modding; it is unchanged and has a separately recorded Cynet detection.

The review ZIP is a Windows application/manual installation package, not an
upload to PyPI or a replacement click distribution. VirusTotal's Code insights
labels it as click 8.5.0 (whl); genuine upstream dependency metadata is present
inside the application runtime. Please consider the actual application identity
and contents when reviewing that classification. This does not explain or
invalidate the separate engine detections.

ZIP SHA-256: 50faf9151bc1c95f80efb4e85f3fcb2a1bd3dad2af10a00b3e924a241907416c.
ZIP report: https://www.virustotal.com/gui/file/50faf9151bc1c95f80efb4e85f3fcb2a1bd3dad2af10a00b3e924a241907416c

Please identify the component or behavior that triggered your finding, and
whether you need the full runtime bundle, source or further evidence. The
candidate is not published as an approved replacement for the quarantined Nexus
files. Their original bytes are preserved for an existing Nexus support request.

Thank you,
Falor
