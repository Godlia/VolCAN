# SIM7080G vehicle modem handover

This document records the verified Raspberry Pi 5 interfaces and commands for
the SIM7080G modem. The C program should switch explicitly between LTE/PPP,
GNSS, and low-power states.

## Hardware and ports

- Raspberry Pi 5
- Waveshare SIM7080G Cat-M/NB-IoT/GNSS HAT
- Modem UART: `/dev/ttyAMA0`
- Modem USB serial interfaces: `/dev/ttyUSB0` through `/dev/ttyUSB3`
- USB modem network interface: `eth1` via `cdc_ether`
- PWRKEY: BCM GPIO4, physical header pin 7
- Serial speed: 57600 baud
- APN: `globaldata`

Stable USB paths use:

```text
/dev/serial/by-id/usb-SimTech__Incorporated_SimTech_SIM7080_1234567890ABCDEF-if02-port0
/dev/serial/by-id/usb-SimTech__Incorporated_SimTech_SIM7080_1234567890ABCDEF-if03-port0
```

The numeric `ttyUSB` names can change after reconnecting.

## Verified interface mapping

| Interface | Purpose |
|---|---|
| `/dev/ttyAMA0` | Verified direct PPP data connection |
| USB interface 02 | AT command and GNSS control |
| USB interface 03 | USB serial data interface; PPP was tested here |
| `eth1` | SIM7080 CDC Ethernet interface; not currently used |
| `ppp0` | Linux PPP network interface |

## Important limitation

Testing showed that this SIM7080 setup cannot reliably operate GNSS and PPP at
the same time. Multiple USB interfaces do not provide independent modem
sessions at the protocol level.

Treat these as mutually exclusive modes:

```text
LTE/PPP mode: GNSS disabled, PPP active
GNSS mode:    PPP stopped, GNSS active
SLEEP mode:   LTE and GNSS stopped, modem asleep
```

Do not poll GNSS while PPP is active.

## LTE/PPP mode

The reliable PPP path is:

```text
/dev/ttyAMA0 -> pppd -> ppp0
```

Peer configuration:

```text
/etc/ppp/peers/rnet-ama0
```

Important settings:

```text
/dev/ttyAMA0
57600
usepeerdns
defaultroute
replacedefaultroute
persist
noauth
nocrtscts
local
```

The chat script is:

```text
/etc/chatscripts/gprs
```

It sends:

```text
AT
ATH
ATE1
AT+CGDCONT=1,"IP","globaldata","",0,0
ATD*99#
```

Expected result:

```text
CONNECT
ppp0 appears
```

Useful commands:

```sh
sudo pon rnet-ama0
ip addr show ppp0
ip route
ping -4 -I ppp0 8.8.8.8
sudo poff rnet-ama0
```

The systemd service for automatic PPP startup is:

```text
vehicle-ppp-ama0.service
```

The C program must not start another PPP session when `ppp0` already exists.

## GNSS mode

Stop PPP before using GNSS:

```sh
sudo systemctl stop vehicle-ppp-ama0.service
sudo poff rnet-ama0 2>/dev/null || true
```

Open the USB AT/GNSS port:

```text
/dev/serial/by-id/usb-SimTech__Incorporated_SimTech_SIM7080_1234567890ABCDEF-if02-port0
```

Use 57600 baud, 8 data bits, no parity, and one stop bit.

Enable GNSS:

```text
AT+CGNSPWR=1
```

Query status:

```text
AT+CGNSINF
```

Example without a fix:

```text
+CGNSINF: 0,,,,,,,,,,,,,,,,,
```

The response fields begin with:

```text
field 1: GNSS run status
field 2: fix status
field 3: UTC timestamp
field 4: latitude
field 5: longitude
```

The existing test script polls every five seconds:

```text
gps_status.py
```

## Modem command handling

Commands terminate with carriage return:

```text
AT\r
AT+CGNSPWR=1\r
AT+CGNSINF\r
```

Wait for `OK`, `ERROR`, or the command-specific response. Flush stale input
before sending each command. Use read and write timeouts, and do not allow
multiple processes to access the same modem port.

## PWRKEY recovery

The existing recovery logic uses libgpiod:

```text
GPIO chip: /dev/gpiochip0
GPIO line: 4
```

Recovery sequence:

1. Set GPIO4 active for 4 seconds.
2. Set GPIO4 inactive.
3. Wait approximately 10 seconds.
4. Set GPIO4 active for 1 second.
5. Set GPIO4 inactive.
6. Wait until `AT` returns `OK`.

Do not pulse PWRKEY while PPP is active.

## Recommended state machine

```text
MODEM_OFF
MODEM_STARTING
MODEM_IDLE
GNSS_ACTIVE
PPP_STARTING
PPP_ACTIVE
PPP_STOPPING
MODEM_SLEEPING
MODEM_ERROR
```

Before switching GNSS to PPP:

1. Stop GNSS polling.
2. Send `AT+CGNSPWR=0`.
3. Close the AT port.
4. Verify the modem responds to `AT`.
5. Start PPP on `/dev/ttyAMA0`.
6. Wait for `ppp0`.

Before switching PPP to GNSS:

1. Stop PPP cleanly.
2. Wait until `ppp0` disappears.
3. Confirm `pppd` no longer owns `/dev/ttyAMA0`.
4. Open the USB AT/GNSS port.
5. Enable GNSS.
6. Start the five-second status loop.

## CDC Ethernet interface

The modem can expose `eth1` through the Linux `cdc_ether` driver. This is a
modem-provided USB network interface, not the Raspberry Pi's built-in Ethernet.
It has appeared with an address such as `192.168.225.20`, but it has not been
validated as the cellular Internet path. The C program should continue using
the verified serial PPP path.

## Safety constraints

- Do not send vehicle CAN commands from this modem-control code.
- Do not use the public MQTT broker for credentials, private GPS data, or
  vehicle commands.
- Do not start PPP and GNSS concurrently.
- Do not use `/dev/ttyUSB2` from multiple processes.
- Do not assume `ttyUSB` numbering is permanent.
- Do not pulse PWRKEY while PPP is active.
- Report modem, PPP, GNSS, and serial errors explicitly.
