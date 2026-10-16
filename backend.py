#!/usr/bin/python3
"""Local JSON-lines service. No network, Wine, root, or HyperHeadset dependency."""
import argparse
import ctypes
import ctypes.util
import errno
import json
import os
import platform
from pathlib import Path
import select
import selectors
import subprocess
import sys
import tempfile
import time

from protocol import State, packet, setting, QUERIES, VENDOR, PRODUCTS

BASE = Path(__file__).resolve().parent
CONFIG = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "hyper-linux"
DATA = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")) / "hyper-linux"


def emit(kind, **data):
    print(json.dumps({"type": kind, **data}, ensure_ascii=False), flush=True)


def system_environment():
    """Do not inject bundled Qt/Python libraries into host audio programs."""
    env = dict(os.environ)
    original = env.get("HYPER_LINUX_HOST_LD_LIBRARY_PATH")
    if original is None and getattr(sys, "frozen", False):
        original = env.get("LD_LIBRARY_PATH_ORIG", "")
    if original is not None:
        if original:
            env["LD_LIBRARY_PATH"] = original
        else:
            env.pop("LD_LIBRARY_PATH", None)
    env["LC_ALL"] = "C"
    return env


def run(args, timeout=4):
    p = subprocess.run(args, capture_output=True, text=True, timeout=timeout, env=system_environment())
    if p.returncode:
        raise RuntimeError(p.stderr.strip() or f"{args[0]} failed ({p.returncode})")
    return p.stdout


class Trace:
    def __init__(self):
        DATA.mkdir(parents=True, exist_ok=True)
        os.chmod(DATA, 0o700)
        self.path = DATA / "usb.jsonl"
        if self.path.exists() and self.path.stat().st_size > 2_000_000:
            self.path.replace(DATA / "usb.previous.jsonl")

    def add(self, direction, **data):
        with self.path.open("a") as f:
            f.write(json.dumps({"time": time.time(), "direction": direction, **data}) + "\n")


def discover():
    devices = []
    for path in sorted(Path("/sys/class/hidraw").glob("hidraw*")):
        try:
            props = dict(line.split("=", 1) for line in (path / "device/uevent").read_text().splitlines() if "=" in line)
            _, vid, pid = props.get("HID_ID", "0:0:0").split(":")
            if int(vid, 16) != VENDOR or int(pid, 16) not in PRODUCTS:
                continue
            descriptor = (path / "device/report_descriptor").read_bytes()
            # Vendor collection FF13, report 66, not the media keys or mic HID collection.
            if b"\x06\x13\xff" not in descriptor or b"\x85\x66" not in descriptor:
                continue
            usb = (path / "device").resolve()
            while usb != usb.parent and not (usb / "idVendor").exists():
                usb = usb.parent
            devices.append({"path": "/dev/" + path.name, "name": props.get("HID_NAME"),
                            "vendor": int(vid, 16), "product": int(pid, 16),
                            "descriptor": descriptor.hex(), "sysfs": str(usb),
                            "bus": int((usb / "busnum").read_text()),
                            "address": int((usb / "devnum").read_text())})
        except (OSError, ValueError):
            continue
    return devices


