#!/usr/bin/env python3
#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

import argparse
import asyncio
import sys

try:
    from bleak import BleakClient, BleakScanner
    from bleak.exc import BleakError
except ImportError:
    print("Missing dependency: bleak")
    print("Install it with: python3 -m pip install bleak")
    sys.exit(1)


DEFAULT_DEVICE_NAME = "Nordic_Status"
NSMS_SERVICE_UUID = "57a70000-9350-11ed-a1eb-0242ac120002"
NSMS_STATUS_UUID = "57a70001-9350-11ed-a1eb-0242ac120002"
CUD_UUID = "00002901-0000-1000-8000-00805f9b34fb"


def decode_value(value: bytes) -> str:
    return value.rstrip(b"\x00").decode("utf-8", errors="replace")


async def find_device(args):
    if args.address:
        return args.address

    print(f"Scanning for {args.name!r}...")

    def match_device(device, adv):
        names = {device.name, adv.local_name}
        has_name = args.name in names
        has_service = NSMS_SERVICE_UUID in [uuid.lower() for uuid in adv.service_uuids]
        return has_name or has_service

    device = await BleakScanner.find_device_by_filter(match_device, timeout=args.timeout)
    if device is None:
        raise RuntimeError(f"Device {args.name!r} not found")

    print(f"Found {device.name or '<unknown>'}: {device.address}")
    return device


async def get_services(client: BleakClient):
    try:
        return client.services
    except BleakError:
        return await client.get_services()


async def read_cud(client: BleakClient, characteristic) -> str:
    for descriptor in characteristic.descriptors:
        if descriptor.uuid.lower() == CUD_UUID:
            try:
                return decode_value(await client.read_gatt_descriptor(descriptor.handle))
            except BleakError as err:
                return f"<CUD read failed: {err}>"

    return "<unnamed>"


async def read_statuses(client: BleakClient):
    services = await get_services(client)
    statuses = []

    for service in services:
        if service.uuid.lower() != NSMS_SERVICE_UUID:
            continue

        for characteristic in service.characteristics:
            if characteristic.uuid.lower() != NSMS_STATUS_UUID:
                continue

            name = await read_cud(client, characteristic)
            try:
                value = decode_value(await client.read_gatt_char(characteristic))
            except BleakError as err:
                value = f"<read failed: {err}>"
            statuses.append((name, value, characteristic))
            print(f"{name}: {value}")

    if not statuses:
        raise RuntimeError("No Nordic Status Message characteristics found")

    return statuses


async def watch_statuses(client: BleakClient, statuses):
    def make_callback(name):
        def callback(_, data):
            print(f"{name}: {decode_value(data)}")

        return callback

    for name, _, characteristic in statuses:
        try:
            await client.start_notify(characteristic, make_callback(name))
        except BleakError as err:
            print(f"{name}: cannot enable notifications ({err})")

    print("Watching notifications. Press Ctrl+C to stop.")
    await asyncio.Event().wait()


async def main_async(args):
    device = await find_device(args)

    async with BleakClient(device) as client:
        if not client.is_connected:
            raise RuntimeError("Failed to connect")

        print("Connected")
        statuses = await read_statuses(client)

        if args.watch:
            await watch_statuses(client, statuses)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Read Nordic Status Message values from the peripheral_status sample."
    )
    parser.add_argument(
        "--name",
        default=DEFAULT_DEVICE_NAME,
        help=f"Bluetooth advertising name to scan for, default: {DEFAULT_DEVICE_NAME}",
    )
    parser.add_argument(
        "--address",
        help="Connect to a specific BLE address/identifier instead of scanning by name.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=10.0,
        help="Scan timeout in seconds, default: 10.",
    )
    parser.add_argument(
        "--watch",
        action="store_true",
        help="Keep the connection open and print status notifications.",
    )
    return parser.parse_args()


def main():
    try:
        asyncio.run(main_async(parse_args()))
    except KeyboardInterrupt:
        pass
    except Exception as err:
        print(f"Error: {err}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
