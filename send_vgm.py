#!/usr/bin/env python3
import gzip
import serial
import struct
import sys
import os
import time

def send_vgm(port, vgm_path):
    raw = open(vgm_path, 'rb').read()
    if vgm_path.lower().endswith('.vgz'):
        data = gzip.decompress(raw)
    else:
        data = raw
    print(f"Sending {os.path.basename(vgm_path)} ({len(data)} bytes) to {port}")

    with serial.Serial(port, timeout=10) as s:
        time.sleep(0.5)  # allow USB CDC to settle after port open
        s.reset_input_buffer()
        s.write(b'OPL3' + struct.pack('<I', len(data)))

        resp = s.readline().decode().strip()
        if resp != 'READY':
            print(f"Unexpected response: {resp!r}")
            return False

        s.write(data)
        print("File sent, waiting for playback...")

        resp = s.readline().decode().strip()
        if resp != 'OK':
            print(f"Unexpected response: {resp!r}")
            return False

        # DONE arrives only when the track finishes; remove timeout so we
        # don't bail out on long tracks.
        s.timeout = None
        resp = s.readline().decode().strip()
        if resp == 'DONE':
            print("Playback complete.")
        else:
            print(f"Unexpected response: {resp!r}")

    return True

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <port> <file.vgm|vgz>")
        print(f"  e.g. {sys.argv[0]} /dev/ttyACM0 song.vgz")
        sys.exit(1)

    if not send_vgm(sys.argv[1], sys.argv[2]):
        sys.exit(1)
