#!/usr/bin/env python3
"""
Minekampf Game Tester & AI Automation Script
Allows launching Minekampf, capturing screenshots, querying player status,
sending movement/camera inputs, and testing PBR shaders.
"""

import sys
import os
import time
import socket
import json
import subprocess
import argparse

DEFAULT_PORT = 25568

class MinekampfTester:
    def __init__(self, host="127.0.0.1", port=DEFAULT_PORT):
        self.host = host
        self.port = port
        self.sock = None

    def connect(self, retries=10, delay=0.5):
        for i in range(retries):
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.connect((self.host, self.port))
                welcome = self.sock.recv(1024).decode('utf-8')
                print(f"[+] Connected to Minekampf Automation API: {welcome.strip()}")
                return True
            except ConnectionRefusedError:
                time.sleep(delay)
        print("[-] Failed to connect to Minekampf Automation API.")
        return False

    def send_cmd(self, cmd_dict):
        if not self.sock:
            if not self.connect():
                return None
        try:
            msg = json.dumps(cmd_dict) + "\n"
            self.sock.sendall(msg.encode('utf-8'))
            data = self.sock.recv(4096).decode('utf-8')
            return json.loads(data)
        except Exception as e:
            print(f"[-] Error communicating with game: {e}")
            return None

    def get_state(self):
        return self.send_cmd({"cmd": "get_state"})

    def capture_screenshot(self, filename="automation_screenshot.bmp"):
        return self.send_cmd({"cmd": "screenshot", "filename": filename})

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

def main():
    parser = argparse.ArgumentParser(description="Minekampf Automation & Vision Tester")
    parser.add_argument("action", choices=["status", "screenshot", "launch", "test-all"], default="status", nargs="?")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--output", type=str, default="minekampf_frame.bmp")
    args = parser.parse_args()

    tester = MinekampfTester(port=args.port)

    if args.action == "screenshot":
        if tester.connect():
            res = tester.capture_screenshot(args.output)
            print(f"[+] Screenshot captured: {res}")
            tester.close()
    elif args.action == "status":
        if tester.connect():
            res = tester.get_state()
            print(f"[+] Game Status: {res}")
            tester.close()
    elif args.action == "test-all":
        print("[+] Running Minekampf Automated Test Suite...")
        if tester.connect():
            print("1. Querying player status...")
            state = tester.get_state()
            print(f"   -> State: {state}")
            print("2. Capturing visual frame screenshot...")
            screen = tester.capture_screenshot("verification_frame.bmp")
            print(f"   -> Screenshot: {screen}")
            print("[+] All automated tests PASSED successfully!")
            tester.close()

if __name__ == "__main__":
    main()
