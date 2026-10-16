import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
import unittest
from unittest.mock import patch

import backend
from protocol import State, packet, setting


class ProtocolTests(unittest.TestCase):
    def test_official_packet_vectors(self):
        # Reconstructed from create_buffers, get_mute and set_sidetone in the bundled service.
        self.assertEqual(packet(0x86), bytes.fromhex("668600") + bytes(61))
        self.assertEqual(packet(0x01, 1), bytes.fromhex("660101") + bytes(61))
        self.assertEqual(packet(0x02, 30, 62), bytes.fromhex("66021e") + bytes(59))

    def test_no_arbitrary_or_firmware_commands(self):
        for command in [0x00, 0x04, 0x05, 0x1E, 0xFF]:
            with self.assertRaises(ValueError):
                packet(command)

    def test_invalid_settings(self):
        for name, value in [("sleep", -1), ("sleep", 256), ("sleep", True), ("mute", 1),
                            ("sidetone", "true"), ("firmware", b"anything")]:
            with self.assertRaises(ValueError):
                setting(name, value)

    def test_battery_invalid_or_offline_never_becomes_zero(self):
        state = State()
        self.assertIsNone(state.update(bytes.fromhex("6689000000")))
        self.assertIsNone(state.values["battery"])
        state.update(bytes.fromhex("6689010064"))
        self.assertEqual(state.values["battery"], 100)
        self.assertIsNone(state.update(bytes.fromhex("66890100ff")))
        self.assertEqual(state.values["battery"], 100)

    def test_notification_sidetone_is_09_not_84(self):
        state = State()
        self.assertEqual(state.update(bytes.fromhex("660901")), "sidetone")
        self.assertIs(state.values["sidetone"], True)
        self.assertEqual(state.update(bytes.fromhex("668400")), "sidetone")
        self.assertIs(state.values["sidetone"], False)

    def test_hardware_button_event_updates_mute(self):
        state = State()
        self.assertEqual(state.update(bytes.fromhex("660a01")), "mute")
        self.assertIs(state.values["mute"], True)
        state.update(bytes.fromhex("668600"))
        self.assertIs(state.values["mute"], False)

    def test_unplug_resets_stale_values(self):
        state = State()
        state.update(bytes.fromhex("6689010037"))
        state.update(bytes.fromhex("668601"))
        state.update(bytes.fromhex("660b00"))
        self.assertIs(state.values["connected"], False)
        self.assertIsNone(state.values["battery"])
        self.assertIsNone(state.values["mute"])

    def test_partial_and_unrelated_reports_ignored(self):
        state = State()
        for data in [b"", b"\x66", b"\x66\x86", b"\x05\x01\x00", b"\x66\x84\xff"]:
            self.assertIsNone(state.update(data))
        self.assertTrue(all(v is None for v in state.values.values()))

    def test_charging_modes(self):
        state = State()
        for value in (0, 1, 2):
            self.assertEqual(state.update(bytes([0x66, 0x8A, value])), "charging")
            self.assertEqual(state.values["charging"], value)
        self.assertIsNone(state.update(bytes([0x66, 0x8A, 255])))

    def test_captured_reports_from_users_actual_dongle(self):
        capture = Path(__file__).parent / "tests/fixtures/cloud3-query.json"
        state = State()
        for item in json.loads(capture.read_text()):
            data = bytes.fromhex(item["hex"])
            if item["direction"] == "rx":
                self.assertEqual(len(data), 62)
                state.update(data)
            else:
                self.assertEqual(len(data), 64)
                self.assertEqual(data, packet(data[1]))
        self.assertEqual(state.values, {"connected": True, "battery": 82, "charging": 0,
            "mute": False, "sidetone": False, "sleep": 20, "color": 2})


class NoTrace:
    def add(self, *args, **kwargs):
        pass


class TransportTests(unittest.TestCase):
    def setUp(self):
        self.rx, self.device_write = os.pipe()
        self.device_read, self.tx = os.pipe()
        os.set_blocking(self.rx, False)
        self.headset = backend.Headset(NoTrace())
        self.headset.fd = self.rx
        self.headset.mode = "hidraw"
        self.headset.state.update(bytes.fromhex("668201"))
        original_write = os.write
        self.write_patch = patch.object(backend.os, "write", side_effect=lambda fd, data:
                                       original_write(self.tx if fd == self.rx else fd, data))
        self.write_patch.start()

    def tearDown(self):
        self.headset.close()
        self.write_patch.stop()
        for fd in (self.device_write, self.device_read, self.tx):
            os.close(fd)

    def test_set_waits_for_matching_readback(self):
        def dongle():
            write = os.read(self.device_read, 64)
            self.assertEqual(write, packet(3, 1))
            query = os.read(self.device_read, 64)
            self.assertEqual(query, packet(0x86))
            os.write(self.device_write, bytes.fromhex("668601"))
        thread = threading.Thread(target=dongle)
        thread.start()
        result = self.headset.set("mute", True)
        thread.join()
        self.assertTrue(result["confirmed"])
        self.assertIs(result["mute"], True)

    def test_timeout_never_claims_write_was_confirmed(self):
        result = self.headset.set("mute", True)
        self.assertFalse(result["confirmed"])
        self.assertIsNone(result["mute"])

    def test_delayed_commit_after_stale_immediate_queries(self):
        def dongle():
            os.read(self.device_read, 64)  # SET sidetone on
            for _ in range(2):
                os.read(self.device_read, 64)
                os.write(self.device_write, bytes.fromhex("668400"))  # cached, old state
            time.sleep(.05)
            os.write(self.device_write, bytes.fromhex("66090101"))
        thread = threading.Thread(target=dongle)
        thread.start()
        result = self.headset.set("sidetone", True)
        thread.join()
        self.assertTrue(result["confirmed"])
        self.assertIs(result["sidetone"], True)

    def test_notification_is_processed_without_polling_query(self):
        os.write(self.device_write, bytes.fromhex("660a01"))
        self.headset.receive(.1)
        self.assertIs(self.headset.state.values["mute"], True)

    def test_alias_response_supported(self):
        def dongle():
            os.read(self.device_read, 64)
            os.write(self.device_write, bytes.fromhex("660b01"))
        thread = threading.Thread(target=dongle)
        thread.start()
        self.assertIsNotNone(self.headset.query(0x82))
        thread.join()


