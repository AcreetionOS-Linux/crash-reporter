# crash-reporter

A utility for collecting crash logs and other system information to assist with debugging.

## Installation

### Arch Linux

Install the `crash-reporter` package from the AUR.

```bash
yay -S crash-reporter
```

### From source

Clone the repository and build the project:

```bash
git clone https://github.com/your-username/crash-reporter.git
cd crash-reporter
make -f Makefile.crash_reporter
```

## Usage

Run the `crash-reporter` executable:

```bash
./crash-reporter
```

This will generate a report file that you can share with developers.

## Building

To build the project, you need to have `gcc` and `make` installed.

Clone the repository and run `make`:

```bash
git clone https://github.com/your-username/crash-reporter.git
cd crash-reporter
make -f Makefile.crash_reporter
```

## Contributing

Contributions are welcome! Please open an issue or submit a pull request.