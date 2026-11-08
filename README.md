# revisa

[![Release](https://img.shields.io/github/v/release/manuelgscom/revisa?logo=github&color=blue)](https://github.com/manuelgscom/revisa/releases/latest) [![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE) [![Corrects: Spanish](https://img.shields.io/badge/corrects-Spanish-informational)](#what-it-corrects) [![Platforms: Windows · Linux](https://img.shields.io/badge/platforms-Windows%20%C2%B7%20Linux-informational)](#install) [![Runs on CPU · 100% offline](https://img.shields.io/badge/runs%20on-CPU%20%C2%B7%20100%25%20offline-informational)](#how-it-works) [![llama.cpp](https://img.shields.io/badge/built%20with-llama.cpp-informational)](https://github.com/ggml-org/llama.cpp) [![Hugging Face](https://img.shields.io/badge/Hugging%20Face-revisa%20model-FFD21E?logo=huggingface)](https://huggingface.co/collections/manuelgscom/revisa-6af07e5219339ae58de2f63d)

**A Spanish proofreader that works in any app. Runs on your CPU, 100% offline.**

https://github.com/user-attachments/assets/cb7fcc03-ca87-4b9e-98f0-6a1ee052c827

*Windows 11: select the text, press Ctrl+Alt+Space, review the changes, press Enter. In a webmail, a team chat and Notepad. The real app and model; the input is scripted.*

> **You write; revisa proofreads. Nothing leaves your computer.**

Select some Spanish text anywhere (an email in the browser, a chat, a document, a terminal) and press **Ctrl+Alt+Space**. A small window shows what is wrong, with a one-line explanation for each change. **Enter** puts the corrected text in place of your selection; **Esc** leaves it as it was.

Cloud proofreaders send what you write to someone else's server. The local, rule-based ones struggle with real mistakes: on real Spanish Wikipedia edits, LanguageTool reaches an F0.5 of 24.1<!-- fig:test.languagetool.a.f05 --> and changes 39.0%<!-- fig:test.languagetool.a.fa --> of the sentences that were already correct. revisa runs a small language model fine-tuned for this one task, `revisa-0.8b`, inside a native C++ engine on your CPU, at about 138 ms<!-- fig:speed.p50_ms --> per sentence. The model only proposes a corrected sentence; deterministic code decides which changes you see, names each one and drops anything doubtful. No account, no telemetry, no GPU.

**En español.** `revisa` es un corrector de castellano que funciona en cualquier aplicación y sin conexión a internet. Seleccionas un texto, pulsas Ctrl+Alt+Espacio y una ventanita te enseña las correcciones, cada una con su explicación; con Enter, se aplican. Corrige la ortografía, las tildes, los homófonos (haber / a ver, porque / por qué), el dequeísmo, el leísmo, la concordancia y las erratas, sin tocar tu estilo. Se ejecuta en la CPU con un modelo pequeño entrenado específicamente para esta tarea, y tu texto no sale del ordenador. Para Windows 10/11 y Linux. La interfaz está en castellano.

## Features

- **Works in any app.** No plugins or integrations: revisa reads your selection, shows the changes and pastes the result back into the window you were in. Terminals included.
- **Every change explained.** Each edit gets a type and a fixed explanation from a closed catalog of 31 error types, written and reviewed in advance. The model never makes up grammar.
- **When in doubt, it doesn't touch.** Deterministic guards drop a change, or a whole sentence, when it looks like a rewrite, falls outside the catalog or touches protected text. On the benchmark, revisa changes fewer than 3% of the sentences that were already correct (1.8%<!-- fig:test.revisa.a.fa --> / 2.4%<!-- fig:test.revisa.b.fa --> / 0.9%<!-- fig:test.revisa.c.fa --> on its three parts).
- **The norm, not your style.** It fixes what the norm calls an error. Word choice, tone, register and commas that are a matter of taste are yours.
- **Protects what isn't prose:** URLs, email addresses, @mentions and #hashtags, `code`, file paths, numbers with units and times.
- **Pick what to correct.** Turn off any error type, for example capital letters in chat, or uncheck one change in the popup before applying.
- **Fast on a CPU.** Speculative decoding guided by your own sentence: 138 ms<!-- fig:speed.p50_ms --> per sentence at the median, 2.2×<!-- fig:speed.speedup --> faster than plain decoding and identical output on 99.4%<!-- fig:speed.spec_identical_pct --> of sentences.
- **Really local.** The only network connection is the one-time download of the model (529 MB<!-- fig:model.mb -->), from a URL pinned to a fixed revision and checked with SHA-256. Your text is never sent, logged or written to disk. No automatic updates either.
- **For your tools too:** a CLI (`revisa check`), a local HTTP server (`revisa serve`) and a language server for Markdown, plain text and commit messages (`revisa lsp`).

## Install

Prebuilt packages are in the [latest release](https://github.com/manuelgscom/revisa/releases/latest). Nothing else is needed (no Python, no runtime, no GPU drivers, no administrator rights), except `curl` or `wget` on Linux for the model download, which nearly every desktop has.

| Platform | Package | What you get |
|---|---|---|
| Windows 10 (1903 or later) or 11, x64 | [`revisa-setup.exe`](https://github.com/manuelgscom/revisa/releases/latest/download/revisa-setup.exe) | The tray app and the `revisa` CLI, installed for your user |
| Linux x86_64 | [`revisa-x86_64.AppImage`](https://github.com/manuelgscom/revisa/releases/latest/download/revisa-x86_64.AppImage) | The tray app and the `revisa` CLI, in one file |
| Linux x86_64, terminal only | [`revisa-linux-x86_64.tar.gz`](https://github.com/manuelgscom/revisa/releases/latest/download/revisa-linux-x86_64.tar.gz) | The `revisa` CLI alone, for servers and editors |

**Requirements on both systems:** an x86-64 CPU with AVX2 (most processors from the last decade have it; if yours does not, revisa says so at startup instead of crashing), about 2 GB of free RAM and 529 MB<!-- fig:model.mb --> of disk for the model.

### Windows

1. **Download** [`revisa-setup.exe`](https://github.com/manuelgscom/revisa/releases/latest/download/revisa-setup.exe). To verify it, also download [`SHA256SUMS`](https://github.com/manuelgscom/revisa/releases/latest/download/SHA256SUMS) and run this in PowerShell:

   ```powershell
   cd $HOME\Downloads
   (Get-FileHash .\revisa-setup.exe -Algorithm SHA256).Hash.ToLower() -eq ((Select-String -Path .\SHA256SUMS -Pattern 'revisa-setup.exe').Line -split '\s+')[0]   # must print True
   ```

2. **Run it.** The installer is not code-signed, so SmartScreen shows *Windows protected your PC*. Click **More info**, then **Run anyway**.

   <img src="assets/smartscreen.png" alt="SmartScreen: click More info (left), then Run anyway (right)" width="600">
3. **Choose the options.** revisa installs for your user only, in `%LOCALAPPDATA%\Programs\revisa`, without administrator rights, and adds a *revisa* entry to the Start menu. The installer offers:
   - **Start with Windows** (checked);
   - **Add to PATH**, to use `revisa` from any terminal (unchecked);
   - a desktop shortcut (unchecked).
4. **First start.** revisa asks once to download its model (529 MB<!-- fig:model.mb -->) from Hugging Face. It shows the progress, checks the SHA-256 and keeps it in `%LOCALAPPDATA%\revisa\models`. When the tray icon says *Listo* (ready), it works offline. Windows may tuck the icon behind the ^ arrow of the notification area.
5. **Try it.** Open Notepad, type `Ya e echo el informe.`, select it and press **Ctrl+Alt+Space**, then **Enter**.

**Update:** run the new installer; it closes the running app first. **Uninstall:** *Settings → Apps* (or *Add or remove programs*) *→ revisa → Uninstall*. The model and your settings stay; to remove them too, delete `%LOCALAPPDATA%\revisa` and `%APPDATA%\revisa`.

### Linux

https://github.com/user-attachments/assets/f7a06276-fa89-45eb-a8e6-7b39b9ef0872

*Linux (X11): the same flow in a webmail and a chat in Firefox, and in the Mousepad editor. The real app and model; the input is scripted.*

**Check the requirements.** glibc 2.31 or later. Tested on Ubuntu 22.04 and 24.04, Debian 12, Fedora 45 and Linux Mint 22.2.

```bash
grep -q avx2 /proc/cpuinfo && echo "AVX2: OK" || echo "AVX2: not supported"
ldd --version | head -n 1     # the last number is the glibc version: 2.31 or later
echo "$XDG_SESSION_TYPE"      # x11: the full shortcut flow; wayland: clipboard mode (below)
```

**Install the AppImage:**

```bash
mkdir -p ~/Applications && cd ~/Applications
curl -LO https://github.com/manuelgscom/revisa/releases/latest/download/revisa-x86_64.AppImage
curl -LO https://github.com/manuelgscom/revisa/releases/latest/download/SHA256SUMS
sha256sum -c --ignore-missing SHA256SUMS     # must print: revisa-x86_64.AppImage: OK
chmod +x revisa-x86_64.AppImage
./revisa-x86_64.AppImage
```

- **First start:** revisa asks once to download its model (529 MB<!-- fig:model.mb -->; it needs `curl` or `wget`), checks its SHA-256 and keeps it in `~/.local/share/revisa/models/` (or `$XDG_DATA_HOME/revisa/models/`). From then on it works offline.
- **Start with the system:** in *Ajustes* (Settings), check *Arrancar con el sistema*. It writes `~/.config/autostart/revisa.desktop` pointing to the AppImage, so keep the file where it is.
- **The CLI is inside too.** Link it as `revisa`:

  ```bash
  mkdir -p ~/.local/bin
  ln -s ~/Applications/revisa-x86_64.AppImage ~/.local/bin/revisa
  export PATH="$HOME/.local/bin:$PATH"   # or log out and back in
  revisa --version
  ```

  On Ubuntu and Debian, `~/.local/bin` joins the `PATH` only at login, and only if it already existed; the `export` line covers the current terminal. `./revisa-x86_64.AppImage cli …` works as well.
- **X11 and Wayland.** On X11 desktops the whole flow works: the global shortcut, reading the selection and pasting the result. Wayland does not let applications register global shortcuts, read the selection or simulate keys, so in a Wayland session revisa switches to **clipboard mode**:
  1. copy the text with Ctrl+C;
  2. choose *Corregir el portapapeles* (check the clipboard) in the tray menu, or bind a desktop shortcut to the AppImage with its full path, since shortcuts do not expand `~` (for example `/home/you/Applications/revisa-x86_64.AppImage --check-clipboard`);
  3. review it in the same popup; Enter puts the corrected text in the clipboard (*Copiado: pega con Ctrl+V*), ready to paste.
- **No tray area?** On desktops without one the icon does not show, but the shortcut and `--check-clipboard` still work.
- **AppImage won't start because FUSE is missing** (minimal systems, containers)? Run it as `APPIMAGE_EXTRACT_AND_RUN=1 ./revisa-x86_64.AppImage`.

**Terminal only.** For servers and editors, the CLI is also a single executable (static C++ runtime) that depends only on glibc:

```bash
curl -LO https://github.com/manuelgscom/revisa/releases/latest/download/revisa-linux-x86_64.tar.gz
curl -LO https://github.com/manuelgscom/revisa/releases/latest/download/SHA256SUMS
sha256sum -c --ignore-missing SHA256SUMS     # must print: revisa-linux-x86_64.tar.gz: OK
tar xzf revisa-linux-x86_64.tar.gz
install -Dm755 revisa-*-linux-x86_64/revisa ~/.local/bin/revisa
export PATH="$HOME/.local/bin:$PATH"   # or log out and back in
revisa --version
```

The first `revisa check` in a terminal (not from a pipe) asks to download the model (`[S/n]`: Enter means yes; it needs `curl` or `wget`). If a new terminal does not find `revisa`, log out and back in, or run the `export` line again.

**Uninstall:** quit revisa first (tray menu → *Salir*), then:

```bash
rm -f ~/Applications/revisa-x86_64.AppImage ~/.local/bin/revisa
rm -f ~/.config/autostart/revisa.desktop
rm -rf ~/.local/share/revisa ~/.config/revisa     # model, settings and server tokens
```

## Usage

### In any app

Select text and press **Ctrl+Alt+Space**. The popup opens next to the cursor:

```
╭─────────────────────────────────────────────────────────────────╮
│ revisa   3 correcciones                                      ✕  │
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │ Ya ~~e~~ he ~~echo~~ hecho el informe y creo ~~de~~ que te  │ │
│ │ va a gustar.                                                │ │
│ └─────────────────────────────────────────────────────────────┘ │
│ ☑ e → he                                           Verbo haber  │
│   Aquí va «he», del verbo «haber» (como en «he venido» o        │
│   «ha dicho»).                                                  │
│ ☑ echo → hecho                                    Echo / hecho  │
│   «hecho» es de «hacer» («he hecho»); «echo», de «echar»        │
│   («te echo de menos»).                                         │
│ ☑ Quitar de                                          Dequeísmo  │
│   Dequeísmo: aquí sobra «de» delante de «que» («creo que»).     │
│ ─────────────────────────────────────────────────────────────── │
│ ↵ Aplicar   Esc Cancelar                  Cancelar   [Aplicar]  │
╰─────────────────────────────────────────────────────────────────╯
```

- **Enter** applies: revisa goes back to your window, pastes the corrected text over the selection and restores your clipboard.
- **Esc** cancels.
- **Click a change** to uncheck it; it is left out of the pasted text.
- When there is nothing to fix, a short *Sin errores* (no errors) notice appears instead.
- In terminals, revisa pastes with Ctrl+Shift+V.

The tray menu has *Corregir el portapapeles* (check the clipboard), *Pausar* (pause), *Ajustes* (settings), *Acerca de* (versions, licenses and the model's SHA-256) and *Salir* (quit). In **Ajustes** you can change the shortcut, choose which error types to correct, set the number of CPU threads and start revisa with the system. The popup and the settings follow your desktop's light or dark theme.

### In the terminal

`revisa check` corrects files, or the standard input, and writes the corrected text to standard output. It never modifies your files.

```console
$ cat correo.txt
Mañana te envío el presupuesto, haber si lo cerramos esta semana. Ya e echo el informe y creo de que te va a gustar.

$ revisa check correo.txt
Mañana te envío el presupuesto, a ver si lo cerramos esta semana. Ya he hecho el informe y creo que te va a gustar.

$ revisa check --json correo.txt | jq -r '.edits[] | "«\(.from)» → «\(.to)»: \(.explanation)"'
«haber» → «a ver»: «a ver» es la preposición «a» con el verbo «ver» («a ver si vienes»); «haber» es el verbo «haber» («va a haber fiesta») o el sustantivo («en su haber»).
«e» → «he»: Aquí va «he», del verbo «haber» (como en «he venido» o «ha dicho»).
«echo» → «hecho»: «hecho» es de «hacer» («he hecho»); «echo», de «echar» («te echo de menos»).
«de » → «»: Dequeísmo: aquí sobra «de» delante de «que» («creo que»).
```

```bash
revisa check FILE…                 # corrected text; with several files, each after "==> FILE <=="
echo "texto" | revisa check        # from the standard input
revisa check --json FILE           # one JSON object per input: corrected text and edits
revisa check --jsonl < cases.jsonl # batch mode: {"id", "text"} per line in, one result per line out
revisa diff ORIGINAL CORRECTED     # the edits between two texts, with type and explanation (no model)
revisa --help
revisa --version
```

The exit code is 0 when nothing changes, 1 when something does and 2 on an error, so scripts and Git hooks can use it. Each edit in `--json` has `start` and `end` (UTF-16 offsets into your text), `from`, `to`, `type` and `explanation`. `--model PATH` uses another GGUF file and `--threads N` sets the CPU threads (by default, the performance cores on hybrid CPUs and all physical cores on the rest).

### In your editor

`revisa lsp` is a language server over stdin and stdout. It checks Markdown, plain text and Git commit messages, publishes each error as a diagnostic with its explanation and offers a quick fix per change (and *Aplicar todas en este párrafo* when a paragraph has several). In Markdown it skips code, link URLs, HTML and front matter. The model must already be downloaded: run `revisa check` on any file once.

**Neovim** (0.11 or later), in `init.lua`:

```lua
vim.lsp.config('revisa', {
  cmd = { 'revisa', 'lsp' },
  filetypes = { 'markdown', 'text', 'gitcommit' },
  root_markers = { '.git' },
})
vim.lsp.enable('revisa')
```

**Helix**, in `~/.config/helix/languages.toml`:

```toml
[language-server.revisa]
command = "revisa"
args = ["lsp"]

[[language]]
name = "markdown"
language-servers = ["marksman", "revisa"]   # keep Helix's default Markdown server

[[language]]
name = "git-commit"
language-servers = ["revisa"]
```

[`src/lsp/EDITORS.md`](src/lsp/EDITORS.md) has the full guide: plain-text files in Helix, keys to jump between errors and apply fixes, and troubleshooting.

### From your own programs

`revisa serve` is a local HTTP server for scripts and tools. It listens only on `127.0.0.1` (port 47312 by default) and every check needs a token:

```bash
TOKEN=$(revisa token new my-script)   # the token is shown only once; only its SHA-256 is stored
revisa serve &                        # or in another terminal; prints: revisa: escuchando en http://127.0.0.1:47312

curl -s -H "Authorization: Bearer $TOKEN" \
     -d '{"text": "Ya e echo el informe."}' \
     http://127.0.0.1:47312/v1/check
```

The response has the same shape as `revisa check --json`. `GET /v1/health` needs no token. The server checks the `Host` header (against DNS rebinding), rejects requests from web pages (`Origin: http(s)://…`), accepts up to 20,000 characters per request and never logs your text. `revisa token list` and `revisa token revoke NAME` manage the tokens; a revoked token stops working at once.

### Configuration

An optional `key = value` file: `~/.config/revisa/config` on Linux (or `$XDG_CONFIG_HOME/revisa/config`) and `%APPDATA%\revisa\config` on Windows. The tray app writes it from *Ajustes* and keeps your comments; the CLI only reads it.

```ini
# global shortcut of the tray app
hotkey = Ctrl+Alt+Space
# error types that are never corrected (ids in "What it corrects")
disabled_types = MAYUSCULA, ESPACIADO
# inference threads (1-256; by default, chosen by revisa)
threads = 4
# port of revisa serve
port = 47312
# a GGUF file instead of the downloaded model
# model = /path/to/revisa-0.8b-Q4_K_M.gguf
```

### What it corrects

31 error types in a closed catalog. Each has a stable id (for `disabled_types`), the label shown in the popup and a fixed explanation in Spanish.

<details>
<summary><b>The 31 error types, with examples</b></summary>

| Id | Label in the popup | Example |
|---|---|---|
| `ESPACIADO` | Espaciado | Gracias ,nos vemos → Gracias, nos vemos |
| `MAYUSCULA` | Mayúscula | nos vemos el Lunes → nos vemos el lunes |
| `TILDE_DIACRITICA` | Tilde diacrítica | no se nada → no sé nada |
| `TILDE_INTERROGATIVA` | Tilde interrogativa | no sé cuando viene → no sé cuándo viene |
| `POR_QUE` | Porque / por qué | ¿porque no vienes? → ¿por qué no vienes? |
| `TILDE_FALTA` | Falta tilde | despues → después |
| `TILDE_SOBRA` | Sobra tilde | fué → fue |
| `HABER_A_VER` | Haber / a ver | haber si vienes → a ver si vienes |
| `VERBO_HABER_H` | Verbo haber | ya e terminado → ya he terminado |
| `ECHO_HECHO` | Echo / hecho | lo he echo → lo he hecho |
| `HAY_AHI_AY` | Hay / ahí / ay | ahí que ir → hay que ir |
| `SINO_SI_NO` | Sino / si no | no lo hizo él, si no ella → no lo hizo él, sino ella |
| `JUNTO_SEPARADO` | Junto o separado | derrepente → de repente |
| `B_V` | B o V | iva a llegar → iba a llegar |
| `G_J` | G o J | cojer → coger |
| `H` | Con o sin h | aora → ahora |
| `LL_Y` | LL o Y | callendo → cayendo |
| `C_Z_S_X` | C, Z, S o X | haser → hacer |
| `SIGNO_APERTURA` | Signo de apertura | Qué tal? → ¿Qué tal? |
| `ERRATA` | Errata | infrome → informe |
| `DEQUEISMO` | Dequeísmo | creo de que → creo que |
| `QUEISMO` | Queísmo | me acuerdo que vino → me acuerdo de que vino |
| `LEISMO` | Leísmo | a María le vi → a María la vi |
| `LAISMO` | Laísmo | la dije que viniera → le dije que viniera |
| `LOISMO` | Loísmo | los di un regalo → les di un regalo |
| `HABER_IMPERSONAL` | Haber impersonal | habían muchas personas → había muchas personas |
| `IMPERATIVO` | Imperativo | callaros → callaos |
| `PRETERITO_S` | Pretérito sin -s | dijistes → dijiste |
| `CONJUGACION` | Conjugación | haiga → haya |
| `CONCORDANCIA` | Concordancia | los problema → los problemas |
| `COMA_SOBRA` | Coma de más | Los clientes, recibirán el pedido → Los clientes recibirán el pedido |

**Not corrected, by design:** added commas (vocatives, asides, before *pero*), a missing final period, quotation-mark and dash styles, the format of numbers, times and dates, synonyms and word order, loanwords, all-caps emphasis, and forms the RAE accepts: personal masculine *leísmo* (*le vi*, about a man), *solo* and *este* with or without an accent, *enseguida* / *en seguida*, *quizá* / *quizás*, *voseo* and *ustedes* for *vosotros*.

</details>

## How it works

```
 your text
   │
   ▼
 1 split into sentences ──► 2 protect URLs, emails, @mentions, code, numbers…
                                  │
                                  ▼
 3 revisa-0.8b (llama.cpp, CPU) rewrites each sentence, corrected
   · speculative decoding: the draft is your own sentence
                                  │
                                  ▼
 4 word diff ──► exact edits ──► 5 rule-based classifier ──► type + fixed explanation
                                                                  │
                                                                  ▼
 6 guards: drop doubtful edits, or the whole sentence ──► disabled types out
                                  │
                                  ▼
 corrected text + edits [{start, end, from, to, type, explanation}]
```

1. **Sentences.** The text is split into sentences by a splitter that knows abbreviations, numbers and URLs.
2. **Protection.** Spans that are not prose are marked as protected; a sentence that is all protected never reaches the model.
3. **The model rewrites; it does not explain.** `revisa-0.8b` receives one sentence and returns it corrected, in plain text, or unchanged. The prompt is minimal, with no examples, and is identical byte for byte in training and in the app. The state of its fixed prefix is computed once and restored for every sentence.

   *Speculative decoding guided by the input.* A corrected sentence is almost always your sentence, so your sentence is the draft. The model checks it in one batch, keeps the longest prefix it agrees with and resynchronizes after each change. A correct sentence takes a single verification pass. The output matches plain greedy decoding in 99.4%<!-- fig:speed.spec_identical_pct --> of a fixed sample of 500 development cases, with the same quality.
4. **Word diff.** A Myers diff over words turns the two sentences into exact edits.
5. **Classifier.** Ordered rules, closed word lists (homophones, diacritic pairs, verb forms…) and a Hunspell `es_ES` dictionary give each edit one of the 31 types and its explanation. An edit no rule recognizes counts as unknown.
6. **Guards: when in doubt, don't touch.** A change outside the catalog (an added comma, quotes, style) is dropped. A sentence whose output is invalid, rewrites too much or contains an unknown edit is left unchanged. The guard thresholds were calibrated on the development set, never on the test. Last, the edits of the error types you turned off are removed.

Recently corrected sentences are kept in a small in-memory cache for the editor and the server; nothing is written to disk.

## Results

revisa was measured on **revisa-bench-es**, a new public Spanish benchmark frozen before evaluation: real errors by native speakers (parts A and B) plus a synthetic part in the register revisa is made for (C), 2,388<!-- fig:bench.total --> cases in all, each part reported on its own. It mixes sentences with errors and sentences that are already correct.

- **A · Wikipedia edits** (917<!-- fig:bench.a.n --> cases): sentences from Spanish Wikipedia articles before and after a real correction by an editor.
- **B · Wikipedia talk pages** (823<!-- fig:bench.b.n --> cases): informal comments written quickly by native speakers.
- **C · Emails and chat** (648<!-- fig:bench.c.n --> cases): synthetic work emails, team chats and instant messages, the register revisa is made for.

**F0.5** scores each edit and weighs precision twice as much as recall. **False alarms** are the share of already-correct sentences that a system changes. Higher F0.5 and lower false alarms are better. The general models get revisa's instructions plus the benchmark's correction criterion (no examples), and they are scored after the same deterministic guards as revisa, which helps them too.

| System | A · Wikipedia edits | B · Talk pages | C · Emails and chat |
|---|---|---|---|
| **revisa-0.8b** (fine-tuned, 0.8B, CPU) | **81.5**<!-- fig:test.revisa.a.f05 --> · 1.8%<!-- fig:test.revisa.a.fa --> | **80.0**<!-- fig:test.revisa.b.f05 --> · 2.4%<!-- fig:test.revisa.b.fa --> | **92.0**<!-- fig:test.revisa.c.f05 --> · 0.9%<!-- fig:test.revisa.c.fa --> |
| Qwen3.5-9B, untuned | 79.8<!-- fig:test.qwen35_9b.a.f05 --> · 4.3%<!-- fig:test.qwen35_9b.a.fa --> | 69.6<!-- fig:test.qwen35_9b.b.f05 --> · 3.8%<!-- fig:test.qwen35_9b.b.fa --> | 91.2<!-- fig:test.qwen35_9b.c.f05 --> · 0.6%<!-- fig:test.qwen35_9b.c.fa --> |
| Qwen3.5-0.8B, untuned (revisa's base) | 70.4<!-- fig:test.qwen35_08b.a.f05 --> · 2.4%<!-- fig:test.qwen35_08b.a.fa --> | 67.9<!-- fig:test.qwen35_08b.b.f05 --> · 2.0%<!-- fig:test.qwen35_08b.b.fa --> | 71.2<!-- fig:test.qwen35_08b.c.f05 --> · 0.9%<!-- fig:test.qwen35_08b.c.fa --> |
| LanguageTool 6.6 | 24.1<!-- fig:test.languagetool.a.f05 --> · 39.0%<!-- fig:test.languagetool.a.fa --> | 52.8<!-- fig:test.languagetool.b.f05 --> · 25.1%<!-- fig:test.languagetool.b.fa --> | 74.1<!-- fig:test.languagetool.c.f05 --> · 11.1%<!-- fig:test.languagetool.c.fa --> |

*Each cell: F0.5 · false alarms.*

<p align="center">
  <img src="assets/results-f05.png" alt="F0.5 per benchmark part: revisa-0.8b, Qwen3.5-9B untuned, Qwen3.5-0.8B untuned and LanguageTool 6.6" width="49%">
  <img src="assets/results-false-alarms.png" alt="False alarms per benchmark part for the same systems" width="49%">
</p>

revisa beats LanguageTool and an untuned model eleven times its size on all three parts in F0.5. Fine-tuning is what makes the difference: its own base model, Qwen3.5-0.8B, averages 69.9<!-- fig:test.qwen35_08b.mean_f05 --> F0.5, against 84.5<!-- fig:test.revisa.mean_f05 --> for revisa. Larger untuned models do win on some parts: Qwen3.5-4B on A (83.6<!-- fig:test.qwen35_4b.a.f05 -->) and Gemma 4 E4B on C (95.1<!-- fig:test.gemma4_e4b.c.f05 -->). None of them reaches revisa's mean, though (the best, Gemma 4 E4B, averages 82.8<!-- fig:test.gemma4_e4b.mean_f05 -->), and they are too slow for proofreading as you type on a CPU.

<p align="center">
  <img src="assets/size-vs-f05.png" alt="Mean F0.5 against model size: revisa-0.8b at 84.5, above untuned Qwen3.5 from 0.8B to 9B, Gemma 4 E2B and E4B, and LanguageTool 6.6" width="80%">
</p>

**Speed.** Engine time per sentence on a desktop CPU with 8<!-- fig:speed.threads --> threads, model loading excluded, on parts A and C of a fixed sample of 500 development cases: **138 ms**<!-- fig:speed.p50_ms --> at the median and 322 ms<!-- fig:speed.p95_ms --> at the 95th percentile, against 305 ms<!-- fig:speed.greedy_p50_ms --> at the median without speculative decoding.

<p align="center">
  <img src="assets/speed.png" alt="Latency per sentence on CPU: 138 ms at the median with speculative decoding, against 305 ms with greedy decoding" width="80%">
</p>

The [benchmark card](https://huggingface.co/datasets/manuelgscom/revisa-bench-es) has the data, the scoring script, how the reference corrections were built and the caveats. The [model card](https://huggingface.co/manuelgscom/revisa-0.8b-GGUF) explains the training and how to run the model with llama.cpp directly.

## Build from source

Use this to read or change the code; for everyday use, the [packages](#install) are simpler. llama.cpp, doctest and cpp-httplib are git submodules and are compiled along with revisa.

### 1. Get the code

```bash
git clone --recursive https://github.com/manuelgscom/revisa.git
cd revisa
```

If you cloned without `--recursive`, run `git submodule update --init`.

### 2. Linux: the CLI

You need git, CMake 3.28 or later and a C++20 compiler (GCC 13 builds the releases).

| Distribution | Command |
|---|---|
| Ubuntu 24.04 or later, Linux Mint 22 or later | `sudo apt install build-essential cmake git` |
| Fedora | `sudo dnf install gcc-c++ make cmake git` |

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DREVISA_TEST_HOOKS=OFF
cmake --build build -j"$(nproc)" --target revisa
install -Dm755 build/revisa ~/.local/bin/revisa
revisa --version
```

The build targets AVX2, FMA and F16C, like the releases. `-DREVISA_NATIVE=ON` optimizes llama.cpp for the CPU of the machine where you build instead.

### 3. Linux: the tray app

It also needs Qt 6.8 or later (Core, Gui and Widgets) and the X11 headers. Debian 13, Ubuntu 25.10 or later and Fedora 41 or later ship a recent enough Qt:

| Distribution | Command |
|---|---|
| Debian 13, Ubuntu 25.10 or later | `sudo apt install qt6-base-dev libx11-dev` |
| Fedora 41 or later | `sudo dnf install qt6-qtbase-devel libX11-devel` |

On older releases (Ubuntu 24.04 ships Qt 6.4), install Qt 6.8 in your home with [aqtinstall](https://github.com/miurahr/aqtinstall), for example `aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O ~/Qt`, and add `-DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64` to the first command below.

```bash
cmake -B build-gui -DCMAKE_BUILD_TYPE=Release -DREVISA_TEST_HOOKS=OFF -DREVISA_GUI=ON
cmake --build build-gui -j"$(nproc)" --target revisa revisa-tray
./build-gui/revisa-tray
```

At run time it loads `libXtst.so.6` (XTest) to simulate Ctrl+C and Ctrl+V; desktops almost always have it.

### 4. Windows

With Visual Studio 2022 (the C++ desktop tools), CMake 3.28 or later, Ninja and Qt 6.8 for MSVC 2022 64-bit (`aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt`), from an *x64 Native Tools Command Prompt for VS 2022*:

```bat
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DREVISA_TEST_HOOKS=OFF -DREVISA_GUI=ON -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build --target revisa revisa-tray
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe --release build\revisa-tray.exe
```

### 5. Tests

```bash
cmake -B build-dev                    # Debug, with the test hooks
cmake --build build-dev -j"$(nproc)"
ctest --test-dir build-dev
```

- `ctest` runs the doctest unit tests, the JSONL cases of the segmenter, the protector, the classifier (one file per error type, which must pass at 100%) and the guards, the golden prompt files and end-to-end tests of the CLI. A fake engine stands in for the model, so they need no model file.
- The `model` suite runs the real model and is skipped, with a warning, without it: `REVISA_TEST_MODEL=/path/to/revisa-0.8b-Q4_K_M.gguf ctest --test-dir build-dev -L model`.
- The tray tests need a development build with the tray, and run without a display:

  ```bash
  cmake -B build-gui-dev -DREVISA_GUI=ON      # plus -DCMAKE_PREFIX_PATH if Qt is in your home
  cmake --build build-gui-dev -j"$(nproc)"
  QT_QPA_PLATFORM=offscreen ctest --test-dir build-gui-dev -R tray
  ```

### Repository layout

```
CMakeLists.txt
cmake/           embeds the catalog data in the binary
src/
  main.cpp       CLI entry point (refuses CPUs without AVX2)
  core/          the engine library (C++20, no Qt, no HTTP)
    text/        UTF-8, NFC, UTF-16 offsets
    segment/     sentence splitter
    protect/     protected spans (URLs, emails, code…)
    infer/       llama.cpp engine: prefix state, speculative decoding, sentence cache, prompt
    diff/        word diff
    classify/    catalog rules, syllable splitting for the accent rules
    catalog/     closed lists, explanations, Hunspell es_ES
    guard/       the guards and their thresholds
    check/       the whole pipeline, shared by the CLI, the server, the LSP and the tray
    queue/       single inference thread with cancellation
    tokens/      server tokens
    fetch/       model download and SHA-256
    config/      configuration file and paths per system
  cli/           revisa check | diff | classify | segment | serve | lsp | token
  server/        HTTP server of revisa serve (cpp-httplib)
  lsp/           language server (JSON-RPC over stdio), Markdown mask, EDITORS.md
  tray/          Qt 6 tray app; platform/ holds X11, Wayland (clipboard mode) and Win32
  win/           Windows icon, manifest and resources
tests/           doctest unit tests, JSONL cases, golden files, end-to-end tests
third_party/     llama.cpp, doctest, cpp-httplib (git submodules)
assets/          images of this README
```

## Limitations

- **Spanish only**, and the interface is in Spanish.
- **One sentence at a time.** Each sentence is corrected without the ones around it, so errors that depend on wider context (a pronoun whose referent is in the previous sentence) can slip through.
- **The norm, not style.** It does not rephrase, add commas of taste or change your vocabulary. It may also miss errors: it is built to stay quiet rather than risk a wrong change, so a clean popup is not a guarantee.
- **The model can be wrong.** Read the changes before pressing Enter; you can uncheck any of them.
- **Windows:** the shortcut does not reach windows running as administrator (Windows does not let a normal app send them keys). The installer is not code-signed, hence the SmartScreen warning. Tested on Windows 11.
- **Linux:** the full shortcut flow needs X11; Wayland sessions use clipboard mode.
- **x86-64 with AVX2 only.** No macOS and no ARM builds.
- **Long texts:** the popup and the server take up to 20,000 characters at a time, and very long sentences without punctuation are skipped.

## License

- **Code:** [MIT](LICENSE), except `src/core/catalog/dictionary.cpp`, a port of Hunspell's lookup code that keeps Hunspell's MPL 1.1 / GPL 2.0 / LGPL 2.1 tri-license.
- **Model** ([`revisa-0.8b`](https://huggingface.co/manuelgscom/revisa-0.8b-GGUF)): Apache-2.0, like its base model, Qwen3.5-0.8B.
- **Benchmark** ([`revisa-bench-es`](https://huggingface.co/datasets/manuelgscom/revisa-bench-es)): CC BY-SA 4.0, since it derives from Wikipedia, with attribution per revision.
- **Third parties:** llama.cpp/ggml, cpp-httplib, nlohmann/json, the Spanish Hunspell dictionary `es_ES` (RLA-ES), Qt 6 (LGPLv3, dynamically linked: the Qt libraries can be replaced) and others. Their licenses and notices are in [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES); doctest is used only by the tests.

---

Made by [Manuel García Sanz](https://manuelgs.com/en/).