class AudioTests(unittest.TestCase):
    def bands(self, gain=0):
        return [{"frequency": f, "gain": gain, "q": 1.4} for f in
                (32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000)]

    def test_eq_graph_complete_and_target_pinned(self):
        config = backend.eq_config("alsa_output.usb-hyperx", self.bands())
        args = config["context.modules"][-1]["args"]
        graph = args["filter.graph"]
        self.assertEqual(len(graph["nodes"]), 11)
        self.assertEqual(len(graph["links"]), 10)
        self.assertEqual(graph["inputs"], ["headroom:In"])
        self.assertEqual(graph["outputs"], ["eq9:Out"])
        self.assertEqual(args["playback.props"]["target.object"], "alsa_output.usb-hyperx")
        self.assertTrue(args["playback.props"]["node.dont-fallback"])

    def test_loopback_has_real_attenuation_and_pins_both_endpoints(self):
        config = backend.loop_config("hyperx-microphone", "hyperx-headphones", 25)
        args = config["context.modules"][-1]["args"]
        self.assertEqual(args["filter.graph"]["nodes"][0]["control"]["Mult"], .25)
        self.assertEqual(args["capture.props"]["target.object"], "hyperx-microphone")
        self.assertEqual(args["playback.props"]["target.object"], "hyperx-headphones")
        self.assertNotIn("media.class", args["capture.props"])
        self.assertNotIn("media.class", args["playback.props"])
        self.assertTrue(args["capture.props"]["node.dont-fallback"])
        self.assertTrue(args["playback.props"]["node.dont-fallback"])

    def test_eq_headroom_handles_overlapping_boosts(self):
        config = backend.eq_config("test", self.bands(1))
        value = config["context.modules"][-1]["args"]["filter.graph"]["nodes"][0]["control"]["Mult"]
        self.assertAlmostEqual(value, 10 ** (-10 / 20))

    def test_invalid_eq_rejected_including_nan(self):
        for key, value in [("gain", 13), ("frequency", 0), ("q", 0), ("gain", float("nan")), ("q", float("inf"))]:
            bands = self.bands()
            bands[0][key] = value
            with self.assertRaises(ValueError):
                backend.eq_config("test", bands)

    def test_selects_only_correct_headset_not_monitor_or_other_hyperx(self):
        rows = [
            {"name": "alsa_output.usb-HP_HyperX_Cloud_III_Wireless-00", "volume": {"front-left": {"value_percent": "40%"}}},
            {"name": "alsa_input.usb-HP_HyperX_Cloud_III_Wireless-00.monitor"},
            {"name": "alsa_input.usb-HyperX_Cloud_II_Wireless-00"},
            {"name": "alsa_output.usb-HyperX_Cloud_III_S_Wireless-00"},
            {"name": "speakers"}]
        with patch.object(backend, "run", return_value=json.dumps(rows)):
            audio = backend.audio_devices()
        self.assertEqual(len(audio["sinks"]), 1)
        self.assertEqual(audio["sinks"][0]["volume"], 40)

    def test_stopping_one_effect_does_not_kill_unrelated_processes(self):
        audio = backend.Audio()
        child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(10)"])
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(10)"])
        try:
            audio.loop = child
            audio.stop("loop")
            self.assertIsNotNone(child.poll())
            self.assertIsNone(unrelated.poll())
        finally:
            unrelated.terminate()
            unrelated.wait()


class ServerTests(unittest.TestCase):
    def test_batched_requests_are_all_processed(self):
        script = Path(__file__).with_name("backend.py")
        requests = b'{"id":1,"action":"invalid"}\n{"id":2,"action":"invalid"}\n{"action":"quit"}\n'
        result = subprocess.run([sys.executable, str(script), "--server"], input=requests,
                                capture_output=True, timeout=4)
        self.assertEqual(result.returncode, 0, result.stderr)
        messages = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([m["id"] for m in messages if m["type"] == "result"], [1, 2])


if __name__ == "__main__":
    unittest.main(verbosity=2)
