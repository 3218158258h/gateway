#!/usr/bin/env python3
import argparse
import os
import select
import signal
import sys
import termios
import threading
import time


def parse_hex(s: str) -> bytes:
    s = s.strip().replace(" ", "").replace("_", "")
    if s.startswith("0x"):
        s = s[2:]
    if len(s) % 2 != 0:
        raise ValueError(f"hex length must be even: {s}")
    return bytes.fromhex(s)


def build_bt_frame(device_id: bytes, payload: bytes) -> bytes:
    if len(device_id) != 2:
        raise ValueError("device-id must be exactly 2 bytes (4 hex chars)")
    if len(payload) > 250:
        raise ValueError("payload too large; must be <= 250 bytes")
    length = len(device_id) + len(payload)
    return b"\xF1\xDD" + bytes([length]) + device_id + payload


def set_raw(fd: int) -> None:
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = attrs[2] | termios.CREAD | termios.CLOCAL
    attrs[3] = 0
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attrs)


def at_ack_loop(fd: int, stop_event: threading.Event) -> None:
    buf = bytearray()
    while not stop_event.is_set():
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r:
            continue
        try:
            data = os.read(fd, 512)
        except OSError:
            break
        if not data:
            continue

        buf.extend(data)
        while b"\r\n" in buf:
            line, _, rest = buf.partition(b"\r\n")
            buf[:] = rest
            if line.startswith(b"AT"):
                try:
                    os.write(fd, b"OK\r\n")
                    print(f"[AT] recv={line!r} -> send=b'OK\\r\\n'")
                except OSError:
                    return
            elif line:
                print(f"[RX] {line.hex()}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Simulate lower-device data over virtual serial node")
    parser.add_argument("--port", required=True, help="Simulator-side serial node path, e.g. /tmp/gateway-vdev/sim0")
    parser.add_argument("--device-id", default="0001", help="2-byte device id in hex, default: 0001")
    parser.add_argument("--payload", default="0102", help="payload hex string, default: 0102")
    parser.add_argument("--count", type=int, default=1, help="number of frames to send, default: 1")
    parser.add_argument("--interval", type=float, default=1.0, help="interval seconds between frames, default: 1.0")
    parser.add_argument("--ack-at", action="store_true", help="auto reply OK\\r\\n when gateway sends AT commands")
    args = parser.parse_args()

    if args.count < 0:
        print("[ERROR] --count must be >= 0", file=sys.stderr)
        return 1

    device_id = parse_hex(args.device_id)
    payload = parse_hex(args.payload)
    frame = build_bt_frame(device_id, payload)

    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY)
    set_raw(fd)

    stop_event = threading.Event()

    def on_signal(signum, _):
        print(f"\n[INFO] signal={signum}, exiting...")
        stop_event.set()

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    ack_thread = None
    if args.ack_at:
        ack_thread = threading.Thread(target=at_ack_loop, args=(fd, stop_event), daemon=True)
        ack_thread.start()

    sent = 0
    try:
        if args.count == 0:
            print("[INFO] --count=0, listen/AT-ACK mode; press Ctrl+C to exit")
            while not stop_event.is_set():
                time.sleep(0.2)
        else:
            for i in range(args.count):
                if stop_event.is_set():
                    break
                os.write(fd, frame)
                sent += 1
                print(f"[TX {i + 1}/{args.count}] {frame.hex()}")
                if i < args.count - 1:
                    time.sleep(max(args.interval, 0.0))
    finally:
        stop_event.set()
        if ack_thread:
            ack_thread.join(timeout=1.0)
        os.close(fd)

    print(f"[DONE] sent={sent}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
