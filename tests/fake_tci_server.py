#!/usr/bin/env python3
"""Loopback RFC 6455 fixture server; never connects to an external endpoint."""

import base64
import hashlib
import socket
import struct
import subprocess
import sys
import threading
import time


def recv_exact(sock, count):
    data = bytearray()
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise EOFError("client closed the fake TCI connection")
        data.extend(chunk)
    return bytes(data)


def read_frame(sock):
    first, second = recv_exact(sock, 2)
    if not first & 0x80:
        raise AssertionError("fixture sends only complete WebSocket frames")
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", recv_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", recv_exact(sock, 8))[0]
    mask = recv_exact(sock, 4) if second & 0x80 else None
    payload = recv_exact(sock, length)
    if mask:
        payload = bytes(value ^ mask[i % 4] for i, value in enumerate(payload))
    return first & 0x0F, payload


def send_frame(sock, opcode, payload):
    length = len(payload)
    if length < 126:
        head = bytes((0x80 | opcode, length))
    elif length <= 0xFFFF:
        head = bytes((0x80 | opcode, 126)) + struct.pack("!H", length)
    else:
        head = bytes((0x80 | opcode, 127)) + struct.pack("!Q", length)
    sock.sendall(head + payload)


def send_iq(sock, rate, samples):
    scalars = [component for sample in samples for component in sample]
    header = struct.pack("<16I", 0, rate, 3, 0, 0, len(scalars), 0, 2, *([0] * 8))
    send_frame(sock, 2, header + struct.pack("<%df" % len(scalars), *scalars))


def handshake(conn):
    request = bytearray()
    while not request.endswith(b"\r\n\r\n"):
        request.extend(recv_exact(conn, 1))
    request_text = request.decode("ascii")
    key = next(line.split(":", 1)[1].strip() for line in request_text.split("\r\n")
               if line.lower().startswith("sec-websocket-key:"))
    accept = base64.b64encode(hashlib.sha1(
        (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")).digest()).decode("ascii")
    conn.sendall(("HTTP/1.1 101 Switching Protocols\r\n"
                  "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                  "Sec-WebSocket-Accept: %s\r\n\r\n" % accept).encode("ascii"))


def read_start_commands(conn, requested_rate):
    commands = []
    while len(commands) < 2:
        opcode, payload = read_frame(conn)
        if opcode != 1:
            raise AssertionError("expected a receive-only text command")
        commands.append(payload.decode("ascii"))
    expected = ["iq_samplerate:%d;" % requested_rate, "iq_start:0;"]
    if commands != expected:
        raise AssertionError("unexpected command sequence: %r" % commands)
    return commands


def run_scenario(executable, requested_rate, changed_rate):
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(2)
    port = server.getsockname()[1]
    error = []
    stopped = []

    def serve():
        try:
            for session in range(2):
                conn, _ = server.accept()
                with conn:
                    conn.settimeout(5)
                    handshake(conn)
                    commands = read_start_commands(conn, requested_rate)
                    if session == 0:
                        send_frame(conn, 1, ("ready;dds:0,14074000;iq_samplerate:%d;iq_start:0;" % requested_rate).encode("ascii"))
                        send_iq(conn, requested_rate, [(-0.75, 0.75), (0.25, -0.5)])
                        time.sleep(0.1)  # let the test observe a non-empty initial epoch
                        send_frame(conn, 1, ("dds:0,14075000;iq_samplerate:%d;" % changed_rate).encode("ascii"))
                        send_iq(conn, changed_rate, [(0.75, -0.25), (0.5, 0.125)])
                        time.sleep(0.4)  # leave the new epoch short for the all-or-nothing read check
                        send_iq(conn, changed_rate, [(-0.25, 0.75)])
                        send_frame(conn, 9, b"still-here")
                        opcode, payload = read_frame(conn)
                        if opcode != 10 or payload != b"still-here":
                            raise AssertionError("client did not pong the fake server ping")
                        time.sleep(0.2)
                    else:
                        send_frame(conn, 1, ("ready;dds:0,14076000;iq_samplerate:%d;iq_start:0;" % requested_rate).encode("ascii"))
                        send_iq(conn, requested_rate, [(-0.5, 0.5), (0.75, -0.75), (0.125, -0.125)])
                        while True:
                            opcode, payload = read_frame(conn)
                            if opcode == 1 and payload == b"iq_stop:0;":
                                stopped.append(commands + [payload.decode("ascii")])
                                break
                            if opcode != 10:
                                raise AssertionError("unexpected client frame: %r %r" % (opcode, payload))
        except Exception as exc:  # propagate fixture thread failures to the parent process
            error.append(exc)
        finally:
            server.close()

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    completed = subprocess.run([executable, str(port), str(requested_rate), str(changed_rate)], check=False)
    thread.join(timeout=8)
    if thread.is_alive():
        raise RuntimeError("fake TCI server did not finish")
    if error:
        raise error[0]
    if completed.returncode != 0:
        raise RuntimeError("C++ client test exited with %d at %d -> %d kS/s" %
                           (completed.returncode, requested_rate // 1000, changed_rate // 1000))
    if not stopped:
        raise RuntimeError("fake server did not observe iq_stop after reconnect")


def main():
    executable = sys.argv[1]
    scenarios = [(24000, 48000), (48000, 96000), (96000, 24000)]
    for requested_rate, changed_rate in scenarios:
        run_scenario(executable, requested_rate, changed_rate)
    print("synthetic Aether TCI lifecycle, metadata epoch, and 24/48/96 kS/s checks passed")


if __name__ == "__main__":
    main()
