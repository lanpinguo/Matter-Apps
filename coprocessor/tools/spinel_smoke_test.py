#!/usr/bin/env python3
"""
Simple Spinel/HDLC smoke test for OpenThread RCP over UART.

Usage example:
  python3 tools/spinel_smoke_test.py --port /dev/ttyACM0 --baud 1000000 --rtscts
"""

import argparse
import sys
import time

import serial


# Spinel command IDs
SPINEL_CMD_RESET = 1
SPINEL_CMD_PROP_VALUE_GET = 2
SPINEL_CMD_PROP_VALUE_IS = 6

# Spinel property IDs
SPINEL_PROP_LAST_STATUS = 0
SPINEL_PROP_PROTOCOL_VERSION = 1
SPINEL_PROP_NCP_VERSION = 2
SPINEL_PROP_CAPS = 5

# Spinel header
SPINEL_HEADER_FLAG = 0x80
SPINEL_HEADER_IID_0 = 0x00

# HDLC bytes
HDLC_FLAG = 0x7E
HDLC_ESC = 0x7D
HDLC_XOR = 0x20


def pack_uint(value: int) -> bytes:
    """Encode Spinel packed unsigned integer."""
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            byte |= 0x80
        out.append(byte)
        if not value:
            break
    return bytes(out)


def unpack_uint(buf: bytes, offset: int = 0):
    """Decode Spinel packed unsigned integer. Returns (value, next_offset)."""
    value = 0
    shift = 0
    i = offset
    while i < len(buf):
        b = buf[i]
        value |= (b & 0x7F) << shift
        i += 1
        if (b & 0x80) == 0:
            return value, i
        shift += 7
    raise ValueError("Truncated packed uint")


def hdlc_fcs16(data: bytes) -> int:
    """PPP/HDLC FCS16 (CRC-16/IBM-SDLC style used by Spinel HDLC)."""
    fcs = 0xFFFF
    for b in data:
        fcs ^= b
        for _ in range(8):
            if fcs & 1:
                fcs = (fcs >> 1) ^ 0x8408
            else:
                fcs >>= 1
    return fcs & 0xFFFF


def hdlc_encode(payload: bytes) -> bytes:
    fcs = hdlc_fcs16(payload) ^ 0xFFFF
    frame = payload + bytes([fcs & 0xFF, (fcs >> 8) & 0xFF])
    out = bytearray([HDLC_FLAG])
    for b in frame:
        if b in (HDLC_FLAG, HDLC_ESC):
            out.append(HDLC_ESC)
            out.append(b ^ HDLC_XOR)
        else:
            out.append(b)
    out.append(HDLC_FLAG)
    return bytes(out)


def hdlc_decode(frame: bytes) -> bytes:
    if len(frame) < 4:
        raise ValueError("Frame too short")
    if frame[0] != HDLC_FLAG or frame[-1] != HDLC_FLAG:
        raise ValueError("Bad HDLC boundaries")

    raw = bytearray()
    esc = False
    for b in frame[1:-1]:
        if esc:
            raw.append(b ^ HDLC_XOR)
            esc = False
        elif b == HDLC_ESC:
            esc = True
        else:
            raw.append(b)
    if esc:
        raise ValueError("Dangling escape byte")
    if len(raw) < 3:
        raise ValueError("Decoded frame too short")

    payload = bytes(raw[:-2])
    rx_fcs = raw[-2] | (raw[-1] << 8)
    calc_fcs = hdlc_fcs16(payload) ^ 0xFFFF
    if rx_fcs != calc_fcs:
        raise ValueError(f"FCS mismatch: rx=0x{rx_fcs:04x} calc=0x{calc_fcs:04x}")
    return payload


def parse_spinel_payload(payload: bytes):
    if len(payload) < 2:
        raise ValueError("Spinel payload too short")
    header = payload[0]
    cmd, off = unpack_uint(payload, 1)
    return header, cmd, payload[off:]


def make_header(tid: int) -> int:
    return SPINEL_HEADER_FLAG | SPINEL_HEADER_IID_0 | (tid & 0x0F)


