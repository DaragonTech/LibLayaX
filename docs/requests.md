# Requests and answers

A request is a piece of text plus one or more questions about it. The answer gives, for each
question, a decision and the probabilities behind it. Both are JSON text, the same format on
every platform and through every binding. It is also the format the `laya-cli` tool and the
HTTP server of laya.cpp use.

The example answers on this page were produced by the English model on the CPU.

## A request

```json
{
  "state": "Please refund the duplicate charge.",
  "questions": {
    "refund": {
      "type": "noul",
      "instructions": "Does the customer ask for a refund?"
    }
  }
}
```

| Field | Required | Meaning |
|---|---|---|
| `state` | yes | The text to decide about: a message, a ticket, a document. |
| `questions` | yes | An object with at least one question. Each key is a name you choose (`refund` here); the answer comes back under the same name. |
| `type` | yes | The kind of question: `noul`, `choice` or `score`. |
| `instructions` | yes | The question itself, in plain words. |
| `criteria` | depends on the type | The possible answers. See each type below. |

`state` and `instructions` are normally strings. A JSON object or array is also accepted; the
library turns it into text and the model reads that text, so you can pass a record such as
`{"subject":"Invoice","body":"Charged twice"}` without building a string yourself.

## The three question types

### noul: yes or no

`noul` answers a yes/no question with a probability.

```json
"refund": {"type": "noul", "instructions": "Does the customer ask for a refund?"}
```

Answer:

```json
"refund": {"type": "noul", "confidence": 0.8364,
           "action": {"act_probability": 1.0}, "noul": 0.8364}
```

| Field | Meaning |
|---|---|
| `noul` | The probability that the answer is yes, from 0 to 1. |
| `confidence` | How far from undecided: the larger of `noul` and `1 − noul`, so from 0.5 to 1. |

Take `noul` above 0.5 as yes, or choose your own threshold: a higher one when a wrong yes is
expensive.

`criteria` is optional for this type. It can describe what yes and no mean, when the
instructions alone leave room for doubt:

```json
"criteria": {"true": "money back is requested", "false": "no money back is requested"}
```

### choice: one of several

`choice` picks one option from a list.

```json
"intent": {"type": "choice",
           "instructions": "What does the customer want?",
           "criteria": ["cancel", "upgrade", "refund"]}
```

Answer, for the text "I want to cancel my subscription.":

```json
"intent": {"type": "choice", "confidence": 0.8491,
           "action": {"act_probability": 1.0},
           "choice": "cancel",
           "probabilities": {"cancel": 0.9649, "upgrade": 0.0047, "refund": 0.0304}}
```

| Field | Meaning |
|---|---|
| `choice` | The option with the highest probability. |
| `probabilities` | The probability of every option, in the order you gave them. They add up to 1. |
| `confidence` | 1 when all the probability is on one option, 0 when it is spread evenly over all of them. |

`criteria` is required: between 2 and 255 options. It can be a list of names, as above, or
an object that gives each name a description. The model reads the descriptions; the answer
uses the names.

```json
"criteria": {"cancel":  "wants to end the subscription",
             "upgrade": "wants a bigger plan",
             "other":   null}
```

A `null` or empty description means "the name says enough".

### score: a level on a scale

`score` places the text on an ordered scale.

```json
"anger": {"type": "score",
          "instructions": "How angry is the customer?",
          "criteria": ["calm", "annoyed", "furious"]}
```

Answer, for the text "This is the third time I am writing!!!":

```json
"anger": {"type": "score", "confidence": 0.2076,
          "action": {"act_probability": 1.0},
          "score": 0.8568,
          "probabilities": {"0": 0.2505, "1": 0.6423, "2": 0.1072},
          "legend": {"0": "calm", "1": "annoyed", "2": "furious"}}
```

| Field | Meaning |
|---|---|
| `score` | The average level, weighted by probability. Levels are numbered from 0, so with three levels it runs from 0 to 2. Here 0.8568 is a little below "annoyed". |
| `probabilities` | The probability of each level, by number. |
| `legend` | Which description belongs to which number. |
| `confidence` | As for `choice`: 1 when one level takes everything, 0 when all are equally likely. |

`criteria` is required and must be a list, lowest level first, with 2 to 255 entries. To get
a single level and not an average, take the entry of `probabilities` with the highest value.

### Fields every answer has

| Field | Meaning |
|---|---|
| `type` | The question type, repeated. |
| `confidence` | See each type above. |
| `action.act_probability` | The output of the model's action head, from 0 to 1. LibLayaX passes it through unchanged; what it means and how it was trained is described by the Laya project. |

All numbers are rounded to four decimals.

## Several questions about one text

Put more than one entry in `questions`. Types can be mixed.

```json
{
  "state": "Please refund the duplicate charge.",
  "questions": {
    "refund": {"type": "noul", "instructions": "Does the customer ask for a refund?"},
    "tone":   {"type": "score", "instructions": "How angry is the customer?",
               "criteria": ["calm", "annoyed", "furious"]}
  }
}
```

The answer has one entry per question, under the names you chose. The model reads the text
once per question, so two questions cost about twice as much as one.

## Batches

To decide about several texts in one call, send an array of requests:

