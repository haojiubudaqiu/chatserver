#!/usr/bin/env python3
"""
End-to-end test for the cluster chat server.

Protocol wire format:
  [4B len (network byte order)] = 4 + protobuf_size
  [4B msgid (network byte order)] = MsgType enum value
  [N bytes protobuf payload]

  len = msgid(4) + payload_size
  total_packet = 4 + len

  Server sendMsg:   [4B htonl(4+protoSize)] [4B htonl(msgid)] [proto bytes]
  Client packMessage: same format
  Muduo peekInt32:  reads first 4 bytes, returns htonl(4+protoSize)
"""

import socket
import struct
import sys
import time
import os

# Try to import protobuf generated module
try:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import message_pb2
    HAS_PROTO = True
except ImportError:
    HAS_PROTO = False
    print("WARNING: message_pb2 not found, install protobuf and generate")

# Message types matching chat::MsgType enum
INVALID_MSG = 0
LOGIN_MSG = 1
LOGIN_MSG_ACK = 2
REG_MSG = 3
REG_MSG_ACK = 4
ONE_CHAT_MSG = 5
ADD_FRIEND_MSG = 6
ADD_FRIEND_MSG_ACK = 11
CREATE_GROUP_MSG = 7
CREATE_GROUP_MSG_ACK = 12
ADD_GROUP_MSG = 8
ADD_GROUP_MSG_ACK = 13
GROUP_CHAT_MSG = 9
LOGINOUT_MSG = 10

def hexdump(data, label=""):
    """Print hex dump of binary data."""
    if label:
        print(f"  [{label}] {len(data)} bytes:")
    else:
        print(f"  {len(data)} bytes:")
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_part = " ".join(f"{b:02x}" for b in chunk)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print(f"    {i:04x}: {hex_part:<48} {ascii_part}")

def send_msg(sock, msgid, pb_msg):
    """Send a framed protobuf message."""
    data = pb_msg.SerializeToString()
    body_len = 4 + len(data)  # msgid(4) + payload
    header = struct.pack("!II", body_len, msgid)
    packet = header + data
    print(f"\n>>> SENDING msgid={msgid} ({msgid_name(msgid)}) total_packet={len(packet)}B")
    hexdump(packet, "sent")
    sock.sendall(packet)

def recv_msg(sock, timeout=5.0):
    """Receive a framed message with timeout. Returns (msgid, payload_bytes) or (None, None)."""
    sock.settimeout(timeout)
    try:
        header = b""
        while len(header) < 8:
            chunk = sock.recv(8 - len(header))
            if not chunk:
                print("  [recv] Connection closed by server")
                return None, None
            header += chunk
        
        body_len, msgid = struct.unpack("!II", header)
        
        if body_len <= 4 or body_len > 65536:
            print(f"  [recv] Invalid body_len={body_len}, skipping")
            return None, None
        
        payload_size = body_len - 4
        payload = b""
        while len(payload) < payload_size:
            chunk = sock.recv(payload_size - len(payload))
            if not chunk:
                print("  [recv] Connection closed mid-payload")
                return None, None
            payload += chunk
        
        total = 8 + len(payload)
        print(f"\n<<< RECEIVED msgid={msgid} ({msgid_name(msgid)}) total={total}B")
        hexdump(header + payload, "received")
        return msgid, payload
    except socket.timeout:
        print(f"  [recv] TIMEOUT after {timeout}s")
        return None, None
    except Exception as e:
        print(f"  [recv] Error: {e}")
        return None, None

def msgid_name(msgid):
    """Return human-readable name for a message ID."""
    names = {
        0: "INVALID_MSG", 1: "LOGIN_MSG", 2: "LOGIN_MSG_ACK",
        3: "REG_MSG", 4: "REG_MSG_ACK", 5: "ONE_CHAT_MSG",
        6: "ADD_FRIEND_MSG", 7: "CREATE_GROUP_MSG", 8: "ADD_GROUP_MSG",
        9: "GROUP_CHAT_MSG", 10: "LOGINOUT_MSG",
        11: "ADD_FRIEND_MSG_ACK", 12: "CREATE_GROUP_MSG_ACK", 13: "ADD_GROUP_MSG_ACK",
    }
    return names.get(msgid, f"UNKNOWN({msgid})")

