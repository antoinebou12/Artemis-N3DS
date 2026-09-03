# Artemis 3DS SD Card Data

Runtime user data lives on the **3DS SD card**, not in this repository.

## Path

All files are under:

```
sd:/3ds/moonlight/
```

(Legacy path name kept so existing pairing and settings continue to work.)

## Layout

| File / folder | Purpose |
|---------------|---------|
| `paired` | Saved host list (`ip:port` per line) |
| `host_profiles` | Per-host preset (`ip:port\|ProfileName`) |
| `last_host` | Last selected host |
| `moonlight.conf` | Stream and UI settings |
| `keys/client.pem`, `keys/key.pem`, `keys/client.p12` | Pairing certificates (sensitive) |
| `keys/uniqueid.dat` | Client unique ID (sensitive) |
| `diagnostics/diagnostic_*.txt` | Saved error dumps from the UI |

## For developers

- **`dist/` is for release packages only** — `artemis.3dsx`, `artemis.cia`, and install docs.
- **Never copy SD card contents into `dist/`** or commit pairing keys to git.
- When building releases, copy only the Artemis `.3dsx` / `.cia` binaries and documentation.

## Remove host vs unpair

Removing a saved host from the 3DS list clears `paired` and `host_profiles` entries for that host. Global `keys/` are shared across hosts; you may still need to unpair or re-pair on the PC if certificates were already issued.

## Backup / restore

To back up pairing and settings, copy `sd:/3ds/moonlight/` from the SD card — not from this repo.
