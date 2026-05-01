# NanoOS

**NanoOS** is a tiny pseudo-Linux shell for the **Arduino Nano / ATmega328P**.


```text
NanoOS v0.5.1
Tiny pseudo-Linux shell for Arduino Nano
```
```text
2 KB RAM
1 KB internal EEPROM disk
optional external I2C EEPROM disk
serial shell
line editor
scripts
RTC tools
I2C tools
pin control
```

NanoOS is not a real operating system. It is a compact Arduino sketch that turns an Arduino Nano into a tiny retro-style interactive computer.
It provides a serial terminal, a small EEPROM-backed file system, a line-based editor, script execution, simple loop handling, RTC/DS3231 tools, I2C scanning, pin control, and support for both internal and external EEPROM storage.
The story behind it: I just wanted a script to communicate directly with my RTC via serial. But then things got a little out of hand ;)

## Features

- Serial Monitor shell with `nanoos>` prompt
- System commands: `help`, `uptime`, `free`, `pwd`, `echo`, `sleep`
- I2C scanner
- Digital pin read/write commands
- LED/pin blinking
- Internal EEPROM file system
- Optional external I2C EEPROM file system
- Mount switching between internal and external EEPROM
- File commands: `ls`, `new`, `edit`, `save`, `load`, `cat`, `rm`, `df`
- Raw EEPROM read/write commands
- Copy/move between internal and external EEPROM
- Line-based text editor
- RTC commands for DS3231 / DS1307-style RTC modules
- DS3231 temperature readout
- DS3231 square-wave output configuration
- DS3231 Alarm 1 configuration
- Script execution with `run <file>`
- Script comments and shebang support
- Simple script loops with `loop start N` / `loop end`

---

## Tested / Target Hardware

Primary target:

```text
Arduino Nano
ATmega328P
16 MHz
2 KB SRAM
32 KB Flash
1 KB internal EEPROM
```

Likely compatible with:

- Arduino Uno
- Other ATmega328P boards

Some pin numbering or bootloader settings may differ between boards.

---

## Optional Hardware

NanoOS can run with only an Arduino Nano and the Serial Monitor. Additional modules enable more commands.

### DS3231 RTC module

Recommended for:

- `date`
- `rtcget`
- `rtcset`
- `temp`
- `sqw`
- `alarm`

Typical I2C address:

```text
0x68
```

### External I2C EEPROM

Recommended type:

```text
24LC256-compatible I2C EEPROM
32 KB
I2C address 0x50
```

Used by:

- `mount ext`
- `mkfs ext`
- external EEPROM file system
- `cp int ext <file>`
- `mv int ext <file>`
- `eeread ext ...`
- `eewrite ext ...`

### I2C LCD

Not yet implemented as a full UI in v0.5.1, but I2C LCD devices are detected by `i2cscan` at common addresses such as:

```text
0x27
0x3F
```

---

## Wiring

### I2C bus on Arduino Nano

```text
A4 = SDA
A5 = SCL
5V = VCC
GND = GND
```

Most DS3231 and 24LC256 modules use I2C and can share the same bus.

Typical setup:

```text
Arduino Nano A4  -> SDA on RTC / EEPROM / LCD
Arduino Nano A5  -> SCL on RTC / EEPROM / LCD
Arduino Nano 5V  -> VCC
Arduino Nano GND -> GND
```

Many modules already include pull-up resistors. If your I2C bus behaves unreliably, check whether pull-ups are present.

---

## Arduino IDE Settings

For a classic Arduino Nano:

```text
Board: Arduino Nano
Processor: ATmega328P
Baud rate: 9600
Serial Monitor line ending: Newline
```

If upload fails, try:

```text
Processor: ATmega328P (Old Bootloader)
```

---

## Starting NanoOS

After upload, open the Serial Monitor at 9600 baud.

You should see:

```text
NanoOS v0.5.1
Tiny pseudo-Linux shell for Arduino Nano
Type 'help' for commands.

nanoos>
```

Type:

```text
help
```

---

## Command Overview

