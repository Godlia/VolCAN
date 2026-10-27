# VolCAN
Volvo CAN bus whatchamacallit

## C scaffold

`volcan_scaffold.c` provides a simple scaffold for:
- controlling `pppd` link up/down
- placeholder MQTT publish/subscribe hooks
- sending and sniffing frames on `can0`

Build:

```bash
gcc -Wall -Wextra -pedantic -std=c11 volcan_scaffold.c -o volcan_scaffold
```

Usage:

```bash
./volcan_scaffold --help
```
