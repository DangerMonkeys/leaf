#!/usr/bin/env python3
"""
play_buslog.py - Send UDP packets from a bus log to a target host.

Usage:
  python udp_replay.py <log_file> <ip_address> [--port 7431] [--encoding utf-8] [-v]

Behavior:
- For appropriate lines:
    Format: <TYPE><ms_since_start>,<rest of line>
    Example: A1234,foo,bar
  The script waits until at/after <ms_since_start> (measured from script start),
  then sends the full line (without the trailing newline) as a UDP packet.

Notes:
- Packets are sent to UDP port 7431 by default (override with --port).
- Output is sent exactly as the line text (no trailing newline).
"""

import argparse
import socket
import sys
import time
from typing import Optional, Tuple

MESSAGE_TYPES_TO_SEND = {"A", "G", "M", "P"}


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Replay a bus log to a target via UDP.")
    p.add_argument("logfile", help="Path to bus log text file.")
    p.add_argument("ip", help="Target IP address (v4 or v6) or hostname.")
    p.add_argument("--port", type=int, default=7431, help="Destination UDP port (default: 7431).")
    p.add_argument("--encoding", default="utf-8", help="Text encoding for the file and UDP payload (default: utf-8).")
    p.add_argument("-v", "--verbose", action="store_true", help="Print what is being sent and when.")
    return p.parse_args()


def get_time_to_send(line: str) -> Optional[int]:
    """
    If line is one to be sent, return the millisecond at which to send.
    Otherwise return None.
    """
    if not line:
        return None
    if line[0] not in MESSAGE_TYPES_TO_SEND:
        return None

    # Find first comma after the type letter
    comma_idx = line.find(",", 1)
    if comma_idx == -1:
        return None

    # Extract the substring between the type letter and the comma
    ms_str = line[1:comma_idx].strip()
    try:
        ms = int(ms_str)
        if ms < 0:
            # Negative timestamps: treat as immediate
            return 0
        return ms
    except ValueError:
        return None


def make_udp_socket(ip: str, port: int) -> Tuple[socket.socket, Tuple[str, int]]:
    """Create a UDP socket and a destination tuple, supporting IPv4/IPv6."""
    # Use getaddrinfo to resolve both IPv4 and IPv6. Prefer first result.
    infos = socket.getaddrinfo(ip, port, proto=socket.IPPROTO_UDP, type=socket.SOCK_DGRAM)
    if not infos:
        raise RuntimeError(f"Could not resolve target {ip}:{port}")

    family, socktype, proto, _, sockaddr = infos[0]
    sock = socket.socket(family, socktype, proto)
    return sock, sockaddr  # sockaddr is (host, port) or (host, port, flowinfo, scopeid) for IPv6


def main() -> int:
    args = parse_args()

    try:
        sock, dest = make_udp_socket(args.ip, args.port)
    except Exception as e:
        print(f"Error creating UDP socket for {args.ip}:{args.port}: {e}", file=sys.stderr)
        return 2

    # Start timing reference
    t0 = time.monotonic()

    def send_packet(packet_line: str):
        data = packet_line.encode(args.encoding, errors="replace")
        try:
            sent = sock.sendto(data, dest)
            if args.verbose:
                elapsed_ms = int((time.monotonic() - t0) * 1000)
                print(f"[{elapsed_ms:>8} ms] sent {sent} bytes -> {args.ip}:{args.port} :: {line}")
        except Exception as e:
            print(f"Error sending UDP packet: {e}", file=sys.stderr)

    try:
        send_packet(f"#Starting playback of {args.logfile}")
        send_packet("!Disconnect sensors")
        send_packet("!Reset reference time")
        with open(args.logfile, "r", encoding=args.encoding, errors="replace") as f:
            for raw_line in f:
                # Keep the full content of the line, minus the trailing newline characters
                line = raw_line.rstrip("\r\n")

                # Skip empty lines and comments
                if not line or line.startswith("#"):
                    continue

                # Determine when to send
                ms = get_time_to_send(line)
                if ms is None:
                    continue

                target_time = t0 + (ms / 1000.0)
                now = time.monotonic()
                sleep_s = target_time - now
                if sleep_s > 0:
                    # Sleep until the scheduled time; if delayed, send immediately
                    time.sleep(sleep_s)

                # Send the packet
                send_packet(line)

    except FileNotFoundError:
        print(f"Log file not found: {args.logfile}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        # Graceful exit on Ctrl+C
        if args.verbose:
            print("\nInterrupted. Exiting.")
    finally:
        try:
            send_packet("!Reconnect sensors")
        except Exception as e:
            print(f"Error reconnecting sensors: {e}")
            pass
        try:
            sock.close()
        except Exception as e:
            print(f"Error closing socket: {e}")
            pass

    return 0


if __name__ == "__main__":
    sys.exit(main())
