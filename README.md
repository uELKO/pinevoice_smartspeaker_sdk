# PineVoice SmartSpeaker SDK

This repository contains source code for Pine64's PineVoice. It is based on Bouffalo Lab's downstream fork of AliOS/YoC/YoCop.

# Architecture

PineVoice's CPU - BL606P, contains three RISC-V cores. We use only two of them, E907 (M0) and C906 (D0). Because of this, this project have two main projects:

- `solutions/pinevoice_fw_e907` - Contains firmware for main processor. It handles WiFi, BT, audio and Wyoming satellite.
- `solutions/pinevoice_fw_c906` - Contains firmware for algorithm/DSP processor. It's sole purpose is to run algorithms/processing. So it handles local Wake Word detection.

# Development

## Downloading and cloning

Clone the repository with all submodules:

```sh
git clone --recursive https://github.com/pine64/pinevoice_smartspeaker_sdk.git
cd pinevoice_smartspeaker_sdk
```

For an existing checkout:

```sh
git submodule update --init --recursive
```

Download the Bouffalo Dev Cube flash tool into the repository:

```sh
mkdir -p tools/flashtool
curl -fL http://files.pine64.org/tools/bouffalo/bflb_flashtool_bl606p_v190.tar.gz \
  -o /tmp/bflb_flashtool_bl606p_v190.tar.gz
tar xzf /tmp/bflb_flashtool_bl606p_v190.tar.gz -C tools/flashtool
chmod +x tools/flashtool/bflb_iot_tool-ubuntu
```

The flash-tool files are ignored by Git, so this step is needed for every new checkout or build environment.

## Preparing the environment

Docker is recommended. The YoC tools use an old SCons release, which has compatibility problems with current Python versions. The supplied Dockerfile uses Ubuntu 22.04 and avoids this issue.

### VS Code Dev Container

Open `pinevoice-sdk.code-workspace` in VS Code and select **Reopen in Container**.

The Dev Container mounts the repository at `/workspace` and installs the BL606P toolchain. The flash tool must still be downloaded into `tools/flashtool/` as described above.

To flash from the Dev Container, expose the PineVoice serial device in `.devcontainer/devcontainer.json`:

```json
"runArgs": [
  "--device=/dev/ttyACM0"
]
```

Replace `/dev/ttyACM0` with the device shown on the host:

```sh
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

Rebuild or reopen the Dev Container after changing `devcontainer.json`. Add another `--device` entry for each additional serial device.

### Docker from the host

Build the image from the repository root:

```sh
docker build -t pinevoice-sdk -f .devcontainer/Dockerfile .
```

Run it with the repository mounted:

```sh
docker run --rm -it --name pinevoice-sdk-build \
  -v "$PWD:/workspace" \
  -w /workspace \
  pinevoice-sdk bash
```

If the container will be used for flashing, add the serial device:

```sh
docker run --rm -it --name pinevoice-sdk-build \
  --device=/dev/ttyACM0 \
  -v "$PWD:/workspace" \
  -w /workspace \
  pinevoice-sdk bash
```

The `--device` option is required for flashing or using `tio` from Docker. It is not needed for compilation.

## Native build setup

Docker is preferred, but the project can also be built directly on the host.

Install the native dependencies on Debian or Ubuntu:

```sh
sudo apt-get update
sudo apt-get install -y build-essential git clang curl ca-certificates \
  tio zip bash make tar xxd bear python3 python3-pip python3-venv
```

Use Python 3.11 for YoC and SCons. 
It is recommended to use native python3.11 and pip. Using `uv` instead caused issues with scons.

```sh
python3.11 -m venv .venv
.venv/bin/pip install yoctools==2.0.40 intelhex prettytable \
  DecryptLogin selenium disutils scons
```


Download the BL606P toolchain into the repository:

```sh
mkdir -p toolchain
curl -fL http://files.pine64.org/tools/bouffalo/bl606p_toolchain.tar.gz \
  | tar xz -C toolchain
```

Add the tools to the current shell:

```sh
export PATH="$PWD/.venv/bin:$PWD/toolchain/bin:$PATH"
mkdir -p "$HOME/.local/bin"
export PATH="$HOME/.local/bin:$PATH"
```

## Compilation

To compile the complete project and create the release archive, run `package.sh` from the repository root:

```sh
./package.sh
```

The script builds C906, builds E907, generates the filesystem and flash images, and creates `firmware_<commit>.zip`.

Build outputs include:

- `solutions/pinevoice_fw_c906/yoc.bin`
- `solutions/pinevoice_fw_e907/yoc.bin`
- `solutions/pinevoice_fw_e907/yoc_rfpa.bin`
- `solutions/pinevoice_fw_e907/generated/`
- `firmware_<commit>.zip`

## Development

For iterative development, build C906 once and then use the shorter E907 build:

```sh
cd solutions/pinevoice_fw_c906
./go
cd ../pinevoice_fw_e907
./build.sh
```

Run `solutions/pinevoice_fw_e907/go` instead of `build.sh` when a full E907 rebuild and image-generation step is needed.

### Flashing during development

The flash script is in `solutions/pinevoice_fw_e907/`. It uses the first character device under `/dev` whose name starts with `tty`. If multiple serial devices are connected, adjust/hard-code `PV_TTY` in `flash.sh`.

Check the serial device:

```sh
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

Before running the flash command, turn the PineVoice off, hold the center ring button, and turn it on. The bootloader timeout is short, so act quickly.

`flash.sh` arguments:

```sh
./flash.sh <cli> <full>
```

- `cli` (optional) - when set to `cli`, it will automatically open `tio` terminal. When set to `-`, this option is skipped
- `full` (optional) - when set to `full`, it will flash media partition as well.

# License

AliOS/Xuantie-RTOS/Yocop is using Apache 2.0 license. Provided code credits goes to their respective owners: Alibaba/AliOS, Bouffalo Lab.

Code made by community for PineVoice is licensed with Apache 2.0 or MIT.