class USBControl:
    """Endpoint-zero SET_REPORT fallback. Never detaches or claims the audio driver."""
    def __init__(self, info):
        name = ctypes.util.find_library("usb-1.0") or "libusb-1.0.so.0"
        try:
            self.lib = ctypes.CDLL(name)
        except OSError as exc:
            raise RuntimeError("libusb-1.0 was not found") from exc
        lib = self.lib
        lib.libusb_init.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
        lib.libusb_exit.argtypes = [ctypes.c_void_p]
        lib.libusb_get_device_list.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))]
        lib.libusb_get_device_list.restype = ctypes.c_ssize_t
        lib.libusb_free_device_list.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_int]
        lib.libusb_get_bus_number.argtypes = [ctypes.c_void_p]
        lib.libusb_get_device_address.argtypes = [ctypes.c_void_p]
        lib.libusb_open.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
        lib.libusb_close.argtypes = [ctypes.c_void_p]
        lib.libusb_control_transfer.argtypes = [ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint8,
                                               ctypes.c_uint16, ctypes.c_uint16, ctypes.c_void_p,
                                               ctypes.c_uint16, ctypes.c_uint]
        self.ctx = ctypes.c_void_p()
        self.handle = ctypes.c_void_p()
        if lib.libusb_init(ctypes.byref(self.ctx)) < 0:
            raise RuntimeError("libusb: USB access is unavailable in this session")
        devs = ctypes.POINTER(ctypes.c_void_p)()
        count = lib.libusb_get_device_list(self.ctx, ctypes.byref(devs))
        try:
            for i in range(max(0, count)):
                dev = devs[i]
                if lib.libusb_get_bus_number(dev) == info["bus"] and lib.libusb_get_device_address(dev) == info["address"]:
                    rc = lib.libusb_open(dev, ctypes.byref(self.handle))
                    if rc < 0:
                        raise RuntimeError(f"libusb_open: error {rc}")
                    break
        finally:
            if count >= 0:
                lib.libusb_free_device_list(devs, 1)
        if not self.handle:
            self.close()
            raise RuntimeError("Dongle is not accessible through libusb")

    def write(self, data):
        buf = ctypes.create_string_buffer(data)
        # HID SET_REPORT, Output Report ID 0x66, interface 3.
        rc = self.lib.libusb_control_transfer(self.handle, 0x21, 9, 0x0266, 3, buf, len(data), 250)
        if rc != len(data):
            raise RuntimeError(f"SET_REPORT returned {rc}; expected {len(data)}")

    def close(self):
        if self.handle:
            self.lib.libusb_close(self.handle)
            self.handle = None
        if self.ctx:
            self.lib.libusb_exit(self.ctx)
            self.ctx = None