```json
[
  {"state": "Refund me.",        "questions": {"q": {"type": "noul", "instructions": "Is a refund requested?"}}},
  {"state": "Thanks, all good.", "questions": {"q": {"type": "noul", "instructions": "Is a refund requested?"}}}
]
```

`results` then has one entry per request, in the same order.

**This is how to get speed on a GPU.** A GPU works on all the questions of a call together.
On an Apple M3 Ultra's GPU one question sent on its own takes about 29 ms, while sixteen sent
in one call take about 94 ms together, 5.9 ms each. The speed figures in the project README
(about 670 questions per second on an RTX 5080 laptop GPU) are for batches of 16. On a CPU a
batch is convenient but hardly faster per question: 70 ms each in a batch against 76 ms alone
on the same machine.

Things to know about batches:

* The size of a batch is the total number of questions in the call, counting every question
  of every request.
* Every question in a batch is padded to the length of the longest one, so a batch of texts
  of similar length is the most efficient. One very long text among short ones slows the
  whole batch.
* If one request in the array is not valid, the whole call fails with an error and no
  results. Check requests you do not control before putting them in a batch with others.
* A batch that does not fit in memory fails with `Insufficient memory for this batch`. Send
  fewer at a time. Batches of 16 are a reasonable starting point.

## The whole answer

```json
{
  "results": [
    {
      "model": "laya-rl-agent",
      "answers": {
        "refund": {"type": "noul", "confidence": 0.8364,
                   "action": {"act_probability": 1.0}, "noul": 0.8364}
      },
      "usage": {"input_tokens": 40, "output_tokens": 0}
    }
  ],
  "elapsed_ms": 117.8,
  "backend": "CPU",
  "device": "Intel(R) Core(TM) Ultra 9 275HX"
}
```

| Field | Meaning |
|---|---|
| `results` | One entry per request, in the order they were sent. A single request (not in an array) also comes back as an array of one. |
| `results[i].model` | Always `laya-rl-agent`. |
| `results[i].answers` | One entry per question, under the question's name. |
| `results[i].usage.input_tokens` | The number of tokens the model read for this request, all its questions together. |
| `results[i].usage.output_tokens` | Always 0: the model does not generate text. |
| `elapsed_ms` | The time the call spent preparing and running the model, in milliseconds, for the whole call. |
| `backend` | `CPU`, or the GPU backend's name such as `Vulkan0`. |
| `device` | The name of the processor or GPU that did the work. |

## Errors

A request that cannot be answered returns one object with one field:

```json
{"error": "Unsupported question type: yesno"}
```

Test for the `error` key before reading `results`. Common messages:

| Message | Cause |
|---|---|
| `[json.exception.parse_error…] …` | The request is not valid JSON. The message gives the position. |
| `[json.exception.out_of_range.403] key 'questions' not found` | A required field is missing (`state`, `questions`, `type` or `instructions`). |
| `questions must be a nonempty object` | `questions` is empty or not an object. |
| `requests must be a nonempty array` | An empty array was sent. |
| `Unsupported question type: …` | `type` is not `noul`, `choice` or `score`. |
| `Choice criteria must be a list or object` | A `choice` question without usable `criteria`. |
| `Score criteria must be an array` | A `score` question whose `criteria` is not a list. |
| `Boolean criteria must be an object` | A `noul` question whose `criteria` is not an object. |
| `Questions require 2 through 255 options` | Fewer than 2 or more than 255 options or levels. |
| `Question '…' exceeds …` | The question or the text is too long. See the next section. |

## Size limits

The model reads a limited number of tokens at a time. A token is a word or a piece of a
word; English prose averages a little more than one token per word.

For each question the model reads one sequence made of the question type, the instructions,
the options and then the text. The limits of the published models are:

| Limit | Value | Error when exceeded |
|---|---|---|
| The whole sequence: question plus text | 512 tokens (`max_len`) | `Question '…' exceeds state context limit (N tokens)` |
| The question part: instructions plus all options | 192 tokens (`head_max_len`) | `… exceeds question head token budget (192 tokens)` or `… exceeds heading token budget (N tokens)` |
| One option or level | 48 tokens | `… exceeds option token limit (48)` |
| Options per question | 2 to 255 | `Questions require 2 through 255 options` |

`laya_info` reports `max_len` and `head_max_len` for the model that is loaded. In the first
message, `N` is the number of tokens that were left for the text after the question, which
is the practical limit for your text with that question: roughly 450 to 480 tokens with a
short question, or about 350 English words.

By default a request that is too long is refused, so you never get an answer about a text the
model only partly read. There are two ways to handle longer texts:

* **Shorten or split the text yourself**, for example decide per paragraph. You stay in
  control of what the model sees.
* **Load the model with `"allow_truncation": true`** ([Options](options.md)). The library
  then cuts what does not fit: the end of the text, and if necessary the end of long options
  and instructions. The answer is then about the beginning of the text only.

`laya_prepare` shows how many tokens a request takes (`lengths`) without running the model.

## Same question, same answer

The library is deterministic: the same request to the same model gives the same answer every
time. The CPU builds gave the same reference answers on every platform that was tested
(0.8364 for the refund example). On a GPU with half precision (`fp16`, `bf16`) the
probabilities stayed within about 0.003 of the CPU's in the measured runs.
