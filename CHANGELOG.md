# Changelog

## 2026-02-28

### Added
- New clock effect: **Karambol** (`fxPileup`) with panel toggle and test trigger.
- Full integration of `fxPileup` across settings API, Preferences save/load, and MQTT-applied effect config.

### Changed
- Extended karambol duration and smoother ending phase.
- OTA progress display reverted from `000%` format to normal `%` format for better readability.

### Technical
- Updated effect wiring signatures in display and manager layers to include `pileupEnabled`.