class SpinelTester:
    def __init__(self, ser: serial.Serial):
        self.ser = ser
        self.tid = 1

    def next_tid(self) -> int:
        self.tid += 1
        if self.tid == 0 or self.tid > 15:
            self.tid = 1
        return self.tid

    def send_cmd(self, cmd: int, data: bytes = b"", tid: int = None):
        if tid is None:
            tid = self.next_tid()
        payload = bytes([make_header(tid)]) + pack_uint(cmd) + data
        self.ser.write(hdlc_encode(payload))
        self.ser.flush()
        return tid

    def read_one_frame(self, timeout_s: float):
        deadline = time.time() + timeout_s
        buf = bytearray()
        in_frame = False
        while time.time() < deadline:
            b = self.ser.read(1)
            if not b:
                continue
            byte = b[0]
            if byte == HDLC_FLAG:
                if in_frame and len(buf) > 0:
                    frame = bytes([HDLC_FLAG]) + bytes(buf) + bytes([HDLC_FLAG])
                    return frame
                in_frame = True
                buf.clear()
            elif in_frame:
                buf.append(byte)
        return None

    def read_response(self, expect_tid: int, timeout_s: float = 2.0, allow_tid0: bool = False):
        while True:
            frame = self.read_one_frame(timeout_s)
            if frame is None:
                raise TimeoutError("Timeout waiting for Spinel response")
            payload = hdlc_decode(frame)
            header, cmd, data = parse_spinel_payload(payload)
            tid = header & 0x0F
            if tid != expect_tid and not (allow_tid0 and tid == 0):
                # Ignore async/event frames from other TIDs.
                continue
            return cmd, data

    def cmd_reset(self):
        tid = self.send_cmd(SPINEL_CMD_RESET)
        # Some RCP builds may emit reset status on TID=0.
        cmd, data = self.read_response(tid, timeout_s=3.0, allow_tid0=True)
        if cmd != SPINEL_CMD_PROP_VALUE_IS:
            raise RuntimeError(f"Unexpected cmd for RESET response: {cmd}")
        prop, off = unpack_uint(data, 0)
        if prop != SPINEL_PROP_LAST_STATUS:
            raise RuntimeError(f"RESET did not return LAST_STATUS, got prop {prop}")
        status, _ = unpack_uint(data, off)
        return status

    def get_prop(self, prop_id: int):
        tid = self.send_cmd(SPINEL_CMD_PROP_VALUE_GET, pack_uint(prop_id))
        deadline = time.time() + 2.0
        while time.time() < deadline:
            cmd, data = self.read_response(tid, timeout_s=0.5, allow_tid0=True)
            if cmd != SPINEL_CMD_PROP_VALUE_IS:
                continue
            prop, off = unpack_uint(data, 0)
            if prop == prop_id:
                return prop, data[off:]
            if prop == SPINEL_PROP_LAST_STATUS:
                status, _ = unpack_uint(data, off)
                raise RuntimeError(f"RCP returned LAST_STATUS={status} for prop {prop_id}")
            # Ignore async/debug properties (for example stream/debug 0x70).
        raise TimeoutError(f"Timeout waiting for prop {prop_id}")


def parse_protocol_version(value: bytes):
    major, off = unpack_uint(value, 0)
    minor, _ = unpack_uint(value, off)
    return f"{major}.{minor}"


def parse_ncp_version(value: bytes):
    # UTF-8 C string (zero-terminated)
    if b"\x00" in value:
        value = value.split(b"\x00", 1)[0]
    return value.decode("utf-8", errors="replace")


def parse_caps(value: bytes):
    caps = []
    off = 0
    while off < len(value):
        cap, off = unpack_uint(value, off)
        caps.append(cap)
    return caps


def main():
    parser = argparse.ArgumentParser(description="OpenThread RCP Spinel smoke test")
    parser.add_argument("--port", required=True, help="Serial port, e.g. /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=1000000, help="Baud rate (default: 1000000)")
    parser.add_argument("--timeout", type=float, default=0.2, help="Serial read timeout seconds")
    parser.add_argument("--rtscts", action="store_true", help="Enable RTS/CTS hardware flow control")
    parser.add_argument("--no-reset", action="store_true", help="Skip Spinel RESET command")
    args = parser.parse_args()

    try:
        with serial.Serial(
            port=args.port,
            baudrate=args.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=args.timeout,
            rtscts=args.rtscts,
            dsrdtr=False,
            xonxoff=False,
        ) as ser:
            ser.reset_input_buffer()
            ser.reset_output_buffer()

            tester = SpinelTester(ser)
            print(f"[INFO] Opened {args.port} @ {args.baud}, rtscts={args.rtscts}")

            if not args.no_reset:
                status = tester.cmd_reset()
                print(f"[PASS] RESET -> LAST_STATUS={status}")
                # give RCP short time to settle
                time.sleep(0.15)

            prop, value = tester.get_prop(SPINEL_PROP_PROTOCOL_VERSION)
            if prop != SPINEL_PROP_PROTOCOL_VERSION:
                raise RuntimeError(f"Expected PROP_PROTOCOL_VERSION, got {prop}")
            print(f"[PASS] PROTOCOL_VERSION: {parse_protocol_version(value)}")

            prop, value = tester.get_prop(SPINEL_PROP_NCP_VERSION)
            if prop != SPINEL_PROP_NCP_VERSION:
                raise RuntimeError(f"Expected PROP_NCP_VERSION, got {prop}")
            print(f"[PASS] NCP_VERSION: {parse_ncp_version(value)}")

            prop, value = tester.get_prop(SPINEL_PROP_CAPS)
            if prop != SPINEL_PROP_CAPS:
                raise RuntimeError(f"Expected PROP_CAPS, got {prop}")
            caps = parse_caps(value)
            print(f"[PASS] CAPS count={len(caps)}")
            print(f"[INFO] CAPS IDs: {caps}")

            print("[OK] Spinel UART/HDLC smoke test passed.")
            return 0

    except Exception as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