```text
System:
  help
  uptime
  free
  pwd
  echo <text>
  sleep <ms>

Files:
  ls
  new <name>
  edit <name>
  save <name>
  load <name>
  run <name>
  cat <name>
  rm <name>
  df
  mkfs int|ext
  mount status|int|ext
  cp int|ext int|ext <name>
  mv int|ext int|ext <name>
  eeread int|ext <addr> [len]
  eewrite int|ext <addr> <value>

I2C:
  i2cscan

RTC / DS3231:
  date
  rtcget
  rtcset YYYY-MM-DD HH:MM:SS
  temp
  sqw status
  sqw off
  sqw 1hz|1024hz|4096hz|8192hz
  alarm status
  alarm clear
  alarm off
  alarm once
  alarm sec SS
  alarm minsec MM SS
  alarm hms HH MM SS

Pins:
  blink
  blink <pin>
  blink <pin> <times>
  pin read <pin>
  pin high <pin>
  pin low <pin>
  pin input <pin>
  pin output <pin>
```

---

# System Commands

## `help`

Shows the built-in help text.

```text
help
```

## `uptime`

Shows runtime since the Arduino booted.

```text
uptime
```

Example:

```text
Uptime: 00:03:12
```

If the board has been running for more than one day, days are shown too.

## `free`

Shows estimated free SRAM.

```text
free
```

Example:

```text
Free SRAM: 612 bytes
```

This is useful because the ATmega328P only has 2 KB SRAM.

## `pwd`

Shows the currently mounted NanoOS filesystem.

```text
pwd
```

Example:

```text
/int
```

or:

```text
/ext
```

## `echo <text>`

Prints text.

```text
echo Hello NanoOS
```

Example:

```text
Hello NanoOS
```

This is especially useful in scripts.

## `sleep <ms>`

Pauses execution for a number of milliseconds.

```text
sleep 1000
```

Allowed range:

```text
0..30000 ms
```

Useful in scripts:

```text
echo Waiting...
sleep 1000
echo Done
```

---

# I2C Commands

## `i2cscan`

Scans the I2C bus and prints detected devices.

```text
i2cscan
```

Example output:

```text
Scanning I2C bus...
Found device at 0x50  EEPROM candidate
Found device at 0x68  DS3231/DS1307/RTC candidate
Devices found: 2
Done.
```

Recognized hints:

```text
0x68       RTC candidate, e.g. DS3231 or DS1307
0x50-0x57  EEPROM candidate
0x27/0x3F  I2C LCD candidate
```

---

# Pin Commands

## `blink`

Blinks the built-in LED once.

```text
blink
```

## `blink <pin>`

Blinks a selected pin once.

```text
blink 13
```

## `blink <pin> <times>`

Blinks a selected pin multiple times.

```text
blink 13 5
```

## `pin read <pin>`

Reads a digital pin.

```text
pin read 2
```

Example:

```text
Pin 2 = HIGH
```

## `pin high <pin>`

Sets a digital pin HIGH.

```text
pin high 13
```

## `pin low <pin>`

Sets a digital pin LOW.

```text
pin low 13
```

## `pin input <pin>`

Sets pin mode to INPUT.

```text
pin input 2
```

## `pin output <pin>`

Sets pin mode to OUTPUT.

```text
pin output 13
```

---

# NanoOS File System

NanoOS has a tiny EEPROM-backed file system.

There are two possible backends:

```text
/int   internal ATmega328P EEPROM
/ext   external I2C EEPROM at 0x50
```

## Internal EEPROM FS

```text
Max files:      8
Max file size:  96 bytes
Storage:        internal ATmega328P EEPROM
```

## External EEPROM FS

Default assumptions:

```text
Chip style:     24LC256-compatible
I2C address:    0x50
Total size:     32768 bytes
Max files:      24
Max file size:  128 bytes
```

The external file system currently uses only a small part of a 24LC256. This is intentional to keep the system simple and SRAM-friendly.

---

## Mounting Filesystems

## `mount status`

Shows currently mounted filesystem and whether the external EEPROM is detected.

```text
mount status
```

Example:

```text
Mounted EEPROM FS: internal
External EEPROM 0x50: present
```

## `mount int`

Switches to internal EEPROM file system.

```text
mount int
```

## `mount ext`

Switches to external EEPROM file system.

```text
mount ext
```

Important: since v0.5.1, `mount ext` does **not** automatically format the EEPROM. If the filesystem is not initialized, NanoOS prints:

```text
Warning: mounted filesystem is not formatted. Use mkfs int|ext.
```

You must explicitly run:

```text
mkfs ext
```

---

## Formatting Filesystems

## `mkfs int`

Formats the internal EEPROM file system.

```text
mkfs int
```

Warning: this deletes all NanoOS files stored in internal EEPROM.

## `mkfs ext`

Formats the external EEPROM file system.

```text
mkfs ext
```

Warning: this writes NanoOS filesystem structures to the external EEPROM. Existing data at the used addresses will be overwritten.

---

## `df`

Shows storage usage for the currently mounted filesystem.

```text
df
```

Example:

```text
Backend:        internal
EEPROM FS slots: 3/8
EEPROM FS data:  288/768 B used, 480 B free
File max size:    96 B
RAM edit buffer:  93/192 B
```

## `ls`

Lists files in the currently mounted filesystem.

```text
ls
```

Example:

```text
Files:
Backend: internal
0  note     42 B
1  boot     88 B
RAM: boot  88 B  modified
```

---

# File Commands

## `new <name>`

Creates a new RAM file buffer.

```text
new note
```

File name rules:

```text
1..8 characters
A-Z a-z 0-9 _ -
```

This creates the file only in RAM. Use `save <name>` to store it in EEPROM.

## `edit <name>`

Opens a file in the line-based editor.

```text
edit note
```

If the file exists in the mounted EEPROM filesystem, it is loaded into RAM first.

If it does not exist, a new RAM file with that name is created.

## `save <name>`

Saves the current RAM file to the mounted EEPROM filesystem.

```text
save note
```

Inside the editor, `.save` does the same and exits the editor.

## `load <name>`

Loads an EEPROM file into RAM.

```text
load note
```

This is useful if you want to load a file before editing, viewing, or saving under another name.

## `cat <name>`

Prints a file.

```text
cat note
```

If the file is not already active in RAM, NanoOS loads it from EEPROM.

## `rm <name>`

Deletes a file from the mounted EEPROM filesystem.

```text
rm note
```

If the deleted file is also the active RAM file, the RAM file is cleared.

---

# Line-Based Editor

NanoOS has a serial-friendly editor. It is not a fullscreen editor. It is designed for the Arduino Serial Monitor.

Start editing:

```text
edit note
```

Example session:

```text
nanoos> edit note
Editing note. Enter lines. Use .save or .quit
edit> Hello NanoOS
edit> This is a file.
edit> .show
1: Hello NanoOS
2: This is a file.
edit> .save
Saved: note
nanoos>
```

## Editor commands

```text
.show          show buffer with line numbers
.clear         clear buffer
.del N         delete line N
.set N TEXT    replace line N with TEXT
.ins N TEXT    insert TEXT before line N
.save          save file and leave editor
.quit          leave editor without saving
```

## `.show`

Shows the current buffer with line numbers.

```text
.show
```

Example:

```text
1: line one
2: line two
3: line three
```

## `.del N`

Deletes line N.

```text
.del 2
```

## `.set N TEXT`

Replaces line N with new text.

```text
.set 2 blink 13 5
```

## `.ins N TEXT`

Inserts a new line before line N.

```text
.ins 1 # New first line
```

To append at the end, insert after the current last line number plus one.

Example: if there are 3 lines:

```text
.ins 4 new final line
```

## `.clear`

Clears the current RAM buffer.

```text
.clear
```

## `.save`

Saves the RAM file to the current mounted filesystem and exits the editor.

```text
.save
```

## `.quit`

Exits the editor without saving.

```text
.quit
```

---

# Scripts

NanoOS can execute text files as scripts.

Create a script:

```text
edit boot
#!nanoos
# This is a NanoOS script
blink 13 2
date
temp
free
.save
```

Run it:

```text
run boot
```

Example output:

```text
Running script: boot
> blink 13 2
Blinking pin 13 2 time(s).
> date
2026-05-01 14:30:12
> temp
DS3231 temperature: 23.75 C
> free
Free SRAM: 612 bytes
Script done. Executed: 4, skipped: 2
```

## Script comments

Lines beginning with `#` are skipped.

```text
# This is a comment
```

## Shebang support

The first line may be:

```text
#!nanoos
```

This is treated as a comment and skipped.

## Blocked commands in scripts

For safety and simplicity, these commands are blocked inside scripts:

```text
edit
new
run
```

