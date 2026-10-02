#!/usr/bin/env python3
"""Generate a synthetic Laya checkpoint for testing the C API without downloading weights.

The checkpoint has the exact architecture laya.cpp validates (the "english" ModernBERT-large
layout: width 1024, 28 layers, vocab 50368) and a real byte-level BPE tokenizer, but the
weights are seeded random numbers. Answers are therefore meaningless; the checkpoint exists
only to exercise loading, tokenization, the full forward pass and JSON formatting end to end.

    python make_synthetic_model.py OUT_DIR [--seed 1234]
Requires numpy and tokenizers (pip install numpy tokenizers). Writes ~830 MB (F16 weights).
"""
import argparse, json, os, struct
import numpy as np

WIDTH, HEADS, LAYERS, INTER, VOCAB = 1024, 16, 28, 2624, 50368
ACT_COSTS = {"escalate": 0.5}  # as in the published checkpoint: one cost -> two action logits


def tensor_specs():
    w = WIDTH
    yield "encoder.embeddings.tok_embeddings.weight", (VOCAB, w), "normal"
    yield "encoder.embeddings.norm.weight", (w,), "ones"
    yield "encoder.final_norm.weight", (w,), "ones"
    yield "type_emb.weight", (3, w), "normal"
    yield "temperature", (3,), "ones"
    for i in range(LAYERS):
        p = f"encoder.layers.{i}"
        if i:
            yield p + ".attn_norm.weight", (w,), "ones"
        yield p + ".mlp_norm.weight", (w,), "ones"
        yield p + ".attn.Wqkv.weight", (3 * w, w), "normal"
        yield p + ".attn.Wo.weight", (w, w), "normal"
        yield p + ".mlp.Wi.weight", (2 * INTER, w), "normal"
        yield p + ".mlp.Wo.weight", (w, INTER), "normal"
    for i in range(2):
        p = f"head.layers.{i}"
        yield p + ".self_attn.in_proj_weight", (3 * w, w), "normal"
        yield p + ".self_attn.in_proj_bias", (3 * w,), "zeros"
        yield p + ".self_attn.out_proj.weight", (w, w), "normal"
        yield p + ".self_attn.out_proj.bias", (w,), "zeros"
        yield p + ".linear1.weight", (4 * w, w), "normal"
        yield p + ".linear1.bias", (4 * w,), "zeros"
        yield p + ".linear2.weight", (w, 4 * w), "normal"
        yield p + ".linear2.bias", (w,), "zeros"
        for s in (".norm1.weight", ".norm2.weight"):
            yield p + s, (w,), "ones"
        for s in (".norm1.bias", ".norm2.bias"):
            yield p + s, (w,), "zeros"
    yield "scorer.0.weight", (w,), "ones"
    yield "scorer.0.bias", (w,), "zeros"
    yield "scorer.1.weight", (w, w), "normal"
    yield "scorer.1.bias", (w,), "zeros"
    yield "scorer.3.weight", (1, w), "normal"
    yield "scorer.3.bias", (1,), "zeros"
    yield "act_head.0.weight", (256, w + 4), "normal"
    yield "act_head.0.bias", (256,), "zeros"
    yield "act_head.2.weight", (len(ACT_COSTS) + 1, 256), "normal"
    yield "act_head.2.bias", (len(ACT_COSTS) + 1,), "zeros"


def write_safetensors(path, seed):
    rng = np.random.default_rng(seed)
    header, offset, specs = {}, 0, list(tensor_specs())
    for name, shape, _ in specs:
        n = int(np.prod(shape)) * 2
        header[name] = {"dtype": "F16", "shape": list(shape), "data_offsets": [offset, offset + n]}
        offset += n
    header["__metadata__"] = {"format": "pt", "note": "synthetic laya test checkpoint"}
    raw = json.dumps(header, separators=(",", ":")).encode()
    raw += b" " * (-len(raw) % 8)
    with open(path, "wb") as f:
        f.write(struct.pack("<Q", len(raw)))
        f.write(raw)
        for name, shape, kind in specs:
            if kind == "normal":
                a = rng.standard_normal(shape, dtype=np.float32) * 0.02
            elif kind == "ones":
                a = np.ones(shape, np.float32)
            else:
                a = np.zeros(shape, np.float32)
            f.write(a.astype("<f2").tobytes())


