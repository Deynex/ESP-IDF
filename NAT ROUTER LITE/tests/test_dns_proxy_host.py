"""Exercise the production UDP DNS proxy through a native host test executable.

The executable takes `online MAIN BACKUP`, `offline`, and `quit` on stdin.
The host-only adapter maps DNS port 53 to test port 1053. No Internet access is used.
"""

import argparse
import socket
import struct
import subprocess
import time
from pathlib import Path


def query(name, ident=0x1234):
    labels = b"".join(bytes([len(part)]) + part.encode("ascii") for part in name.split("."))
    return struct.pack("!HHHHHH", ident, 0x0100, 1, 0, 0, 0) + labels + b"\0\0\1\0\1"


def answer(request, address="203.0.113.7"):
    return (request[:2] + struct.pack("!HHHHH", 0x8180, 1, 1, 0, 0)
            + request[12:] + b"\xc0\x0c\0\1\0\1\0\0\0\x3c\0\4" + socket.inet_aton(address))


def udp(address, timeout=2.0, port=0):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((address, port))
    sock.settimeout(timeout)
    return sock


def expect_silence(sock, seconds=0.25):
    previous = sock.gettimeout()
    sock.settimeout(seconds)
    try:
        data, _ = sock.recvfrom(8192)
        raise AssertionError(f"Unexpected UDP response: {data!r}")
    except socket.timeout:
        pass
    finally:
        sock.settimeout(previous)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("--port", type=int, default=1053)
    args = parser.parse_args()
    listener = ("127.0.0.1", args.port)
    log_path = args.executable.parent / "runtime.log"
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen([str(args.executable.resolve())], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=log, text=True, bufsize=1)
        sockets = []
        try:
            assert process.stdout.readline().strip() == "READY", "Proxy did not start"

            def command(text):
                process.stdin.write(text + "\n")
                process.stdin.flush()
                assert process.stdout.readline().strip() == "OK", text
                time.sleep(0.15)

            upstream_a = udp("127.0.1.1", port=args.port)
            upstream_b = udp("127.0.2.1", port=args.port)
            client_a = udp("127.0.0.2", 3.0)
            client_b = udp("127.0.0.3", 3.0)
            sockets.extend([upstream_a, upstream_b, client_a, client_b])

            # An early DHCP client must receive a retryable failure before Wi-Fi is ready.
            request = query("offline.example")
            client_a.sendto(request, listener)
            response, _ = client_a.recvfrom(8192)
            assert response[:2] == request[:2] and response[2] & 0x80 and response[3] & 15 == 2
            assert response[12:] == request[12:]
            print("PASS: offline queries return SERVFAIL")

            command("online 127.0.1.1 0.0.0.0")
            request = query("online.example", 0x1111)
            client_a.sendto(request, listener)
            forwarded, source = upstream_a.recvfrom(8192)
            assert forwarded[2:] == request[2:], "Question or EDNS data changed"
            reply = answer(forwarded)
            upstream_a.sendto(reply, source)
            response, _ = client_a.recvfrom(8192)
            assert response == request[:2] + reply[2:]
            print("PASS: online answer preserves the DNS wire payload and original ID")

            # Both clients reuse the same transaction ID; reverse answer order deliberately.
            request_a = query("client-a.example", 0x2222)
            request_b = query("client-b.example", 0x2222)
            client_a.sendto(request_a, listener)
            client_b.sendto(request_b, listener)
            packets = [upstream_a.recvfrom(8192), upstream_a.recvfrom(8192)]
            expected = {}
            for forwarded, source in reversed(packets):
                address = "203.0.113.11" if forwarded[12:] == request_a[12:] else "203.0.113.12"
                reply = answer(forwarded, address)
                expected[forwarded[12:]] = request_a[:2] + reply[2:]
                upstream_a.sendto(reply, source)
            assert client_a.recvfrom(8192)[0] == expected[request_a[12:]]
            assert client_b.recvfrom(8192)[0] == expected[request_b[12:]]
            print("PASS: concurrent clients with the same DNS ID receive their own answers")

            command("online 127.0.2.1 0.0.0.0")
            request = query("changed.example", 0x3333)
            client_a.sendto(request, listener)
            forwarded, source = upstream_b.recvfrom(8192)
            reply = answer(forwarded, "203.0.113.21")
            upstream_b.sendto(reply, source)
            assert client_a.recvfrom(8192)[0] == request[:2] + reply[2:]
            expect_silence(upstream_a)
            print("PASS: upstream changes take effect without renewing the client DHCP lease")

            # A response with an unrecognised proxy ID must not be sent to the client.
            request = query("wrong-id.example", 0x4444)
            client_a.sendto(request, listener)
            forwarded, source = upstream_b.recvfrom(8192)
            reply = answer(forwarded, "203.0.113.31")
            wrong_id = (int.from_bytes(forwarded[:2], "big") + 1) & 0xffff
            upstream_b.sendto(wrong_id.to_bytes(2, "big") + reply[2:], source)
            expect_silence(client_a)
            upstream_b.sendto(reply, source)
            assert client_a.recvfrom(8192)[0] == request[:2] + reply[2:]
            print("PASS: unmatched response IDs are ignored")

            request = query("timeout.example", 0x5555)
            start = time.monotonic()
            client_a.sendto(request, listener)
            upstream_b.recvfrom(8192)  # Deliberately do not reply.
            response, _ = client_a.recvfrom(8192)
            elapsed = time.monotonic() - start
            assert response[:2] == request[:2] and response[3] & 15 == 2
            assert 0.5 < elapsed < 3.0, elapsed
            print("PASS: upstream timeout returns SERVFAIL within the configured bound")

            command("offline")
            client_b.sendto(query("lost.example", 0x6666), listener)
            response, _ = client_b.recvfrom(8192)
            assert response[:2] == b"\x66\x66" and response[3] & 15 == 2
            print("PASS: losing Wi-Fi returns to offline behaviour")
        finally:
            for sock in sockets:
                sock.close()
            if process.poll() is None:
                process.stdin.write("quit\n")
                process.stdin.flush()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    print("dns_proxy host integration: all tests passed")


if __name__ == "__main__":
    main()