class Headset:
    def __init__(self, trace):
        self.trace, self.state = trace, State()
        self.fd = None
        self.control = None
        self.mode = None
        self.size = 64
        self.info = None
        self.last_rx = 0

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None
        if self.control:
            self.control.close()
            self.control = None
        self.mode = None
        self.state.reset()

    def receive(self, wait=0):
        packets = []
        if self.fd is None:
            return packets
        ready, _, _ = select.select([self.fd], [], [], wait)
        while ready:
            try:
                data = os.read(self.fd, 4096)
            except BlockingIOError:
                break
            if not data:
                raise OSError(errno.ENODEV, "Dongle disconnected")
            self.trace.add("rx", hex=data.hex())
            if self.state.update(data):
                self.last_rx = time.monotonic()
                packets.append(data)
            ready, _, _ = select.select([self.fd], [], [], 0)
        return packets

    def send(self, command, value=0):
        data = packet(command, value, self.size)
        self.trace.add("tx", transport=self.mode, hex=data.hex())
        if self.mode == "usb-control":
            self.control.write(data)
        else:
            n = os.write(self.fd, data)
            if n != len(data):
                raise RuntimeError(f"Incomplete write: {n}/{len(data)}")

    def query(self, code, timeout=0.3):
        self.send(code)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for data in self.receive(min(0.025, max(0, end - time.monotonic()))):
                aliases = {0x82: 0x0B, 0x86: 0x0A, 0x84: 0x09, 0x89: 0x0D, 0x8A: 0x0C}
                if data[1] in (code, aliases.get(code)):
                    return data
        return None

    def connect(self):
        self.close()
        candidates = discover()
        if not candidates:
            raise RuntimeError("Cloud III Wireless was not found. Connect the USB dongle.")
        errors = []
        for info in candidates:
            self.info = info
            try:
                self.fd = os.open(info["path"], os.O_RDWR | os.O_NONBLOCK | os.O_CLOEXEC)
            except OSError as exc:
                errors.append(f"{info['path']}: {exc.strerror}")
                continue
            for mode in ("hidraw", "usb-control"):
                if mode == "usb-control":
                    try:
                        self.control = USBControl(info)
                    except Exception as exc:
                        errors.append(str(exc))
                        continue
                self.mode = mode
                for size in (64, 62):
                    self.size = size
                    try:
                        # Probe with a read-only connection query. No blind setting writes.
                        self.receive()
                        if self.query(0x82, 0.35):
                            return self.snapshot()
                    except (OSError, RuntimeError) as exc:
                        errors.append(f"{mode}/{size}: {exc}")
            self.close()
        self.close()
        raise RuntimeError("Could not confirm communication with the dongle. " + "; ".join(errors[-6:]) +
                           ". Close HyperHeadset/NGENUITY and check Diagnostics.")

    def snapshot(self):
        return {**self.state.snapshot(), "device": self.info, "transport": self.mode,
                "packet_size": self.size, "usb_ready": self.fd is not None and self.mode is not None}

    def refresh(self):
        if not self.mode:
            self.connect()
        for code in QUERIES:
            if self.query(code) is None:
                # Do not keep showing a field indefinitely after a timed-out query.
                field = {0x82: "connected", 0x86: "mute", 0x84: "sidetone", 0x85: "sleep",
                         0x89: "battery", 0x8A: "charging", 0x8F: "color"}[code]
                self.state.values[field] = None
                self.state.seen.pop(field, None)
        return self.snapshot()

    def set(self, name, value):
        command, arg, query = setting(name, value)
        if not self.mode:
            self.connect()
        if self.state.values["connected"] is not True:
            raise RuntimeError("Turn on the headset before changing its USB settings.")
        self.receive()
        self.send(command, arg)
        # Read-back is separate from a write succeeding. Never fake success in the UI.
        self.state.values[name] = None
        for _ in range(2):
            reply = self.query(query, 0.2)
            if reply is not None and self.state.values[name] == value:
                return {"confirmed": True, **self.snapshot()}
        # On this dongle the immediate GET can return the old value. Real
        # captures show the committed setting arriving as a notification
        # roughly 0.6–0.8 seconds after the SET. Wait for that event rather
        # than reporting a false failure after a fast, stale read-back.
        end = time.monotonic() + 1.25
        while time.monotonic() < end:
            self.receive(min(.025, end - time.monotonic()))
            if self.state.values[name] == value:
                return {"confirmed": True, **self.snapshot()}
        return {"confirmed": False, **self.snapshot()}


def audio_devices():
    def pick(kind):
        rows = json.loads(run(["pactl", "-f", "json", "list", kind]))
        result = []
        for row in rows:
            name = row["name"]
            label = (name + " " + row.get("description", "")).lower().replace("_", " ")
            props = row.get("properties", {})
            model_matches = "hyperx" in label and "cloud iii wireless" in label
            pid_matches = str(props.get("device.product.id", "")).lower() in ("0x05b7", "0x0c9d", "1463", "3229")
            if not (model_matches or pid_matches) or name.endswith(".monitor") or name.startswith("hyper_linux"):
                continue
            volumes = [float(v.get("value_percent", "0%").rstrip("%")) for v in row.get("volume", {}).values()]
            description = row.get("description")
            if not description or description == "(null)":
                description = "HyperX Cloud III Wireless · " + ("stereo output" if kind == "sinks" else "microphone")
            result.append({"name": name, "description": description,
                           "mute": row.get("mute"), "volume": round(sum(volumes) / max(1, len(volumes)))})
        return result
    return {"sinks": pick("sinks"), "sources": pick("sources")}


