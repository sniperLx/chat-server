#!/usr/bin/env python3
"""Black-box integration tests for the poll-based chat server."""

import socket
import struct
import subprocess
import time
from pathlib import Path


LOGIN = 1
TEXT = 2
LOGIN_OK = 3
LOGIN_ERROR = 4
PORT = 8000
MAX_TEXT = 4000


def encode(message_type, payload=b""):
    """Encode one frame: network-order total length, type, then payload."""
    if isinstance(payload, str):
        payload = payload.encode()
    return struct.pack("!IB", len(payload) + 1, message_type) + payload


def recv_exact(sock, length):
    """Read exactly length bytes or raise EOFError on a truncated response."""
    data = bytearray()
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise EOFError("server closed the connection")
        data.extend(chunk)
    return bytes(data)


def recv_packet(sock):
    """Read and decode one server frame, including its type byte."""
    network_length = struct.unpack("!I", recv_exact(sock, 4))[0]
    packet = recv_exact(sock, network_length)
    return packet[0], packet[1:].decode()


def connect():
    """Open a bounded test connection so a broken server cannot hang CI."""
    sock = socket.create_connection(("127.0.0.1", PORT), timeout=2)
    sock.settimeout(2)
    return sock


def login(sock, name):
    """Send a login frame and return the server's first response."""
    sock.sendall(encode(LOGIN, name))
    return recv_packet(sock)


def expect_packet(sock, expected):
    """Assert the next server packet matches the expected type and payload."""
    actual = recv_packet(sock)
    assert actual == expected, f"expected {expected!r}, got {actual!r}"


def main():
    root = Path(__file__).resolve().parents[1]
    server = subprocess.Popen(
        [str(root / "server" / "server")],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    clients = []

    try:
        # The test owns the server process, so port 8000 must not already be in use.
        deadline = time.time() + 2
        while True:
            line = server.stdout.readline()
            if "listening on port" in line:
                break
            if server.poll() is not None or time.time() > deadline:
                raise RuntimeError("server failed to start")

        alice = connect()
        clients.append(alice)
        assert login(alice, "Alice") == (LOGIN_OK, "Alice")

        duplicate = connect()
        clients.append(duplicate)
        message_type, error = login(duplicate, "alice")
        assert message_type == LOGIN_ERROR
        assert "already in use" in error
        assert login(duplicate, "Bob") == (LOGIN_OK, "Bob")
        expect_packet(alice, (TEXT, "Bob joined the chat"))

        alice.sendall(encode(TEXT, "hello"))
        expect_packet(duplicate, (TEXT, "Alice: hello"))

        duplicate.sendall(encode(TEXT, "hi"))
        expect_packet(alice, (TEXT, "Bob: hi"))

        fragmented = encode(TEXT, "fragmented message")
        for index in range(len(fragmented)):
            alice.sendall(fragmented[index : index + 1])
        expect_packet(duplicate, (TEXT, "Alice: fragmented message"))

        alice.sendall(encode(TEXT, "first") + encode(TEXT, "second"))
        expect_packet(duplicate, (TEXT, "Alice: first"))
        expect_packet(duplicate, (TEXT, "Alice: second"))

        alice.sendall(encode(TEXT, "x" * MAX_TEXT))
        expect_packet(duplicate, (TEXT, "Alice: " + "x" * MAX_TEXT))

        anonymous = connect()
        clients.append(anonymous)
        anonymous.sendall(encode(TEXT, "not logged in"))
        message_type, error = recv_packet(anonymous)
        assert message_type == LOGIN_ERROR
        assert "Log in" in error

        duplicate.close()
        clients.remove(duplicate)
        expect_packet(alice, (TEXT, "Bob left the chat"))

        # A released name can be reused, including with different letter case.
        bob = connect()
        clients.append(bob)
        assert login(bob, "bob") == (LOGIN_OK, "bob")
        expect_packet(alice, (TEXT, "bob joined the chat"))

        oversized = connect()
        clients.append(oversized)
        oversized.sendall(struct.pack("!I", 4097))
        assert oversized.recv(1) == b""
        oversized.close()
        clients.remove(oversized)
    finally:
        for client in clients:
            client.close()
        server.terminate()
        try:
            server_output = server.communicate(timeout=2)[0]
        except subprocess.TimeoutExpired:
            server.kill()
            server_output = server.communicate()[0]

    # Forwarded chat bodies must stay out of the server console.
    assert "Alice: hello" not in server_output
    print("all protocol integration tests passed")


if __name__ == "__main__":
    main()