This prevents interactive editor issues and recursive script execution.

Allowed commands include, for example:

```text
echo Starting test
blink 13 3
sleep 1000
date
temp
free
df
pin high 13
pin low 13
alarm status
sqw status
```

---

# Script Loops

NanoOS supports simple non-nested loops inside scripts.

Syntax:

```text
loop start N
...
loop end
```

Allowed loop count:

```text
1..255
```

Example:

```text
edit blink5
#!nanoos
# Blink LED 5 times
loop start 5
blink 13 1
sleep 200
loop end
echo Done
.save
```

Run it:

```text
run blink5
```

Expected behavior:

```text
blink 13 1
sleep 200
```

is repeated 5 times, then `echo Done` is executed.

Nested loops are not supported. If a loop appears inside another loop, NanoOS prints a warning.

---

# Copying and Moving Between EEPROM Filesystems

NanoOS can copy or move files between internal and external EEPROM filesystems.

## `cp int ext <name>`

Copies a file from internal EEPROM to external EEPROM.

```text
cp int ext note
```

## `cp ext int <name>`

Copies a file from external EEPROM to internal EEPROM.

```text
cp ext int note
```

## `mv int ext <name>`

Moves a file from internal to external EEPROM.

```text
mv int ext note
```

## `mv ext int <name>`

Moves a file from external to internal EEPROM.

```text
mv ext int note
```

## Truncation protection

Since internal files are smaller than external files, copying from external to internal could truncate a file.

NanoOS v0.5.1 prevents this. If truncation would occur, the copy/move fails with:

```text
Error: copy failed. File missing, filesystem unformatted, target full, or truncation would occur.
```

or:

```text
Error: move failed. File missing, filesystem unformatted, target full, or truncation would occur.
```

---

# Raw EEPROM Commands

NanoOS includes low-level EEPROM access commands.

Be careful: raw writes can corrupt the NanoOS filesystem.

## `eeread int <addr> [len]`

Reads bytes from internal EEPROM.

```text
eeread int 0 16
```

## `eeread ext <addr> [len]`

Reads bytes from external EEPROM.

```text
eeread ext 0 16
```

Maximum read length:

```text
32 bytes
```

## `eewrite int <addr> <value>`

Writes one byte to internal EEPROM.

```text
eewrite int 100 42
```

## `eewrite ext <addr> <value>`

Writes one byte to external EEPROM.

```text
eewrite ext 1000 42
```

Value range:

```text
0..255
```

Warning: do not write randomly to address 0 or the file table unless you intentionally want to corrupt/reset the filesystem.

---

# RTC / DS3231 Commands

NanoOS expects a DS3231 or similar RTC at:

```text
0x68
```

## `date`

Shows the current RTC date and time.

```text
date
```

Example:

```text
2026-05-01 14:30:12
```

If no RTC is found:

```text
No RTC detected at 0x68. Use 'uptime' instead.
```

## `rtcget`

Shows RTC date/time and DS3231 status/control registers.

```text
rtcget
```

Example:

```text
RTC: 2026-05-01 14:30:12
Status: 0x00
Control: 0x04
```

Status flags may include:

```text
OSF=1   oscillator stop flag
A1F=1   Alarm 1 flag
A2F=1   Alarm 2 flag
```

## `rtcset YYYY-MM-DD HH:MM:SS`

Sets the RTC date and time.

```text
rtcset 2026-05-01 14:30:00
```

Example:

```text
RTC set to: 2026-05-01 14:30:00
```

Supported year range:

```text
2000..2099
```

## `temp`

Reads the DS3231 internal temperature sensor.

```text
temp
```

Example:

```text
DS3231 temperature: 23.75 C
```

Note: this is the DS3231 chip temperature, not necessarily exact ambient temperature.

---

# DS3231 Square-Wave Output

The DS3231 INT/SQW pin can output a square wave.

## `sqw status`

Shows current square-wave configuration.

```text
sqw status
```

Example:

```text
SQW control: 0x00 SQW=1Hz
```

## `sqw off`

Disables square-wave output.

```text
sqw off
```

## `sqw 1hz`

Enables 1 Hz output.

```text
sqw 1hz
```

## Other supported frequencies

```text
sqw 1024hz
sqw 4096hz
sqw 8192hz
```

---

# DS3231 Alarm Commands

