"""Cloud III Wireless protocol reconstructed from NGENUITY 3.1.0.

See research/PROTOCOL.md for binary addresses and limitations.
Only allowlisted, reversible settings are exposed here.
"""
import time

VENDOR = 0x03F0
PRODUCTS = {0x05B7, 0x0C9D}
QUERIES = (0x82, 0x86, 0x84, 0x85, 0x89, 0x8A, 0x8F)
SETTINGS = {"mute": (0x03, 0x86), "sidetone": (0x01, 0x84), "sleep": (0x02, 0x85)}


def packet(command, value=0, size=64):
    if command not in QUERIES and command not in (1, 2, 3):
        raise ValueError("Comando não autorizado pelo protocolo reconstruído")
    if size not in (62, 64) or not 0 <= value <= 255:
        raise ValueError("Pacote inválido")
    return bytes([0x66, command, value]) + bytes(size - 3)


def setting(name, value):
    if name not in SETTINGS:
        raise ValueError("Configuração desconhecida")
    if name in ("mute", "sidetone"):
        if type(value) is not bool:
            raise ValueError("O valor deve ser verdadeiro ou falso")
    elif type(value) is not int or not 0 <= value <= 255:
        raise ValueError("Use 0 (nunca) ou 1 a 255 minutos")
    return SETTINGS[name][0], int(value), SETTINGS[name][1]


class State:
    def __init__(self):
        self.reset()

    def reset(self):
        self.values = {"connected": None, "battery": None, "charging": None,
                       "mute": None, "sidetone": None, "sleep": None, "color": None}
        self.seen = {}

    def update(self, data):
        if len(data) < 3 or data[0] != 0x66:
            return None
        code, value = data[1:3]
        field = None
        if code in (0x82, 0x0B) and value in (0, 1):
            field, value = "connected", bool(value)
            if not value:
                self.reset()
        elif code in (0x86, 0x0A) and value in (0, 1):
            field, value = "mute", bool(value)
        elif code in (0x84, 0x09) and value in (0, 1):
            field, value = "sidetone", bool(value)
        elif code == 0x85:
            field = "sleep"
        elif code in (0x89, 0x0D) and len(data) >= 5:
            if (data[2] or data[3]) and data[4] <= 100:
                field, value = "battery", data[4]
        elif code in (0x8A, 0x0C) and value in (0, 1, 2):
            field = "charging"
        elif code == 0x8F:
            field = "color"
        if field is not None:
            self.values[field] = value
            self.seen[field] = time.monotonic()
        return field

    def snapshot(self):
        now = time.monotonic()
        out = dict(self.values)
        out["age_seconds"] = {k: round(now - t, 2) for k, t in self.seen.items()}
        return out
