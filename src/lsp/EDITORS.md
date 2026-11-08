# Using revisa in your editor

`revisa lsp` is a language server (LSP over stdin/stdout). It checks Spanish spelling and grammar in
Markdown, plain text and Git commit messages, and gives your editor:

- **Diagnostics** (severity *Information*, source `revisa`), with the error type as the code and a
  short explanation in Spanish as the message.
- **Quick fixes**: one code action per correction (for example «Cambiar a «después»») and, when a
  paragraph has two or more, «Aplicar todas en este párrafo».

Everything runs locally: the model is loaded inside the `revisa lsp` process and no text leaves
your machine. A document is checked 300 ms after you stop typing, and only the sentences that
changed are sent to the model again. In Markdown, code, link URLs, HTML and front matter are
ignored. Only Spanish is supported.

## Before you start

1. `revisa` must be on your `PATH` (the editor runs `revisa lsp`).
2. The model must already be on disk. `revisa lsp` cannot ask for the download, so check any
   file once in a terminal and accept it:

   ```sh
   revisa check notes.md
   ```

   If you keep the model elsewhere, set `model = /path/to/model.gguf` in
   `~/.config/revisa/config`, or add `"--model", "/path/to/model.gguf"` to the server command.

## Neovim (0.11 or later)

Add to your `init.lua`:

```lua
vim.lsp.config('revisa', {
  cmd = { 'revisa', 'lsp' },
  filetypes = { 'markdown', 'text', 'gitcommit' },
  root_markers = { '.git' },
})
vim.lsp.enable('revisa')

-- Optional: show the explanations at the end of the line.
vim.diagnostic.config({ virtual_text = true })
```

No extra mapping of language ids is needed: the server accepts Neovim's `text` and `gitcommit`
as they are. Files outside a Git repository work too.

- **See the errors:** `]d` and `[d` jump between them, `<C-w>d` opens the explanation under the
  cursor, and `:lua vim.diagnostic.setloclist()` lists them all.
- **Apply a fix:** on an error, press `gra` (or run `:lua vim.lsp.buf.code_action()`) and pick
  the change.

## Helix

Add to `~/.config/helix/languages.toml`:

```toml
[language-server.revisa]
command = "revisa"
args = ["lsp"]

[[language]]
name = "markdown"
language-servers = ["revisa"]

[[language]]
name = "git-commit"
language-servers = ["revisa"]

# Helix has no plain-text language: this one adds it for .txt files.
[[language]]
name = "text"
scope = "text.plain"
file-types = ["txt"]
language-id = "plaintext"
language-servers = ["revisa"]
```

`language-servers` replaces the default list for that language. If you also use another server
(for example `marksman` for Markdown), keep it: `language-servers = ["marksman", "revisa"]`.

Check the setup with `hx --health markdown`: `revisa` must appear with a tick under
*Configured language servers*.

- **See the errors:** they are marked in the gutter; `]d` and `[d` jump between them and
  `space d` opens the list.
- **Apply a fix:** on an error, press `space a` and pick the change.

## Troubleshooting

- **No diagnostics at all.** Check a file with `revisa check` once in a terminal (see above).
  Without a model, `revisa lsp` writes the reason to its standard error and exits.
- **Where the server's messages go.** The editor collects the server's standard error:
  - Neovim: `:checkhealth vim.lsp` shows whether `revisa` is attached, and
    `:lua vim.cmd.edit(vim.lsp.get_log_path())` opens the log.
  - Helix: start it with `hx -v` and run `:log-open`.
- **"revisa no ha podido cargar el modelo".** The model file is damaged or is not a revisa model.
  Check the `model` path in `~/.config/revisa/config`, or delete the file and run `revisa check`
  again to download it.
- **Nothing in some files.** Only Markdown, plain text and Git commit messages are checked. In
  Neovim, `:set filetype?` must be `markdown`, `text` or `gitcommit`.