def write_tokenizer(directory):
    from tokenizers import Tokenizer, models, pre_tokenizers, trainers, normalizers, decoders
    specials = ["[UNK]", "[CLS]", "[SEP]", "[PAD]", "[MASK]"]
    tok = Tokenizer(models.BPE())
    tok.normalizer = normalizers.NFC()
    tok.pre_tokenizer = pre_tokenizers.ByteLevel(add_prefix_space=False, use_regex=True)
    tok.decoder = decoders.ByteLevel()
    corpus = [
        "Please refund the duplicate charge on my account.",
        "Does the customer ask for a refund? yes, the statement holds",
        "no, the statement does not hold. choice question: score question: noul question:",
        "level 0: level 1: level 2: true: false: The quick brown fox jumps over the lazy dog.",
        "Café naïve résumé Ünïcödé — 日本語 テキスト 中文 español português ação coração",
        "I want to cancel my subscription and get my money back immediately!",
    ] * 50
    trainer = trainers.BpeTrainer(vocab_size=2000, special_tokens=specials,
                                  initial_alphabet=pre_tokenizers.ByteLevel.alphabet(), show_progress=False)
    tok.train_from_iterator(corpus, trainer)
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, "tokenizer.json")
    tok.save(path)
    data = json.load(open(path, encoding="utf-8"))
    data["normalizer"] = {"type": "NFC"}
    data["model"]["dropout"] = None
    data["model"]["byte_fallback"] = False
    data["model"]["ignore_merges"] = False
    data["model"]["merges"] = [m.split(" ") if isinstance(m, str) else m for m in data["model"]["merges"]]
    for t in data["added_tokens"]:
        t.update(single_word=False, rstrip=False, lstrip=False, normalized=False)
    json.dump(data, open(path, "w", encoding="utf-8"), ensure_ascii=False)
    json.dump({"cls_token": "[CLS]", "sep_token": "[SEP]", "pad_token": "[PAD]",
               "mask_token": {"content": "[MASK]"}, "unk_token": "[UNK]"},
              open(os.path.join(directory, "tokenizer_config.json"), "w"))


def write_configs(out):
    os.makedirs(os.path.join(out, "encoder"), exist_ok=True)
    encoder = {
        "model_type": "modernbert", "hidden_size": WIDTH, "num_attention_heads": HEADS,
        "num_hidden_layers": LAYERS, "intermediate_size": INTER, "vocab_size": VOCAB,
        "local_attention": 128, "global_attn_every_n_layers": 3, "norm_bias": False,
        "attention_bias": False, "mlp_bias": False, "hidden_activation": "gelu", "norm_eps": 1e-5,
        "layer_types": ["full_attention" if i % 3 == 0 else "sliding_attention" for i in range(LAYERS)],
        "rope_parameters": {"full_attention": {"rope_type": "default", "rope_theta": 160000.0},
                            "sliding_attention": {"rope_type": "default", "rope_theta": 10000.0}},
    }
    json.dump(encoder, open(os.path.join(out, "encoder", "config.json"), "w"), indent=1)
    # Mirrors the published english checkpoint's rl_agent_config.json (training stats omitted).
    agent = {"encoder": "/kaggle/input/notebooks/nandukuttan/rl-agent-asset/models/ModernBERT-large",
             "head_layers": 2, "max_len": 512, "head_max_len": 192, "max_prefixes": 6,
             "act_costs": ACT_COSTS, "cost_wrong_act": 3.0, "amp_dtype": "bf16", "model_name": "rl-agent",
             "temperature": [1.6369030475616455, 1.2514300346374512, 1.983399510383606],
             "temperature_by_options": {"choice:3-5": 1.7601518630981445, "choice:6-10": 1.0000158548355103,
                                        "score:3-5": 1.2514300346374512, "noul:2": 1.983399510383606,
                                        "choice:11+": 0.10058280825614929, "choice:2": 1.9063563346862793}}
    json.dump(agent, open(os.path.join(out, "rl_agent_config.json"), "w"), indent=1)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--seed", type=int, default=1234)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    write_configs(args.out)
    write_tokenizer(os.path.join(args.out, "tokenizer"))
    write_safetensors(os.path.join(args.out, "model.safetensors"), args.seed)
    print("synthetic checkpoint written to", args.out)