def eq_config(target, bands):
    if len(bands) != 10:
        raise ValueError("The equalizer requires 10 bands")
    nodes, links = [], []
    max_gain = 0.0
    for i, band in enumerate(bands):
        freq, gain, q = float(band["frequency"]), float(band["gain"]), float(band.get("q", 1.4))
        if not (20 <= freq <= 20000 and -12 <= gain <= 12 and 0.1 <= q <= 12):
            raise ValueError("Band values are out of range")
        max_gain += max(0.0, gain)
        nodes.append({"type": "builtin", "label": "bq_peaking", "name": f"eq{i}",
                      "control": {"Freq": freq, "Q": q, "Gain": gain}})
        if i:
            links.append({"output": f"eq{i-1}:Out", "input": f"eq{i}:In"})
    # Conservative headroom: sum of positive boosts prevents clipping even with overlapping bands.
    preamp = 10 ** (-max_gain / 20)
    nodes.insert(0, {"type": "builtin", "label": "linear", "name": "headroom",
                     "control": {"Mult": preamp, "Add": 0.0}})
    links.insert(0, {"output": "headroom:Out", "input": "eq0:In"})
    return {"context.properties": {"log.level": 1},
            "context.spa-libs": {"audio.convert.*": "audioconvert/libspa-audioconvert",
                                 "support.*": "support/libspa-support"},
            "context.modules": [{"name": "libpipewire-module-protocol-native"},
                {"name": "libpipewire-module-client-node"},
                {"name": "libpipewire-module-filter-chain", "args": {
                    "node.description": "Hyper Linux · Equalizer", "media.name": "Hyper Linux EQ",
                    "filter.graph": {"nodes": nodes, "links": links,
                                     "inputs": ["headroom:In"], "outputs": ["eq9:Out"]},
                    "audio.channels": 2, "audio.position": ["FL", "FR"],
                    "capture.props": {"node.name": "hyper_linux_eq", "media.class": "Audio/Sink"},
                    "playback.props": {"node.name": "hyper_linux_eq_output", "node.passive": True,
                                       "target.object": target, "node.dont-fallback": True}}}]}


def loop_config(source, sink, volume):
    if not 0 <= volume <= 100:
        raise ValueError("Invalid volume")
    # A real gain node controls monitor volume; a generic 'volume' stream
    # property is not sufficient to guarantee attenuation on PipeWire.
    config = eq_config(sink, [{"frequency": 1000, "gain": 0, "q": 1} for _ in range(10)])
    args = config["context.modules"][-1]["args"]
    args.update({"node.description": "Hyper Linux · Microphone monitor", "media.name": "Hyper Linux Monitor",
                 "node.latency": "480/48000", "audio.channels": 1, "audio.position": ["MONO"],
                 "filter.graph": {"nodes": [{"type": "builtin", "name": "monitor_gain", "label": "linear",
                     "control": {"Mult": volume / 100, "Add": 0.0}}],
                     "inputs": ["monitor_gain:In"], "outputs": ["monitor_gain:Out"]},
                 "capture.props": {"node.name": "hyper_linux_monitor_input", "node.passive": True,
                     "target.object": source, "node.dont-fallback": True},
                 "playback.props": {"node.name": "hyper_linux_monitor_output", "target.object": sink,
                     "node.dont-fallback": True}})
    return config