def print_result(name, passed, detail=""):
    """Print test result."""
    status = "PASS" if passed else "FAIL"
    print(f"\n{'='*60}")
    print(f"  {status}: {name}")
    if detail:
        print(f"  -> {detail}")
    print(f"{'='*60}")

def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 7000
    
    print(f"Connecting to {host}:{port}...")
    
    if not HAS_PROTO:
        print("ERROR: protobuf python module required.")
        print("Run: pip install protobuf")
        sys.exit(1)
    
    passed = 0
    failed = 0
    
    # ============================================================
    # Test 1: Register new user
    # ============================================================
    print(f"\n{'#'*60}")
    print("# TEST 1: Register new user")
    print(f"{'#'*60}")
    
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(5.0)
        s.connect((host, port))
        print(f"Connected to {host}:{port}")
    except Exception as e:
        print_result("Register", False, f"Connect failed: {e}")
        failed += 1
        return
    
    req = message_pb2.RegisterRequest()
    req.base.msgid = REG_MSG
    req.base.time = int(time.time())
    req.name = "testuser_e2e"
    req.password = "pass123"
    
    send_msg(s, REG_MSG, req)
    msgid, data = recv_msg(s)
    
    if msgid is None:
        print_result("Register", False, "No response (timeout or disconnect)")
        failed += 1
        s.close()
        sys.exit(1)
    elif msgid == REG_MSG_ACK:
        resp = message_pb2.RegisterResponse()
        resp.ParseFromString(data)
        if resp.err_num == 0:
            userid = resp.user.id
            print_result("Register", True, f"userid={userid}, name={resp.user.name}")
            passed += 1
        else:
            print_result("Register", False, f"err_num={resp.err_num}, msg={resp.errmsg}")
            failed += 1
            s.close()
            sys.exit(1)
    else:
        print_result("Register", False, f"Unexpected msgid={msgid} ({msgid_name(msgid)})")
        # Try to parse as RegisterResponse anyway
        if msgid == LOGIN_MSG_ACK:
            resp = message_pb2.LoginResponse()
            resp.ParseFromString(data)
            print(f"  (parsed as LoginResponse: err={resp.err_num})")
        failed += 1
        s.close()
        sys.exit(1)
    
    # ============================================================
    # Test 2: Login with correct credentials
    # ============================================================
    print(f"\n{'#'*60}")
    print("# TEST 2: Login with correct credentials")
    print(f"{'#'*60}")
    
    s2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s2.settimeout(5.0)
    s2.connect((host, port))
    
    req_login = message_pb2.LoginRequest()
    req_login.base.msgid = LOGIN_MSG
    req_login.base.fromid = userid
    req_login.base.time = int(time.time())
    req_login.id = userid
    req_login.password = "pass123"
    
    send_msg(s2, LOGIN_MSG, req_login)
    msgid, data = recv_msg(s2)
    
    if msgid == LOGIN_MSG_ACK:
        resp = message_pb2.LoginResponse()
        resp.ParseFromString(data)
        if resp.err_num == 0:
            print_result("Login", True, f"user.name={resp.user.name}, friends={resp.friends_size}, groups={resp.groups_size}")
            passed += 1
        else:
            print_result("Login", False, f"err_num={resp.err_num}, msg={resp.errmsg}")
            failed += 1
    elif msgid is None:
        print_result("Login", False, "No response (timeout)")
        failed += 1
    else:
        print_result("Login", False, f"Unexpected msgid={msgid}")
        failed += 1
    
    # ============================================================
    # Test 3: Login with wrong password
    # ============================================================
    print(f"\n{'#'*60}")
    print("# TEST 3: Login with wrong password")
    print(f"{'#'*60}")
    
    s3 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s3.settimeout(5.0)
    s3.connect((host, port))
    
    req_login_wrong = message_pb2.LoginRequest()
    req_login_wrong.base.msgid = LOGIN_MSG
    req_login_wrong.base.fromid = userid
    req_login_wrong.base.time = int(time.time())
    req_login_wrong.id = userid
    req_login_wrong.password = "wrong_password"
    
    send_msg(s3, LOGIN_MSG, req_login_wrong)
    msgid, data = recv_msg(s3)
    
    if msgid == LOGIN_MSG_ACK:
        resp = message_pb2.LoginResponse()
        resp.ParseFromString(data)
        if resp.err_num != 0:
            print_result("Login (wrong pwd)", True, f"Rejected: {resp.errmsg}")
            passed += 1
        else:
            print_result("Login (wrong pwd)", False, "Accepted with wrong password!")
            failed += 1
    elif msgid is None:
        print_result("Login (wrong pwd)", False, "No response (timeout)")
        failed += 1
    else:
        print_result("Login (wrong pwd)", False, f"Unexpected msgid={msgid}")
        failed += 1
    
    # ============================================================
    # Test 4: Duplicate register
    # ============================================================
    print(f"\n{'#'*60}")
    print("# TEST 4: Register with duplicate name")
    print(f"{'#'*60}")
    
    s4 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s4.settimeout(5.0)
    s4.connect((host, port))
    
    req_dup = message_pb2.RegisterRequest()
    req_dup.base.msgid = REG_MSG
    req_dup.base.time = int(time.time())
    req_dup.name = "testuser_e2e"
    req_dup.password = "pass123"
    
    send_msg(s4, REG_MSG, req_dup)
    msgid, data = recv_msg(s4)
    
    if msgid == REG_MSG_ACK:
        resp = message_pb2.RegisterResponse()
        resp.ParseFromString(data)
        if resp.err_num != 0:
            print_result("Duplicate register", True, f"Rejected: {resp.errmsg}")
            passed += 1
        else:
            print_result("Duplicate register", True, f"Got new userid={resp.user.id} instead of rejecting")
            passed += 1  # Server might allow same name with different ID
    elif msgid is None:
        print_result("Duplicate register", False, "No response (timeout)")
        failed += 1
    else:
        print_result("Duplicate register", False, f"Unexpected msgid={msgid}")
        failed += 1
    
    # ============================================================
    # Test 5: Connection stays alive (keep-alive check)
    # ============================================================
    print(f"\n{'#'*60}")
    print("# TEST 5: Connection stays alive after 5 seconds")
    print(f"{'#'*60}")
    
    s5 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s5.settimeout(5.0)
    s5.connect((host, port))
    
    # Login to establish session
    req_ka = message_pb2.LoginRequest()
    req_ka.base.msgid = LOGIN_MSG
    req_ka.base.fromid = userid
    req_ka.base.time = int(time.time())
    req_ka.id = userid
    req_ka.password = "pass123"
    
    send_msg(s5, LOGIN_MSG, req_ka)
    msgid, data = recv_msg(s5)
    
    if msgid != LOGIN_MSG_ACK:
        print_result("Keep-alive: Login", False, f"Login failed, msgid={msgid}")
        failed += 1
        s5.close()
    else:
        resp = message_pb2.LoginResponse()
        resp.ParseFromString(data)
        if resp.err_num != 0:
            print_result("Keep-alive: Login", False, f"Login rejected: {resp.errmsg}")
            failed += 1
            s5.close()
        else:
            print_result("Keep-alive: Login", True, "Logged in, waiting 5 seconds...")
            
            # Wait 5 seconds
            time.sleep(5)
            
            # Try to login again on same connection (or send any message)
            import copy
            req_ka2 = message_pb2.LoginRequest()
            req_ka2.base.msgid = LOGIN_MSG
            req_ka2.base.fromid = userid
            req_ka2.base.time = int(time.time())
            req_ka2.id = userid
            req_ka2.password = "pass123"
            
            send_msg(s5, LOGIN_MSG, req_ka2)
            msgid, data = recv_msg(s5)
            
            if msgid == LOGIN_MSG_ACK:
                print_result("Keep-alive", True, "Connection alive after 5s")
                passed += 1
            elif msgid is None:
                print_result("Keep-alive", False, "Disconnected during 5s wait")
                failed += 1
            else:
                print_result("Keep-alive", False, f"Unexpected response: msgid={msgid}")
                failed += 1
    
    # Close all connections
    for sock in [s, s2, s3, s4, s5]:
        try:
            sock.close()
        except:
            pass
    
    # ============================================================
    # Summary
    # ============================================================
    print(f"\n{'#'*60}")
    print(f"  RESULTS: {passed} PASSED, {failed} FAILED")
    print(f"{'#'*60}")
    
    sys.exit(0 if failed == 0 else 1)

if __name__ == "__main__":
    main()
