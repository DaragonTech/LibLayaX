#!/usr/bin/env python3
"""Parity test: the C ABI library must answer exactly like laya-cli for the same checkpoint.

    python test_parity.py --lib build/bin/liblaya.so --cli build/bin/laya-cli --model MODEL_DIR
           [--backend cpu] [--compare-dump other_platform_dump.json]

Runs a set of requests through both, compares the "results" members byte for byte, and
optionally compares against a dump written by test-capi on another platform (LAYA_TEST_DUMP),
e.g. the Windows laya.dll run under Wine.
"""
import argparse, ctypes, json, subprocess, sys

REQUESTS = [
    {"state": "Please refund the duplicate charge.",
     "questions": {"refund": {"type": "noul", "instructions": "Does the customer ask for a refund?"}}},
    {"state": "I want to cancel my subscription.",
     "questions": {"intent": {"type": "choice", "instructions": "What does the customer want?",
                              "criteria": ["cancel", "upgrade", "refund"]},
                   "anger": {"type": "score", "instructions": "How angry is the customer?",
                             "criteria": ["calm", "annoyed", "furious"]}}},
    {"state": {"ticket": 42, "text": "Café naïve 日本語 — money back now!"},
     "questions": {"refund": {"type": "noul", "instructions": "Does the customer ask for a refund?",
                              "criteria": {"true": "asks for money back", "false": "does not"}}}},
    {"state": "Order #1182 arrived broken; the box was crushed.",
     "questions": {"category": {"type": "choice", "instructions": "Ticket category",
                                "criteria": {"damage": "item arrived damaged", "late": "delivery is late",
                                             "billing": None, "other": ""}}}},
]
# Same batch test-capi uses for its dump (request order and content must match exactly).
DUMP_BATCH = [
    {"state": "I want to cancel my subscription.",
     "questions": {"intent": {"type": "choice", "instructions": "What does the customer want?",
                              "criteria": ["cancel", "upgrade", "refund"]},
                   "anger": {"type": "score", "instructions": "How angry is the customer?",
                             "criteria": ["calm", "annoyed", "furious"]}}},
    {"state": {"ticket": 42, "text": "Café naïve 日本語 — money back now!"},
     "questions": {"refund": {"type": "noul", "instructions": "Does the customer ask for a refund?",
                              "criteria": {"true": "asks for money back", "false": "does not"}}}},
]


def load(path):
    lib = ctypes.CDLL(path)
    lib.laya_create.restype = ctypes.c_void_p
    lib.laya_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    lib.laya_predict.restype = ctypes.c_void_p
    lib.laya_predict.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.laya_free_string.argtypes = [ctypes.c_void_p]
    lib.laya_destroy.argtypes = [ctypes.c_void_p]
    lib.laya_last_error.restype = ctypes.c_char_p
    return lib


def lib_predict(lib, agent, request):
    p = lib.laya_predict(agent, json.dumps(request, ensure_ascii=False).encode())
    text = ctypes.string_at(p).decode()
    lib.laya_free_string(p)
    return json.loads(text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lib", required=True)
    ap.add_argument("--cli", required=True)
    ap.add_argument("--model", required=True)
    ap.add_argument("--backend", default="cpu")
    ap.add_argument("--compare-dump")
    a = ap.parse_args()

    lib = load(a.lib)
    agent = lib.laya_create(a.model.encode(), json.dumps({"backend": a.backend}).encode())
    if not agent:
        sys.exit("laya_create failed: " + lib.laya_last_error().decode())

    batches = [[r] for r in REQUESTS] + [REQUESTS, DUMP_BATCH]
    lines = "\n".join(json.dumps(b, ensure_ascii=False) for b in batches) + "\n"
    cli = subprocess.run([a.cli, "--model", a.model, "--" + a.backend], input=lines.encode(),
                         capture_output=True, check=True)
    cli_out = [json.loads(line) for line in cli.stdout.decode().splitlines() if line.strip()]
    failures = 0
    for i, (batch, expected) in enumerate(zip(batches, cli_out)):
        got = lib_predict(lib, agent, batch)
        same = got.get("results") == expected.get("results")
        failures += not same
        print(f"batch {i}: {'OK ' if same else 'DIFF'} {json.dumps(got.get('results'), ensure_ascii=False)[:110]}")
        if not same:
            print("  cli :", json.dumps(expected, ensure_ascii=False))
            print("  lib :", json.dumps(got, ensure_ascii=False))

    if a.compare_dump:
        other = json.loads("{" + open(a.compare_dump, encoding="utf-8").read() + "}")["results"]
        same = other == cli_out[-1]["results"]
        failures += not same
        print(f"cross-platform dump {a.compare_dump}: {'OK' if same else 'DIFF'}")
        if not same:
            print("  dump:", json.dumps(other, ensure_ascii=False))
            print("  cli :", json.dumps(cli_out[-1]["results"], ensure_ascii=False))

    lib.laya_destroy(agent)
    print("PASS" if not failures else f"FAIL ({failures})")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