class Audio:
    def __init__(self):
        self.loop = self.eq = None
        self.eq_file = self.loop_file = None

    def stop(self, name):
        child = getattr(self, name)
        if child:
            child.terminate()
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
            setattr(self, name, None)
        path = getattr(self, name + "_file")
        if path:
            path.unlink(missing_ok=True)
            setattr(self, name + "_file", None)

    def start_loop(self, source, sink, volume):
        devices = audio_devices()
        if source not in [d["name"] for d in devices["sources"]] or sink not in [d["name"] for d in devices["sinks"]]:
            raise ValueError("Select the Cloud III Wireless input and output")
        if not 0 <= volume <= 100:
            raise ValueError("Invalid volume")
        self.stop("loop")
        DATA.mkdir(parents=True, exist_ok=True)
        fd, path = tempfile.mkstemp(prefix="hyper-linux-monitor-", suffix=".conf")
        self.loop_file = Path(path)
        with os.fdopen(fd, "w") as f:
            json.dump(loop_config(source, sink, volume), f)
        with (DATA / "loopback.log").open("w") as log:
            self.loop = subprocess.Popen(["pipewire", "-c", path], stdin=subprocess.DEVNULL, stdout=log, stderr=log, env=system_environment())
        time.sleep(0.12)
        if self.loop.poll() is not None:
            raise RuntimeError((DATA / "loopback.log").read_text()[-1200:] or "PipeWire stopped the monitor")

    def start_eq(self, target, bands):
        if target not in [d["name"] for d in audio_devices()["sinks"]]:
            raise ValueError("Select the physical headset output")
        config = eq_config(target, bands)
        self.stop("eq")
        DATA.mkdir(parents=True, exist_ok=True)
        fd, path = tempfile.mkstemp(prefix="hyper-linux-eq-", suffix=".conf")
        self.eq_file = Path(path)
        with os.fdopen(fd, "w") as f:
            json.dump(config, f)
        with (DATA / "eq.log").open("w") as log:
            self.eq = subprocess.Popen(["pipewire", "-c", path], stdin=subprocess.DEVNULL, stdout=log, stderr=log, env=system_environment())
        # Verify the node exists. Do not alter the user's default audio device.
        for _ in range(10):
            if self.eq.poll() is not None:
                raise RuntimeError((DATA / "eq.log").read_text()[-1200:] or "PipeWire stopped the equalizer")
            sinks = json.loads(run(["pactl", "-f", "json", "list", "sinks"]))
            if any(r["name"] == "hyper_linux_eq" for r in sinks):
                return
            time.sleep(0.08)
        self.stop("eq")
        raise RuntimeError("PipeWire did not register the equalizer output; check eq.log")

    def shutdown(self):
        self.stop("loop")
        self.stop("eq")


def diagnose():
    trace = Trace()
    headset = Headset(trace)
    result = {"devices": discover(), "trace": str(trace.path), "hardware_verified": False}
    try:
        result["headset"] = headset.refresh()
        result["hardware_verified"] = result["headset"]["connected"] is True
    except Exception as exc:
        result["usb_error"] = str(exc)
    finally:
        headset.close()
    try:
        result["audio"] = audio_devices()
    except Exception as exc:
        result["audio_error"] = str(exc)
    DATA.mkdir(parents=True, exist_ok=True)
    (DATA / "diagnostic.json").write_text(json.dumps(result, ensure_ascii=False, indent=2))
    return result


