#!/usr/bin/env python3
"""Local RFC 6455 fixture server; never connects to an external endpoint."""

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
    assert first & 0x80, "fixture sends only complete WebSocket frames"
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


def main():
    executable = sys.argv[1]
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    port = server.getsockname()[1]
    result = []
    error = []

    def serve():
        try:
            for session in range(2):
                conn, _ = server.accept()
                with conn:
                    conn.settimeout(5)
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
                                  f"Sec-WebSocket-Accept: {accept}\r\n\r\n").encode("ascii"))

                    commands = []
                    while len(commands) < 2:
                        opcode, payload = read_frame(conn)
                        assert opcode == 1
                        commands.append(payload.decode("ascii"))
                    assert commands == ["iq_samplerate:48000;", "iq_start:0;"], commands

                    center = 14074000 if session == 0 else 14075000
                    first = 0.25 if session == 0 else -0.25
                    second = -0.5 if session == 0 else 0.5
                    third = 1.0 if session == 0 else 0.75
                    fourth = 0.125 if session == 0 else -0.125
                    send_frame(conn, 1, f"ready;dds:0,{center};iq_samplerate:48000;iq_start:0;".encode("ascii"))
                    header = struct.pack("<16I", 0, 48000, 3, 0, 0, 4, 0, 2, *([0] * 8))
                    samples = struct.pack("<4f", first, second, third, fourth)
                    send_frame(conn, 2, header + samples)

                    if session == 0:
                        time.sleep(0.2)
                        conn.shutdown(socket.SHUT_WR)
                    else:
                        opcode, payload = read_frame(conn)
                        assert opcode == 1 and payload == b"iq_stop:0;", (opcode, payload)
                        result.append(commands + [payload.decode("ascii")])
        except Exception as exc:  # surface thread failures in the parent process
            error.append(exc)
        finally:
            server.close()

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    completed = subprocess.run([executable, str(port)], check=False)
    thread.join(timeout=7)
    if thread.is_alive():
        raise RuntimeError("fake TCI server did not finish")
    if error:
        raise error[0]
    if completed.returncode != 0:
        raise RuntimeError(f"C++ client test exited with {completed.returncode}")
    if not result:
        raise RuntimeError("fake server did not observe the stop command after reconnect")
    print("synthetic Aether TCI fake server passed")


if __name__ == "__main__":
    main()
