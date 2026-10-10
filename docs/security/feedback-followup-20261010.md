# Player bug follow-up — 10 October 2026

This source follow-up is separate from the immutable review.2 binaries and scans.
It does not declare the antivirus findings false positives or clear distribution.

## Tooltip read failures

The prior hover guard used `StaticObject` and `IsAlive`, whose production
implementations already caught access failures and returned null/false. The
outer guard could therefore mistake an unreadable field/object for no tooltip.
The earlier item fixture threw directly and did not expose this production
error-swallowing path.

`TryStaticObject` and `TryIsAlive` now report successful reads separately from
their values. Item collection pauses on either unknown state. Legacy accessors
keep their null/false interface. A successful null/dead object still permits
normal collection. The original distant-pickup tick remains untouched; disabling
vanilla input is not justified by the reported mod interruption.

The locally held metadata for the supported game records
`TooltipItem : MonoBehaviour` with static `TooltipItem.highlightedTooltipItem`.
The installed GameAssembly SHA-256 is
`d5f9fb458f0e9878e697a92b590a586a52fd77e645f8bf87bb4f2b74c6c56567`, matching
the supported build. This confirms the field name in that metadata, not an actual
live hover/resume session. No game was running or launched for these tests.

## CLI errors

The CLI catches `OSError` only at its top-level dispatch and returns exit 2 with
the original error detail, rather than a traceback. Backend functions continue
to raise `PermissionError`, preserving the UI's explicit installation response.
A process-enumeration error still prevents installation and game writes.

## Validation

- 51 Python session/setup tests passed, including the two new CLI boundary tests.
- `items_test`: 12/12; adds unreadable Unity-lifetime state to hover/resume checks.
- `game_read_test`: 12/12; compiles the actual production game accessors and checks
  partial read failure, missing APIs/metadata, null references, live/destroyed
  objects and failed lifetime reads.
- The other 10 compiled native test executables passed in the isolated player
  build. This is controlled native validation, not real-game testing.
- The review.2 archives, installed game DLLs and player saves were not changed.

Further loader/hook/security fixes and newly rebuilt binaries need their own
results and hashes. The review.2 scan results must not be reused for them.
