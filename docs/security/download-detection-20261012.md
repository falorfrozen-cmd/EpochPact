# GitHub download detection — 2026-10-12

## Observed failure

Chrome reported "Virus detected" for `EpochPact-0.1.2-dev.10.zip`. The local
Microsoft Defender history identifies **Trojan:Win32/Wacatac.C!ml** on the two
copies of **version.dll** inside that ZIP:

- `CopyToGame/version.dll`
- `EpochPact/_internal/native/build/version.dll`

Defender recorded a successful protection action and `DidThreatExecute: false`.
This history does not identify the desktop EXE as the detected component. The
exact reason for the loader verdict has not been established, and no Microsoft
analyst has classified this finding as a false positive.

## Current result

Defender security intelligence was updated from **1.459.673.0** to
**1.459.676.0** using Microsoft's existing installed tool. Custom diagnostic
scans returned exit code **0** and an explicit **No threats found** result for:

| Sample | Result |
|---|---|
| dev.11 version.dll | No threats found |
| dev.11 player ZIP | No threats found |
| dev.11 complete extracted runtime folder | No threats found |
| Unchanged dev.10 player ZIP | No threats found |

The scans used `MpCmdRun -Scan -ScanType 3 -DisableRemediation` to preserve
diagnostic samples. Real-time, behavior and downloaded-file protection remained
enabled. No exclusion or protection-disable setting was added, and quarantined
files were not restored. The separate exclusion-inspection command was denied
access; this report does not claim all pre-existing user exclusions were checked.

The actual **dev.11 GitHub asset was then downloaded through the user's Chrome**.
The download API returned a completed local file, 26,886,873 bytes. Its SHA-256
matches the published ZIP:

`12b1a88abb6796259af85ead0ea38d1ccf9cbd732ecdab5594d450257d346d01`

The downloaded archive's complete inventory verification passed. Defender history
had no new dev.11 detection. The browser tool refused `chrome://downloads` under
its URL security policy; no workaround was attempted. Consequently the download
manager's visual status was not checked. Completion was verified from Chrome's
returned download path, the actual file, its bytes/hash and inventory.

## What changed and what did not

This investigation updated Defender definitions and verified the existing current
release. No binary was rebuilt just to change its hash, and the required loader
was not removed from the package. The previously published dev.11 EXE, DLLs and
ZIP retain their original hashes and connection/gameplay validation scope.

The loader source was inspected. It forwards version-information APIs to the
real System32 library, limits core startup to `Last Epoch.exe`, honors its
disabled marker, and loads the core from an explicit game-relative path with
restricted dependency search. No new code defect explaining this specific
Defender verdict was established by this inspection. This is not an exhaustive
security audit or proof of safety.

A clean local scan alone would not establish a successful browser download; that
flow was tested separately. These results apply to this computer, exact files
and signature version. They do not guarantee a clean result from every security
product, another machine or future definitions. No new VirusTotal or Nexus
clearance is claimed. Structured evidence is in
[download-detection-20261012.json](download-detection-20261012.json); private raw
logs and user paths remain outside the public report.

If a finding recurs, retain the exact hash, detection name and definition version
and request analysis of that specific DLL through
[Microsoft's software-developer submission process](https://learn.microsoft.com/en-us/defender-xdr/developer-faq).
The [official submission form](https://www.microsoft.com/en-us/wdsi/filesubmission)
asks for the security product, exact sample, detection name and explanatory
context. No vendor submission was made in this investigation.
