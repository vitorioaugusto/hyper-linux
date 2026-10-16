# Cloud III Wireless protocol notes

This document describes the small command set implemented by Hyper Linux.
It is not a vendor specification or a claim of full NGENUITY compatibility.

## Identification and transport

The backend matches vendor `03f0`, product `05b7` or `0c9d`, with vendor HID
collection `ff13` and report ID `66`. It does not attach to unrelated media-key
collections. The tested `05b7` dongle uses USB interface 3.

The tested device accepts 64-byte output packets and returns 62-byte reports.
The backend tries 64-byte output first and only probes 62 bytes if a read-only
connection query does not receive a reply. A libusb endpoint-zero SET_REPORT
fallback is available; it does not detach or claim the audio driver.

Each packet starts with report ID `66`, then a command byte and optional value;
the remaining output bytes are zero.

| Operation | Query | Setter |
|---|---|---|
| Wireless connection | `66 82` | — |
| Microphone mute | `66 86` | `66 03 00/01` |
| Hardware sidetone | `66 84` | `66 01 00/01` |
| Auto-off timer | `66 85` | `66 02 minutes` |
| Battery | `66 89` | — |
| Charging | `66 8a` | — |
| Color enumeration | `66 8f` | — |

The timer accepts 0 for never, or 1–255 minutes. The UI uses one mute control
through the audio server when available, because Linux already integrates
microphone mute on the tested system. It does not redundantly write the mute
state back after every notification.

## Incoming reports

- Getter replies generally echo the command byte.
- Sidetone notifications use `66 09 value`.
- Mute notifications use `66 0a value`.
- Connection notifications use `66 0b value`.
- Charging notifications use `66 0c value`, where 0 is not charging, 1 is
  charging, and 2 is fully charged.
- Battery notifications use `66 0d ...`; the percentage is byte 4. If bytes 2
  and 3 are both zero, the report does not contain a valid battery reading.

An immediate getter after a setter can return the previous value. Captures on
the tested dongle showed a later notification roughly 0.6–0.8 seconds after
the write. The backend waits for that notification before treating the
operation as a failure. This interval does not establish acoustic latency.

## Validation

The fixture in `tests/fixtures/cloud3-query.json` contains 64-byte requests
and 62-byte responses from the tested `03f0:05b7` device: connected, 82%
battery, not charging, unmuted, sidetone off, auto-off at 20 minutes. No serial
number queries or microphone audio are included.

Tests also cover malformed and unrelated reports, invalid battery readings,
disconnect state cleanup, delayed setter confirmation, notification handling,
request framing, and the software audio graph.

The protocol was reconstructed while examining a locally supplied NGENUITY
3.1.0 installer and validated with device queries and user testing. This
repository distributes the independent Linux implementation and a small
packet fixture, not the installer, vendor executables, extracted resources
or disassembly.
