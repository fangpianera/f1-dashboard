import sys, time, serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
dur = float(sys.argv[2]) if len(sys.argv) > 2 else 15.0
do_reset = (len(sys.argv) > 3 and sys.argv[3] == "reset")

s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.3
s.open()

if do_reset:
    # IO0 (DTR) high, then pulse EN (RTS) to reboot into the app, not download mode
    s.dtr = False
    s.rts = True
    time.sleep(0.15)
    s.rts = False
    time.sleep(0.05)

# After the pulse, leave the control lines alone so a reset is not re-triggered
s.dtr = None
s.rts = None

t0 = time.time()
buf = b""
while time.time() - t0 < dur:
    try:
        chunk = s.read(4096)
    except Exception as e:
        print("READ_ERR", e)
        break
    if chunk:
        buf += chunk
s.close()

sys.stdout.write(buf.decode("utf-8", "replace"))
sys.stdout.write("\n----BYTES=%d----\n" % len(buf))