def server():
    trace, audio = Trace(), Audio()
    headset = Headset(trace)
    sel = selectors.DefaultSelector()
    sel.register(sys.stdin.buffer, selectors.EVENT_READ)
    subscription = None
    try:
        subscription = subprocess.Popen(["pactl", "subscribe"], stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=system_environment())
        os.set_blocking(subscription.stdout.fileno(), False)
        sel.register(subscription.stdout, selectors.EVENT_READ)
    except OSError:
        pass
    input_buffer = b""
    subscription_buffer = b""
    poll_at = time.monotonic() + 0.1
    emit("ready", trace=str(trace.path))
    try:
        while True:
            # Small wait keeps physical mute/sidetone events responsive, even when UI is idle.
            if headset.fd is not None:
                try:
                    if headset.receive():
                        emit("state", **headset.snapshot())
                except OSError as exc:
                    headset.close()
                    emit("error", message=str(exc))
                    emit("state", **headset.snapshot())
            lines = []
            for key, _ in sel.select(0.01):
                if subscription and key.fileobj is subscription.stdout:
                    chunk = os.read(key.fileobj.fileno(), 65536)
                    if not chunk:
                        sel.unregister(key.fileobj)
                    else:
                        subscription_buffer += chunk
                        complete = []
                        while b"\n" in subscription_buffer:
                            event, subscription_buffer = subscription_buffer.split(b"\n", 1)
                            complete.append(event)
                        if any(b"on source " in event or b"on sink " in event for event in complete):
                            try:
                                emit("audio_state", data=audio_devices())
                            except Exception:
                                pass
                    continue
                chunk = os.read(key.fileobj.fileno(), 65536)
                if not chunk:
                    return
                input_buffer += chunk
                if len(input_buffer) > 1_000_000:
                    raise ValueError("Request is too large")
                while b"\n" in input_buffer:
                    line, input_buffer = input_buffer.split(b"\n", 1)
                    lines.append(line)
            for line in lines:
                request = {}
                try:
                    request = json.loads(line)
                    action = request.get("action")
                    data = {}
                    if action == "refresh":
                        data = headset.refresh()
                        emit("state", **data)
                    elif action == "set":
                        start = time.monotonic()
                        data = headset.set(request["name"], request["value"])
                        data["elapsed_ms"] = round((time.monotonic() - start) * 1000)
                        emit("state", **data)
                        if not data["confirmed"]:
                            raise RuntimeError("Command sent, but the headset did not confirm the requested value.")
                    elif action == "audio_devices":
                        data = audio_devices()
                    elif action == "audio_set":
                        devices = audio_devices()
                        kind = request["kind"]
                        if kind not in ("sink", "source"):
                            raise ValueError("Invalid device type")
                        target = request["target"]
                        if target not in [d["name"] for d in devices[kind + "s"]]:
                            raise ValueError("HyperX device not found")
                        if "mute" in request:
                            run(["pactl", f"set-{kind}-mute", target, "1" if request["mute"] else "0"])
                        elif "volume" in request:
                            vol = int(request["volume"])
                            if not 0 <= vol <= 100:
                                raise ValueError("Volume is out of range")
                            run(["pactl", f"set-{kind}-volume", target, f"{vol}%"])
                        data = audio_devices()
                    elif action == "loopback":
                        if request["enabled"]:
                            audio.start_loop(request["source"], request["sink"], int(request.get("volume", 25)))
                        else:
                            audio.stop("loop")
                        data = {"enabled": bool(audio.loop and audio.loop.poll() is None)}
                    elif action == "eq":
                        if request["enabled"]:
                            audio.start_eq(request["sink"], request["bands"])
                        else:
                            audio.stop("eq")
                        data = {"enabled": bool(audio.eq and audio.eq.poll() is None)}
                    elif action == "diagnose":
                        data = {"devices": discover(), "headset": headset.snapshot(), "trace": str(trace.path)}
                        try:
                            data["audio"] = audio_devices()
                        except Exception as exc:
                            data["audio_error"] = str(exc)
                        (DATA / "diagnostic.json").write_text(json.dumps(data, ensure_ascii=False, indent=2))
                    elif action == "quit":
                        return
                    else:
                        raise ValueError("Unknown action")
                    emit("result", id=request.get("id"), action=action, ok=True, data=data)
                except Exception as exc:
                    emit("result", id=request.get("id"), action=request.get("action"), ok=False, error=str(exc))
            if time.monotonic() >= poll_at:
                poll_at = time.monotonic() + 15
                if headset.mode:
                    try:
                        # Queries are spaced over loop iterations; a full refresh never blocks mute clicks.
                        headset.send(0x89)
                        headset.send(0x8A)
                        headset.send(0x82)
                    except Exception as exc:
                        headset.close()
                        emit("error", message=str(exc))
                        emit("state", **headset.snapshot())
            for name in ("loop", "eq"):
                child = getattr(audio, name)
                if child and child.poll() is not None:
                    audio.stop(name)
                    emit("process_stopped", name=name)
    finally:
        if subscription:
            subscription.terminate()
            try:
                subscription.wait(timeout=1)
            except subprocess.TimeoutExpired:
                subscription.kill()
                subscription.wait()
        audio.shutdown()
        headset.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Hyper Linux local service for Cloud III Wireless")
    parser.add_argument("--server", action="store_true")
    parser.add_argument("--diagnose", action="store_true")
    parser.add_argument("--runtime-info", action="store_true", help="Print backend runtime information without accessing hardware")
    args = parser.parse_args()
    if args.runtime_info:
        print(json.dumps({"python": platform.python_version(), "frozen": bool(getattr(sys, "frozen", False)),
                          "executable": sys.executable, "protocol": "cloud-iii-wireless"}))
    elif args.server:
        server()
    else:
        print(json.dumps(diagnose(), ensure_ascii=False, indent=2))
