#!/usr/bin/env python3
"""Simulador minimo del bridge ROS 2 de la catedra (lado PC).

Escucha tramas @LL:TTT:PAYLOAD:CC\n de la Blue Pill por un puerto serie,
las valida y responde ACK:ok a cada EVT. Sirve para probar el firmware
completo antes de la integracion en el laboratorio.

Uso:
    python3 bridge_sim.py /dev/ttyUSB0            # Linux
    python3 bridge_sim.py COM5                    # Windows
    python3 bridge_sim.py COM5 --no-ack           # escenario de falla
Requiere: pip install pyserial
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("Falta pyserial: pip install pyserial")

VALID_TYPES = {"CMD", "DAT", "EVT", "STS", "ACK", "ERR"}


def checksum(text: str) -> int:
    value = 0
    for char in text:
        value ^= ord(char)
    return value


def encode(msg_type: str, payload: str) -> bytes:
    body = f"{msg_type}:{payload}"
    ll = f"{len(body):02X}"
    cc = f"{checksum(f'{ll}:{body}'):02X}"
    return f"@{ll}:{body}:{cc}\n".encode("ascii")


def parse(line: str):
    """Devuelve (tipo, payload) o None si la trama es invalida."""
    if not line.startswith("@") or len(line) < 10:
        return None
    try:
        ll_text, body_and_cc = line[1:].split(":", 1)
        body, cc_text = body_and_cc.rsplit(":", 1)
        if len(ll_text) != 2 or len(cc_text) != 2:
            return None
        if int(ll_text, 16) != len(body):
            return None
        if int(cc_text, 16) != checksum(f"{ll_text}:{body}"):
            return None
        msg_type, payload = body.split(":", 1)
        if msg_type not in VALID_TYPES:
            return None
        return msg_type, payload
    except ValueError:
        return None


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("port", help="Puerto serie del USB-UART (ej. COM5, /dev/ttyUSB0)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--no-ack", action="store_true",
                    help="No responder ACK (probar reintentos y ERR:timeout)")
    args = ap.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.1) as ser:
        print(f"Bridge simulado en {args.port} @ {args.baud} 8N1"
              f"{' (modo NO-ACK)' if args.no_ack else ''}")
        buffer = b""
        while True:
            buffer += ser.read(256)
            while b"\n" in buffer:
                raw, buffer = buffer.split(b"\n", 1)
                line = raw.decode("ascii", errors="replace").strip()
                if not line:
                    continue
                stamp = time.strftime("%H:%M:%S")
                result = parse(line)
                if result is None:
                    print(f"[{stamp}] RX INVALIDA : {line!r}")
                    continue
                msg_type, payload = result
                print(f"[{stamp}] RX {msg_type}:{payload}")
                if msg_type == "EVT" and not args.no_ack:
                    frame = encode("ACK", "ok")
                    ser.write(frame)
                    print(f"[{stamp}] TX {frame.decode().strip()}")


if __name__ == "__main__":
    main()
