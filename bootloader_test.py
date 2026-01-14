# bootloader_test.py
# Make sure your app .bin is in the same folder or give the correct path

import socket, struct, sys, os, zlib, time

BL_MAGIC = 0xB00710AD
BL_HELLO = 1
BL_INFO  = 2
BL_BEGIN = 3
BL_DATA  = 4
BL_END   = 5
BL_ACK   = 6
BL_ERR   = 7
BL_OK    = 8

HDR_FMT = "<I B B H I I I I"   # magic,u8,u8,u16,seq,arg0,arg1,len
HDR_LEN = struct.calcsize(HDR_FMT)

def pack_msg(msg_type, seq, arg0=0, arg1=0, payload=b""):
    return struct.pack(HDR_FMT, BL_MAGIC, msg_type, 0, HDR_LEN, seq, arg0, arg1, len(payload)) + payload

def unpack_msg(data: bytes):
    if len(data) < HDR_LEN:
        raise ValueError("packet too short")
    h = struct.unpack(HDR_FMT, data[:HDR_LEN])
    payload = data[HDR_LEN:]
    return h, payload

def hdr_fields(h):
    magic, mtype, rsv0, hdr_len, seq, arg0, arg1, plen = h
    return magic, mtype, seq, arg0, arg1, plen

def explain_err(h, context=""):
    magic, mtype, seq, arg0, arg1, plen = hdr_fields(h)
    return f"{context} BL_ERR: seq={seq}, arg0={arg0}, arg1={arg1}, payload_len={plen}"

def recv_expect(sock, want_seq, allowed_types):
    data, addr = sock.recvfrom(4096)
    h, payload = unpack_msg(data)

    magic, mtype, seq, arg0, arg1, plen = hdr_fields(h)

    if magic != BL_MAGIC:
        raise RuntimeError(f"Bad magic: 0x{magic:08X}")
    if seq != want_seq:
        raise RuntimeError(f"Seq mismatch: got {seq}, expected {want_seq}")
    if mtype not in allowed_types:
        raise RuntimeError(f"Unexpected type {mtype}, hdr={h}")

    return h, payload, addr

def main():

    PC_BIND_IP = " "     # your PC IP on the direct-link NIC
    MCU_IP     = " "     # your Nucleo static IP
    MCU_PORT   = 5000
    BIN_PATH   = ""    # MUST be the app .bin linked at 0x08040000 (add your path)

    if not os.path.exists(BIN_PATH):
        print(f"ERROR: can't find {BIN_PATH} in {os.getcwd()}")
        sys.exit(1)

    img = open(BIN_PATH, "rb").read()

    # pad to 4 bytes for flash_write alignment
    if len(img) % 4 != 0:
        img += b"\xFF" * (4 - (len(img) % 4))

    img_size = len(img)
    img_crc  = zlib.crc32(img) & 0xFFFFFFFF

    print(f"BIN: {BIN_PATH}")
    print(f"Image bytes (padded): {img_size}")
    print(f"CRC32(zlib): 0x{img_crc:08X}")
    print("First 8 bytes:", img[:8].hex(), "(helps confirm vector table)")

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((PC_BIND_IP, 0))
    s.settimeout(2)

    seq = 1

    # HELLO -> INFO
    s.sendto(pack_msg(BL_HELLO, seq), (MCU_IP, MCU_PORT))
    h, payload, addr = recv_expect(s, seq, {BL_INFO, BL_ERR})
    magic, mtype, rseq, arg0, arg1, plen = hdr_fields(h)

    if mtype == BL_ERR:
        print(explain_err(h, "HELLO"))
        sys.exit(1)

    APP_BASE = arg0
    APP_MAX  = arg1
    print(f"Bootloader: APP_BASE=0x{APP_BASE:08X}, APP_MAX={APP_MAX} bytes")

    if img_size > APP_MAX:
        print("ERROR: image too large for APP_MAX")
        sys.exit(1)

    # BEGIN(size, crc) -> ACK
    seq += 1
    s.sendto(pack_msg(BL_BEGIN, seq, img_size, img_crc), (MCU_IP, MCU_PORT))
    h, payload, _ = recv_expect(s, seq, {BL_ACK, BL_ERR})
    magic, mtype, rseq, arg0, arg1, plen = hdr_fields(h)

    if mtype == BL_ERR:
        print(explain_err(h, "BEGIN"))
        sys.exit(1)

    # DATA chunks
    CHUNK = 1024  # must match your bootloader max and be multiple of 4
    offset = 0
    while offset < img_size:
        seq += 1
        chunk = img[offset:offset+CHUNK]
        s.sendto(pack_msg(BL_DATA, seq, offset, 0, chunk), (MCU_IP, MCU_PORT))
        h, payload, _ = recv_expect(s, seq, {BL_ACK, BL_ERR})
        magic, mtype, rseq, arg0, arg1, plen = hdr_fields(h)

        if mtype == BL_ERR:
            print(explain_err(h, f"DATA(off={offset})"))
            sys.exit(1)

        offset += len(chunk)

        if offset % (64*1024) == 0 or offset == img_size:
            print(f"Sent {offset}/{img_size}")

    # END -> OK
    seq += 1
    s.sendto(pack_msg(BL_END, seq), (MCU_IP, MCU_PORT))
    h, payload, _ = recv_expect(s, seq, {BL_OK, BL_ERR})
    magic, mtype, rseq, arg0, arg1, plen = hdr_fields(h)

    if mtype == BL_ERR:
        print(explain_err(h, "END"))
        print(f"  (If CRC mismatch) computed=0x{arg0:08X}, expected=0x{arg1:08X}")
        sys.exit(1)

    print(" Transfer + CRC verify passed. Bootloader should jump to app now.")
    time.sleep(2)

if __name__ == "__main__":
    main()
