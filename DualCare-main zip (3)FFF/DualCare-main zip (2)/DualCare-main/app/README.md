# IM-WHS

## Prerequisites

- your brain & reading skills
- `uv` (python manager)

### installing `uv`

1. open powershell
2. paste this into the powershell

   ```powershell
   powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"
   ```

3. quit the powershell
4. open the powershell again and run

   ```powershell
   uv --version

   # you should see something like
   # uv --version
   # uv 0.11.x (x86_64-unknown-linux-gnu) # some windows architecture
   ```

## Run the app

### uv

first do:

```bash
uv sync
```

Run as a desktop app:

```bash
uv run flet run
```

Run as a web app:

```bash
uv run flet run --web
```

For more details on running the app, refer to the [Getting Started Guide](https://docs.flet.dev/).

## Build the app

### Android

```bash
flet build apk -v
```

For more details on building and signing `.apk` or `.aab`, refer to the [Android Packaging Guide](https://docs.flet.dev/publish/android/).

### Linux

```bash
flet build linux -v
```

For more details on building Linux package, refer to the [Linux Packaging Guide](https://docs.flet.dev/publish/linux/).

### Windows

```bash
flet build windows -v
```

For more details on building Windows package, refer to the [Windows Packaging Guide](https://docs.flet.dev/publish/windows/).