NanoOS uses **DS3231 Alarm 1** because Alarm 1 supports seconds resolution.

The DS3231 INT/SQW pin is **open-drain**. It usually needs a pull-up resistor. When an alarm triggers, the pin is pulled LOW until the alarm flag is cleared.

## `alarm status`

Shows Alarm 1 registers, interpreted mode, interrupt state, and alarm flags.

```text
alarm status
```

Example:

```text
Alarm1 registers: 0x80 0x80 0x80 0x80
Alarm1 mode: once per second
Alarm1 interrupt: enabled
Alarm flags: A1F=1 A2F=0
```

## `alarm clear`

Clears DS3231 alarm flags.

```text
alarm clear
```

Use this after an alarm has fired.

## `alarm off`

Disables Alarm 1 interrupt.

```text
alarm off
```

## `alarm once`

Configures Alarm 1 to trigger once per second.

```text
alarm once
```

## `alarm sec SS`

Triggers when seconds match.

```text
alarm sec 30
```

This fires once per minute at second 30.

## `alarm minsec MM SS`

Triggers when minutes and seconds match.

```text
alarm minsec 45 00
```

This fires once per hour at minute 45, second 00.

## `alarm hms HH MM SS`

Triggers when hours, minutes, and seconds match.

```text
alarm hms 07 30 00
```

This fires once per day at 07:30:00.

---

# Typical Workflows

## Create and save a note

```text
new note
edit note
Hello NanoOS
This is stored in EEPROM.
.save
ls
cat note
```

## Edit an existing file

```text
edit note
.show
.set 2 This line has been changed.
.ins 1 # Header
.del 3
.show
.save
```

## Create and run a script

```text
edit boot
#!nanoos
# Boot demo
pwd
echo Starting demo
blink 13 2
date
temp
free
echo Done
.save
run boot
```

## Use an external EEPROM

```text
i2cscan
mount status
mount ext
mkfs ext
df
new extnote
edit extnote
Hello external EEPROM
.save
ls
cat extnote
```

## Copy a file from internal to external

```text
mount int
ls
cp int ext note
mount ext
ls
cat note
```

---

# Memory and Limits

NanoOS is designed for a very small microcontroller.

Current important limits:

```text
Input line length:     63 characters
File name length:      8 characters
RAM edit buffer:       192 bytes
Internal files:        8
Internal file size:    96 bytes
External files:        24
External file size:    128 bytes
Script loop count:     1..255
Raw EEPROM read len:   1..32 bytes
sleep range:           0..30000 ms
```

The sketch uses static buffers to avoid dynamic memory allocation.

---

# Important Notes

## This is not a real OS

NanoOS is a shell-like Arduino sketch. It has no real processes, no kernel, no filesystem driver abstraction in the desktop sense, and no memory protection.

## EEPROM wear

EEPROM has limited write endurance. NanoOS uses `EEPROM.update()` for internal EEPROM and update-like behavior for external EEPROM to avoid unnecessary writes, but frequent writing still causes wear.

For heavy use, FRAM would be better than EEPROM.

## External EEPROM page writes are not optimized

NanoOS writes external EEPROM byte-by-byte with a small delay after each write. This is simple and reliable but slow.

A future version could use page writes for much faster saving.

## RTC battery backup

If using a DS3231 module with a coin cell, the clock can continue running after the Arduino is powered off.

## DS3231 INT/SQW pin

The INT/SQW pin is open-drain. It needs a pull-up. Alarm output usually goes LOW when active.

---

# Planned Future Features

Possible future versions:

```text
chiptemp              internal ATmega328P temperature sensor
lcd                   I2C LCD output
format confirmation   safer mkfs flow
touch <file>          create empty file directly in EEPROM
rename <old> <new>    rename file
append <file> <text>  append without opening editor
hexdump               nicer EEPROM dump format
autorun boot          optional run boot script at startup
FRAM support          better for frequent writes
larger external FS    use more of 24LC256 capacity
```

---

# Example Demo Script

```text
#!nanoos
# NanoOS demo
pwd
echo Starting NanoOS demo
blink 13 2
sleep 500
date
temp
free
df
loop start 3
echo Loop tick
blink 13 1
sleep 250
loop end
echo Demo complete
```

Save as `demo` and run:

```text
run demo
```

---

# License
MIT, see License file

