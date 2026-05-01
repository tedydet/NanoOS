#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>

// ============================================================
// NanoOS v0.5.1
// Tiny pseudo-Linux shell for Arduino Nano / ATmega328P
//
// Commands:
//   help
//   uptime
//   free
//   i2cscan
//   date
//   rtcget
//   rtcset YYYY-MM-DD HH:MM:SS
//   temp
//   sqw status|off|1hz|1024hz|4096hz|8192hz
//   alarm status|clear|off|once|sec SS|minsec MM SS|hms HH MM SS
//   ls
//   new <name>
//   edit <name>
//   save <name>
//   load <name>
//   run <name>
//   cat <name>
//   rm <name>
//   df
//   pwd
//   echo <text>
//   sleep <ms>
//   mkfs int|ext
//   mount status|int|ext
//   cp int|ext int|ext <name>
//   mv int|ext int|ext <name>
//   eeread int|ext <addr> [len]
//   eewrite int|ext <addr> <value>
//   blink
//   blink <pin>
//   blink <pin> <times>
//   pin read <pin>
//   pin high <pin>
//   pin low <pin>
//   pin input <pin>
//   pin output <pin>
// ============================================================

#define NANOOS_VERSION "0.5.1"

const uint32_t SERIAL_BAUD = 9600;
const uint8_t INPUT_BUFFER_SIZE = 64;
const uint8_t DS3231_ADDRESS = 0x68;

const uint8_t FS_MAGIC = 0x42;
const uint8_t FS_VERSION = 1;
const uint8_t FS_NAME_LEN = 8;
const uint8_t INT_FS_MAX_FILES = 8;
const uint8_t INT_FS_SLOT_SIZE = 96;
const uint8_t EXT_FS_MAX_FILES = 24;
const uint8_t EXT_FS_SLOT_SIZE = 128;
const uint8_t RAM_FILE_SIZE = 192;
const int FS_MAGIC_ADDR = 0;
const int FS_VERSION_ADDR = 1;
const int FS_TABLE_ADDR = 4;
const uint8_t FS_ENTRY_SIZE = 12;
const uint8_t EXT_EEPROM_DEFAULT_ADDR = 0x50;
const uint16_t EXT_EEPROM_SIZE_BYTES = 32768; // 24LC256-style default

char inputBuffer[INPUT_BUFFER_SIZE];
uint8_t inputPos = 0;

char ramFileName[FS_NAME_LEN + 1];
char ramFileBuffer[RAM_FILE_SIZE + 1];
uint16_t ramFileSize = 0;
bool ramFileActive = false;
bool ramFileDirty = false;
bool editorMode = false;
bool fsUseExternal = false;
uint8_t extEepromAddress = EXT_EEPROM_DEFAULT_ADDR;

// ------------------------------------------------------------
// Free RAM calculation for AVR
// ------------------------------------------------------------
extern unsigned int __heap_start;
extern void *__brkval;

int freeMemory() {
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

// ------------------------------------------------------------
// Utility
// ------------------------------------------------------------
void printPrompt() {
  Serial.print(F("nanoos> "));
}

void printBootMessage() {
  Serial.println();
  Serial.println(F("NanoOS v" NANOOS_VERSION));
  Serial.println(F("Tiny pseudo-Linux shell for Arduino Nano"));
  Serial.println(F("Type 'help' for commands."));
  Serial.println();
  printPrompt();
}

bool parseIntSafe(const char *s, int &out) {
  if (s == NULL || *s == '\0') {
    return false;
  }

  char *endPtr;
  long value = strtol(s, &endPtr, 10);

  if (*endPtr != '\0') {
    return false;
  }

  if (value < -32768 || value > 32767) {
    return false;
  }

  out = (int)value;
  return true;
}

bool validPin(int pin) {
#if defined(NUM_DIGITAL_PINS)
  return pin >= 0 && pin < NUM_DIGITAL_PINS;
#else
  return pin >= 0 && pin <= 19;
#endif
}

byte bcdToDec(byte val) {
  return ((val / 16 * 10) + (val % 16));
}

byte decToBcd(byte val) {
  return ((val / 10 * 16) + (val % 10));
}

bool i2cDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool readRtcRaw(byte *data, uint8_t len) {
  Wire.beginTransmission(DS3231_ADDRESS);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  Wire.requestFrom(DS3231_ADDRESS, len);
  if (Wire.available() < len) {
    return false;
  }

  for (uint8_t i = 0; i < len; i++) {
    data[i] = Wire.read();
  }

  return true;
}

bool readRtcDateTime(uint16_t &year, uint8_t &month, uint8_t &day, uint8_t &hour, uint8_t &minute, uint8_t &second) {
  byte data[7];
  if (!readRtcRaw(data, 7)) {
    return false;
  }

  second = bcdToDec(data[0] & 0x7F);
  minute = bcdToDec(data[1] & 0x7F);

  if (data[2] & 0x40) {
    // 12-hour mode. Convert to 24-hour mode.
    hour = bcdToDec(data[2] & 0x1F);
    bool pm = data[2] & 0x20;
    if (pm && hour < 12) {
      hour += 12;
    }
    if (!pm && hour == 12) {
      hour = 0;
    }
  } else {
    hour = bcdToDec(data[2] & 0x3F);
  }

  day = bcdToDec(data[4] & 0x3F);
  month = bcdToDec(data[5] & 0x1F);
  year = 2000 + bcdToDec(data[6]);

  return true;
}

bool writeRtcDateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) {
    return false;
  }

  Wire.beginTransmission(DS3231_ADDRESS);
  Wire.write((uint8_t)0x00);
  Wire.write(decToBcd(second));
  Wire.write(decToBcd(minute));
  Wire.write(decToBcd(hour));
  Wire.write(decToBcd(1)); // Day of week placeholder: 1
  Wire.write(decToBcd(day));
  Wire.write(decToBcd(month));
  Wire.write(decToBcd(year - 2000));

  return Wire.endTransmission() == 0;
}

void printTwoDigits(uint8_t value) {
  if (value < 10) {
    Serial.print('0');
  }
  Serial.print(value);
}

void printRtcDateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
  Serial.print(year);
  Serial.print('-');
  printTwoDigits(month);
  Serial.print('-');
  printTwoDigits(day);
  Serial.print(' ');
  printTwoDigits(hour);
  Serial.print(':');
  printTwoDigits(minute);
  Serial.print(':');
  printTwoDigits(second);
  Serial.println();
}

bool parseDate(const char *text, uint16_t &year, uint8_t &month, uint8_t &day) {
  if (text == NULL || strlen(text) != 10) {
    return false;
  }

  if (text[4] != '-' || text[7] != '-') {
    return false;
  }

  char buf[5];
  memcpy(buf, text, 4);
  buf[4] = '\0';
  int y = atoi(buf);

  memcpy(buf, text + 5, 2);
  buf[2] = '\0';
  int m = atoi(buf);

  memcpy(buf, text + 8, 2);
  buf[2] = '\0';
  int d = atoi(buf);

  if (y < 2000 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31) {
    return false;
  }

  year = (uint16_t)y;
  month = (uint8_t)m;
  day = (uint8_t)d;
  return true;
}

bool parseTime(const char *text, uint8_t &hour, uint8_t &minute, uint8_t &second) {
  if (text == NULL || strlen(text) != 8) {
    return false;
  }

  if (text[2] != ':' || text[5] != ':') {
    return false;
  }

  char buf[3];
  memcpy(buf, text, 2);
  buf[2] = '\0';
  int h = atoi(buf);

  memcpy(buf, text + 3, 2);
  buf[2] = '\0';
  int m = atoi(buf);

  memcpy(buf, text + 6, 2);
  buf[2] = '\0';
  int s = atoi(buf);

  if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) {
    return false;
  }

  hour = (uint8_t)h;
  minute = (uint8_t)m;
  second = (uint8_t)s;
  return true;
}

bool readRegister(uint8_t address, uint8_t reg, byte &value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  Wire.requestFrom(address, (uint8_t)1);
  if (Wire.available() < 1) {
    return false;
  }

  value = Wire.read();
  return true;
}

bool writeRegister(uint8_t address, uint8_t reg, byte value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

uint8_t fsMaxFiles() {
  return fsUseExternal ? EXT_FS_MAX_FILES : INT_FS_MAX_FILES;
}

uint8_t fsSlotSizeBytes() {
  return fsUseExternal ? EXT_FS_SLOT_SIZE : INT_FS_SLOT_SIZE;
}

int fsDataBaseAddr() {
  return FS_TABLE_ADDR + (fsMaxFiles() * FS_ENTRY_SIZE);
}

uint16_t fsTotalDataBytes() {
  return (uint16_t)fsMaxFiles() * (uint16_t)fsSlotSizeBytes();
}

const __FlashStringHelper *fsBackendName() {
  return fsUseExternal ? F("external") : F("internal");
}

bool extEepromReadByte(uint16_t addr, byte &value) {
  Wire.beginTransmission(extEepromAddress);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  if (Wire.endTransmission() != 0) {
    return false;
  }

  Wire.requestFrom(extEepromAddress, (uint8_t)1);
  if (Wire.available() < 1) {
    return false;
  }

  value = Wire.read();
  return true;
}

bool extEepromWriteByte(uint16_t addr, byte value) {
  Wire.beginTransmission(extEepromAddress);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  Wire.write(value);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  delay(5); // EEPROM write cycle time
  return true;
}

bool extEepromUpdateByte(uint16_t addr, byte value) {
  byte oldValue;
  if (!extEepromReadByte(addr, oldValue)) {
    return false;
  }

  if (oldValue == value) {
    return true;
  }

  return extEepromWriteByte(addr, value);
}

bool fsReadByte(uint16_t addr, byte &value) {
  if (fsUseExternal) {
    return extEepromReadByte(addr, value);
  }

  if (addr >= EEPROM.length()) {
    return false;
  }

  value = EEPROM.read(addr);
  return true;
}

bool fsUpdateByte(uint16_t addr, byte value) {
  if (fsUseExternal) {
    return extEepromUpdateByte(addr, value);
  }

  if (addr >= EEPROM.length()) {
    return false;
  }

  EEPROM.update(addr, value);
  return true;
}

byte fsReadByteOrZero(uint16_t addr) {
  byte value = 0;
  fsReadByte(addr, value);
  return value;
}

bool fsHasValidMagic() {
  byte magic = 0;
  byte version = 0;
  fsReadByte(FS_MAGIC_ADDR, magic);
  fsReadByte(FS_VERSION_ADDR, version);
  return magic == FS_MAGIC && version == FS_VERSION;
}

void fsFormatCurrentBackend() {
  fsUpdateByte(FS_MAGIC_ADDR, FS_MAGIC);
  fsUpdateByte(FS_VERSION_ADDR, FS_VERSION);

  for (uint8_t slot = 0; slot < fsMaxFiles(); slot++) {
    int base = fsEntryAddr(slot);
    fsUpdateByte(base, 0);      // used flag
    fsUpdateByte(base + 1, 0);  // size low
    fsUpdateByte(base + 2, 0);  // size high
    for (uint8_t i = 0; i < FS_NAME_LEN; i++) {
      fsUpdateByte(base + 3 + i, 0);
    }
    fsUpdateByte(base + 11, 0); // reserved
  }
}

bool parseBackend(const char *text, bool &externalOut) {
  if (text == NULL) {
    return false;
  }

  if (strcmp(text, "int") == 0 || strcmp(text, "internal") == 0) {
    externalOut = false;
    return true;
  }

  if (strcmp(text, "ext") == 0 || strcmp(text, "external") == 0) {
    externalOut = true;
    return true;
  }

  return false;
}

bool switchFsBackend(bool useExternal) {
  if (useExternal && !i2cDevicePresent(extEepromAddress)) {
    return false;
  }

  fsUseExternal = useExternal;
  return true;
}

void printHexByte(byte value) {
  if (value < 16) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

bool readRtcRegisters(uint8_t startReg, byte *data, uint8_t len) {
  Wire.beginTransmission(DS3231_ADDRESS);
  Wire.write(startReg);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  Wire.requestFrom(DS3231_ADDRESS, len);
  if (Wire.available() < len) {
    return false;
  }

  for (uint8_t i = 0; i < len; i++) {
    data[i] = Wire.read();
  }

  return true;
}

bool writeRtcRegisters(uint8_t startReg, const byte *data, uint8_t len) {
  Wire.beginTransmission(DS3231_ADDRESS);
  Wire.write(startReg);
  for (uint8_t i = 0; i < len; i++) {
    Wire.write(data[i]);
  }
  return Wire.endTransmission() == 0;
}

bool parseByteRange(const char *text, uint8_t minValue, uint8_t maxValue, uint8_t &out) {
  int value;
  if (!parseIntSafe(text, value)) {
    return false;
  }

  if (value < minValue || value > maxValue) {
    return false;
  }

  out = (uint8_t)value;
  return true;
}

bool clearDs3231AlarmFlags() {
  byte status;
  if (!readRegister(DS3231_ADDRESS, 0x0F, status)) {
    return false;
  }

  status &= ~0x03;
  return writeRegister(DS3231_ADDRESS, 0x0F, status);
}

bool setDs3231Alarm1Enabled(bool enabled) {
  byte control;
  if (!readRegister(DS3231_ADDRESS, 0x0E, control)) {
    return false;
  }

  if (enabled) {
    control |= 0x05;  // INTCN=1, A1IE=1
  } else {
    control &= ~0x01; // A1IE=0
  }

  return writeRegister(DS3231_ADDRESS, 0x0E, control);
}

void printAlarm1Mode(byte a1sec, byte a1min, byte a1hour, byte a1daydate) {
  bool a1m1 = a1sec & 0x80;
  bool a1m2 = a1min & 0x80;
  bool a1m3 = a1hour & 0x80;
  bool a1m4 = a1daydate & 0x80;

  Serial.print(F("Alarm1 mode: "));

  if (a1m1 && a1m2 && a1m3 && a1m4) {
    Serial.println(F("once per second"));
  } else if (!a1m1 && a1m2 && a1m3 && a1m4) {
    Serial.print(F("when seconds match: "));
    printTwoDigits(bcdToDec(a1sec & 0x7F));
    Serial.println();
  } else if (!a1m1 && !a1m2 && a1m3 && a1m4) {
    Serial.print(F("when minutes+seconds match: "));
    printTwoDigits(bcdToDec(a1min & 0x7F));
    Serial.print(':');
    printTwoDigits(bcdToDec(a1sec & 0x7F));
    Serial.println();
  } else if (!a1m1 && !a1m2 && !a1m3 && a1m4) {
    Serial.print(F("when hours+minutes+seconds match: "));
    printTwoDigits(bcdToDec(a1hour & 0x3F));
    Serial.print(':');
    printTwoDigits(bcdToDec(a1min & 0x7F));
    Serial.print(':');
    printTwoDigits(bcdToDec(a1sec & 0x7F));
    Serial.println();
  } else {
    Serial.println(F("custom/unsupported"));
  }
}

int fsEntryAddr(uint8_t slot) {
  return FS_TABLE_ADDR + (slot * FS_ENTRY_SIZE);
}

int fsDataAddr(uint8_t slot) {
  return fsDataBaseAddr() + (slot * fsSlotSizeBytes());
}

void clearRamFile() {
  ramFileName[0] = '\0';
  ramFileBuffer[0] = '\0';
  ramFileSize = 0;
  ramFileActive = false;
  ramFileDirty = false;
}

bool isValidFileName(const char *name) {
  if (name == NULL) {
    return false;
  }

  uint8_t len = strlen(name);
  if (len == 0 || len > FS_NAME_LEN) {
    return false;
  }

  for (uint8_t i = 0; i < len; i++) {
    char c = name[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) {
      return false;
    }
  }

  return true;
}

void copyFileName(char *dest, const char *src) {
  memset(dest, 0, FS_NAME_LEN + 1);
  strncpy(dest, src, FS_NAME_LEN);
  dest[FS_NAME_LEN] = '\0';
}

void fsFormatIfNeeded() {
  if (fsHasValidMagic()) {
    return;
  }

  fsFormatCurrentBackend();
}

bool fsSlotUsed(uint8_t slot) {
  return fsReadByteOrZero(fsEntryAddr(slot)) == 1;
}

uint16_t fsSlotSize(uint8_t slot) {
  int base = fsEntryAddr(slot);
  uint16_t low = fsReadByteOrZero(base + 1);
  uint16_t high = fsReadByteOrZero(base + 2);
  return low | (high << 8);
}

void fsReadName(uint8_t slot, char *nameOut) {
  int base = fsEntryAddr(slot);
  for (uint8_t i = 0; i < FS_NAME_LEN; i++) {
    nameOut[i] = (char)fsReadByteOrZero(base + 3 + i);
  }
  nameOut[FS_NAME_LEN] = '\0';
}

void fsWriteEntry(uint8_t slot, const char *name, uint16_t size) {
  int base = fsEntryAddr(slot);
  fsUpdateByte(base, 1);
  fsUpdateByte(base + 1, size & 0xFF);
  fsUpdateByte(base + 2, (size >> 8) & 0xFF);

  for (uint8_t i = 0; i < FS_NAME_LEN; i++) {
    byte value = 0;
    if (name != NULL && i < strlen(name)) {
      value = name[i];
    }
    fsUpdateByte(base + 3 + i, value);
  }

  fsUpdateByte(base + 11, 0);
}

void fsClearEntry(uint8_t slot) {
  int base = fsEntryAddr(slot);
  fsUpdateByte(base, 0);
  fsUpdateByte(base + 1, 0);
  fsUpdateByte(base + 2, 0);
  for (uint8_t i = 0; i < FS_NAME_LEN; i++) {
    fsUpdateByte(base + 3 + i, 0);
  }
  fsUpdateByte(base + 11, 0);
}

int8_t fsFindSlotByName(const char *name) {
  char storedName[FS_NAME_LEN + 1];

  for (uint8_t slot = 0; slot < fsMaxFiles(); slot++) {
    if (!fsSlotUsed(slot)) {
      continue;
    }

    fsReadName(slot, storedName);
    if (strncmp(storedName, name, FS_NAME_LEN) == 0) {
      return slot;
    }
  }

  return -1;
}

int8_t fsFindFreeSlot() {
  for (uint8_t slot = 0; slot < fsMaxFiles(); slot++) {
    if (!fsSlotUsed(slot)) {
      return slot;
    }
  }

  return -1;
}

bool fsLoadToRam(const char *name) {
  int8_t slot = fsFindSlotByName(name);
  if (slot < 0) {
    return false;
  }

  uint16_t size = fsSlotSize(slot);
  if (size > RAM_FILE_SIZE) {
    size = RAM_FILE_SIZE;
  }

  int dataAddr = fsDataAddr(slot);
  for (uint16_t i = 0; i < size; i++) {
    ramFileBuffer[i] = (char)fsReadByteOrZero(dataAddr + i);
  }
  ramFileBuffer[size] = '\0';

  copyFileName(ramFileName, name);
  ramFileSize = size;
  ramFileActive = true;
  ramFileDirty = false;
  return true;
}

bool fsSaveFromRam(const char *name) {
  if (!ramFileActive) {
    return false;
  }

  if (!isValidFileName(name)) {
    return false;
  }

  uint16_t size = ramFileSize;
  if (size > fsSlotSizeBytes()) {
    size = fsSlotSizeBytes();
  }

  int8_t slot = fsFindSlotByName(name);
  if (slot < 0) {
    slot = fsFindFreeSlot();
  }

  if (slot < 0) {
    return false;
  }

  int dataAddr = fsDataAddr(slot);
  for (uint16_t i = 0; i < fsSlotSizeBytes(); i++) {
    byte value = 0;
    if (i < size) {
      value = ramFileBuffer[i];
    }
    fsUpdateByte(dataAddr + i, value);
  }

  fsWriteEntry(slot, name, size);
  copyFileName(ramFileName, name);
  ramFileSize = size;
  ramFileBuffer[size] = '\0';
  ramFileActive = true;
  ramFileDirty = false;
  return true;
}

void appendEditorLine(const char *line) {
  if (line == NULL) {
    return;
  }

  uint16_t len = strlen(line);
  uint16_t needed = len + 1;

  if (ramFileSize + needed > RAM_FILE_SIZE) {
    Serial.println(F("Editor buffer full. Line ignored."));
    return;
  }

  for (uint16_t i = 0; i < len; i++) {
    ramFileBuffer[ramFileSize++] = line[i];
  }
  ramFileBuffer[ramFileSize++] = '\n';
  ramFileBuffer[ramFileSize] = '\0';
  ramFileDirty = true;
}

uint16_t editorLineCount() {
  if (ramFileSize == 0) {
    return 0;
  }

  uint16_t count = 0;
  bool hasCharsOnLine = false;

  for (uint16_t i = 0; i < ramFileSize; i++) {
    if (ramFileBuffer[i] == '\n') {
      count++;
      hasCharsOnLine = false;
    } else {
      hasCharsOnLine = true;
    }
  }

  if (hasCharsOnLine) {
    count++;
  }

  return count;
}

bool getEditorLineBounds(uint16_t lineNumber, uint16_t &start, uint16_t &end) {
  if (lineNumber == 0) {
    return false;
  }

  uint16_t currentLine = 1;
  start = 0;

  for (uint16_t i = 0; i <= ramFileSize; i++) {
    char c = (i < ramFileSize) ? ramFileBuffer[i] : '\0';

    if (c == '\n' || c == '\0') {
      if (currentLine == lineNumber) {
        end = i;
        return true;
      }

      currentLine++;
      start = i + 1;
    }
  }

  return false;
}

void printEditorBufferNumbered() {
  if (ramFileSize == 0) {
    Serial.println(F("[empty]"));
    return;
  }

  uint16_t lineNumber = 1;
  uint16_t lineStart = 0;

  for (uint16_t i = 0; i <= ramFileSize; i++) {
    char c = (i < ramFileSize) ? ramFileBuffer[i] : '\0';

    if (c == '\n' || c == '\0') {
      if (i == lineStart && c == '\0') {
        break;
      }

      Serial.print(lineNumber);
      Serial.print(F(": "));
      for (uint16_t j = lineStart; j < i; j++) {
        Serial.print(ramFileBuffer[j]);
      }
      Serial.println();

      lineNumber++;
      lineStart = i + 1;
    }
  }
}

bool rebuildEditorBuffer(uint16_t removeStart, uint16_t removeEndExclusive, const char *insertText, bool addTrailingNewline) {
  char newBuffer[RAM_FILE_SIZE + 1];
  uint16_t newSize = 0;

  for (uint16_t i = 0; i < removeStart && i < ramFileSize; i++) {
    if (newSize >= RAM_FILE_SIZE) {
      return false;
    }
    newBuffer[newSize++] = ramFileBuffer[i];
  }

  if (insertText != NULL) {
    uint16_t len = strlen(insertText);
    for (uint16_t i = 0; i < len; i++) {
      if (newSize >= RAM_FILE_SIZE) {
        return false;
      }
      newBuffer[newSize++] = insertText[i];
    }

    if (addTrailingNewline) {
      if (newSize >= RAM_FILE_SIZE) {
        return false;
      }
      newBuffer[newSize++] = '\n';
    }
  }

  for (uint16_t i = removeEndExclusive; i < ramFileSize; i++) {
    if (newSize >= RAM_FILE_SIZE) {
      return false;
    }
    newBuffer[newSize++] = ramFileBuffer[i];
  }

  memcpy(ramFileBuffer, newBuffer, newSize);
  ramFileBuffer[newSize] = '\0';
  ramFileSize = newSize;
  ramFileDirty = true;
  return true;
}

bool editorDeleteLine(uint16_t lineNumber) {
  uint16_t start;
  uint16_t end;

  if (!getEditorLineBounds(lineNumber, start, end)) {
    return false;
  }

  uint16_t removeEnd = end;
  if (removeEnd < ramFileSize && ramFileBuffer[removeEnd] == '\n') {
    removeEnd++;
  }

  return rebuildEditorBuffer(start, removeEnd, NULL, false);
}

bool editorSetLine(uint16_t lineNumber, const char *text) {
  uint16_t start;
  uint16_t end;

  if (text == NULL) {
    text = "";
  }

  if (!getEditorLineBounds(lineNumber, start, end)) {
    return false;
  }

  uint16_t removeEnd = end;
  if (removeEnd < ramFileSize && ramFileBuffer[removeEnd] == '\n') {
    removeEnd++;
  }

  return rebuildEditorBuffer(start, removeEnd, text, true);
}

bool editorInsertLine(uint16_t lineNumber, const char *text) {
  if (text == NULL) {
    text = "";
  }

  uint16_t count = editorLineCount();

  if (lineNumber == 0 || lineNumber > count + 1) {
    return false;
  }

  if (lineNumber == count + 1) {
    return rebuildEditorBuffer(ramFileSize, ramFileSize, text, true);
  }

  uint16_t start;
  uint16_t end;
  if (!getEditorLineBounds(lineNumber, start, end)) {
    return false;
  }

  return rebuildEditorBuffer(start, start, text, true);
}

bool parseEditorLineCommand(char *commandLine, const char *commandName, uint16_t &lineNumber, char *&textOut) {
  char *cursor = commandLine + strlen(commandName);

  while (*cursor == ' ' || *cursor == '\t') {
    cursor++;
  }

  if (*cursor == '\0') {
    return false;
  }

  char *numberStart = cursor;
  while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
    cursor++;
  }

  char saved = *cursor;
  *cursor = '\0';

  int value;
  bool ok = parseIntSafe(numberStart, value);
  *cursor = saved;

  if (!ok || value < 1 || value > 999) {
    return false;
  }

  while (*cursor == ' ' || *cursor == '\t') {
    cursor++;
  }

  lineNumber = (uint16_t)value;
  textOut = cursor;
  return true;
}

void trimLineInPlace(char *line) {
  if (line == NULL) {
    return;
  }

  uint8_t start = 0;
  while (line[start] == ' ' || line[start] == '\t') {
    start++;
  }

  if (start > 0) {
    uint8_t i = 0;
    while (line[start] != '\0') {
      line[i++] = line[start++];
    }
    line[i] = '\0';
  }

  int len = strlen(line);
  while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
    line[len - 1] = '\0';
    len--;
  }
}

bool isBlockedScriptCommand(const char *line) {
  if (line == NULL) {
    return true;
  }

  char tmp[INPUT_BUFFER_SIZE];
  strncpy(tmp, line, INPUT_BUFFER_SIZE - 1);
  tmp[INPUT_BUFFER_SIZE - 1] = '\0';

  char *cmd = strtok(tmp, " ");
  if (cmd == NULL) {
    return true;
  }

  return strcmp(cmd, "edit") == 0 || strcmp(cmd, "new") == 0 || strcmp(cmd, "run") == 0;
}

bool parseLoopStartLine(const char *line, uint8_t &count) {
  if (line == NULL) {
    return false;
  }

  char tmp[INPUT_BUFFER_SIZE];
  strncpy(tmp, line, INPUT_BUFFER_SIZE - 1);
  tmp[INPUT_BUFFER_SIZE - 1] = '\0';

  char *cmd = strtok(tmp, " ");
  char *sub = strtok(NULL, " ");
  char *num = strtok(NULL, " ");
  char *extra = strtok(NULL, " ");

  if (cmd == NULL || sub == NULL || num == NULL || extra != NULL) {
    return false;
  }

  if (strcmp(cmd, "loop") != 0 || strcmp(sub, "start") != 0) {
    return false;
  }

  int value;
  if (!parseIntSafe(num, value)) {
    return false;
  }

  if (value < 1 || value > 255) {
    return false;
  }

  count = (uint8_t)value;
  return true;
}

bool isLoopEndLine(const char *line) {
  if (line == NULL) {
    return false;
  }

  char tmp[INPUT_BUFFER_SIZE];
  strncpy(tmp, line, INPUT_BUFFER_SIZE - 1);
  tmp[INPUT_BUFFER_SIZE - 1] = '\0';

  char *cmd = strtok(tmp, " ");
  char *sub = strtok(NULL, " ");
  char *extra = strtok(NULL, " ");

  return cmd != NULL && sub != NULL && extra == NULL &&
         strcmp(cmd, "loop") == 0 &&
         strcmp(sub, "end") == 0;
}

bool extractScriptLine(const char *buffer, uint16_t size, uint16_t &index, char *lineOut) {
  uint8_t pos = 0;

  while (index < size) {
    char c = buffer[index++];

    if (c == '\r') {
      continue;
    }

    if (c == '\n' || c == '\0') {
      lineOut[pos] = '\0';
      trimLineInPlace(lineOut);
      return true;
    }

    if (pos < INPUT_BUFFER_SIZE - 1) {
      lineOut[pos++] = c;
    } else {
      lineOut[pos] = '\0';
      trimLineInPlace(lineOut);

      while (index < size && buffer[index] != '\n' && buffer[index] != '\0') {
        index++;
      }

      return true;
    }
  }

  if (pos > 0) {
    lineOut[pos] = '\0';
    trimLineInPlace(lineOut);
    return true;
  }

  return false;
}

bool executeScriptCommandLine(char *line, uint16_t &executed, uint16_t &skipped) {
  if (line[0] == '\0') {
    return true;
  }

  if (line[0] == '#') {
    skipped++;
    return true;
  }

  if (isBlockedScriptCommand(line)) {
    Serial.print(F("! blocked: "));
    Serial.println(line);
    skipped++;
    return true;
  }

  if (isLoopEndLine(line)) {
    Serial.println(F("! ignored stray loop end"));
    skipped++;
    return true;
  }

  uint8_t dummyCount;
  if (parseLoopStartLine(line, dummyCount)) {
    Serial.println(F("! nested loops are not supported"));
    skipped++;
    return true;
  }

  if (strncmp(line, "loop", 4) == 0) {
    Serial.print(F("! invalid loop command: "));
    Serial.println(line);
    skipped++;
    return true;
  }

  char execLine[INPUT_BUFFER_SIZE];
  strncpy(execLine, line, INPUT_BUFFER_SIZE - 1);
  execLine[INPUT_BUFFER_SIZE - 1] = '\0';

  Serial.print(F("> "));
  Serial.println(execLine);
  executeCommand(execLine);
  executed++;
  return true;
}

void executeScriptRange(const char *buffer, uint16_t startIndex, uint16_t endIndex, uint16_t &executed, uint16_t &skipped) {
  uint16_t index = startIndex;
  char line[INPUT_BUFFER_SIZE];

  while (index < endIndex && extractScriptLine(buffer, endIndex, index, line)) {
    executeScriptCommandLine(line, executed, skipped);
  }
}

void formatUptime(uint32_t seconds) {
  uint32_t days = seconds / 86400UL;
  seconds %= 86400UL;

  uint8_t hours = seconds / 3600UL;
  seconds %= 3600UL;

  uint8_t minutes = seconds / 60UL;
  uint8_t secs = seconds % 60UL;

  if (days > 0) {
    Serial.print(days);
    Serial.print(F("d "));
  }

  if (hours < 10) Serial.print('0');
  Serial.print(hours);
  Serial.print(':');

  if (minutes < 10) Serial.print('0');
  Serial.print(minutes);
  Serial.print(':');

  if (secs < 10) Serial.print('0');
  Serial.println(secs);
}

// ------------------------------------------------------------
// Commands
// ------------------------------------------------------------
void cmdHelp() {
  Serial.println(F("NanoOS commands:"));
  Serial.println();
  Serial.println(F("System:"));
  Serial.println(F("  help                 Show this help"));
  Serial.println(F("  uptime               Show runtime since boot"));
  Serial.println(F("  free                 Show free SRAM"));
  Serial.println(F("  pwd                  Show mounted EEPROM filesystem"));
  Serial.println(F("  echo <text>          Print text"));
  Serial.println(F("  sleep <ms>           Pause for milliseconds"));
  Serial.println();
  Serial.println(F("Files:"));
  Serial.println(F("  ls                   List EEPROM files"));
  Serial.println(F("  new <name>           Create RAM file"));
  Serial.println(F("  edit <name>          Edit RAM/EEPROM file"));
  Serial.println(F("                       Editor: .show .clear .del N"));
  Serial.println(F("                               .set N TEXT .ins N TEXT"));
  Serial.println(F("                               .save .quit"));
  Serial.println(F("  save <name>          Save RAM file to EEPROM"));
  Serial.println(F("  load <name>          Load EEPROM file into RAM"));
  Serial.println(F("  run <name>           Execute file as script"));
  Serial.println(F("                       Script skips #/shebang; blocks edit/new/run"));
  Serial.println(F("                       Script loop: loop start N ... loop end, N=1..255"));
  Serial.println(F("  cat <name>           Print RAM or EEPROM file"));
  Serial.println(F("  rm <name>            Delete EEPROM file"));
  Serial.println(F("  df                   Show EEPROM filesystem usage"));
  Serial.println(F("  mkfs int|ext         Format selected EEPROM filesystem"));
  Serial.println(F("  mount status|int|ext Select internal/external EEPROM FS"));
  Serial.println(F("  cp int|ext int|ext <name>"));
  Serial.println(F("                       Copy file between EEPROM filesystems"));
  Serial.println(F("  mv int|ext int|ext <name>"));
  Serial.println(F("                       Move file between EEPROM filesystems"));
  Serial.println(F("  eeread int|ext <addr> [len]"));
  Serial.println(F("                       Raw EEPROM byte read"));
  Serial.println(F("  eewrite int|ext <addr> <value>"));
  Serial.println(F("                       Raw EEPROM byte write"));
  Serial.println();
  Serial.println(F("I2C:"));
  Serial.println(F("  i2cscan              Scan I2C bus"));
  Serial.println();
  Serial.println(F("RTC / DS3231:"));
  Serial.println(F("  date                 Show RTC date/time if available"));
  Serial.println(F("  rtcget               Show RTC date/time and status"));
  Serial.println(F("  rtcset YYYY-MM-DD HH:MM:SS"));
  Serial.println(F("                       Set RTC date/time"));
  Serial.println(F("  temp                 Show DS3231 temperature"));
  Serial.println(F("  sqw status           Show DS3231 square-wave config"));
  Serial.println(F("  sqw off              Disable DS3231 square-wave output"));
  Serial.println(F("  sqw 1hz|1024hz|4096hz|8192hz"));
  Serial.println(F("                       Enable DS3231 square-wave output"));
  Serial.println(F("  alarm status         Show DS3231 alarm status"));
  Serial.println(F("  alarm clear          Clear DS3231 alarm flags"));
  Serial.println(F("  alarm off            Disable DS3231 Alarm 1 interrupt"));
  Serial.println(F("  alarm once           Alarm 1 once per second"));
  Serial.println(F("  alarm sec SS         Alarm 1 when seconds match"));
  Serial.println(F("  alarm minsec MM SS   Alarm 1 when minutes+seconds match"));
  Serial.println(F("  alarm hms HH MM SS   Alarm 1 when hours+minutes+seconds match"));
  Serial.println();
  Serial.println(F("Pins:"));
  Serial.println(F("  blink                Blink built-in LED once"));
  Serial.println(F("  blink <pin>          Blink selected pin once"));
  Serial.println(F("  blink <pin> <times>  Blink selected pin N times"));
  Serial.println(F("  pin read <pin>       Read pin state"));
  Serial.println(F("  pin high <pin>       Set pin HIGH"));
  Serial.println(F("  pin low <pin>        Set pin LOW"));
  Serial.println(F("  pin input <pin>      Set pin mode INPUT"));
  Serial.println(F("  pin output <pin>     Set pin mode OUTPUT"));
  Serial.println();
}

void cmdUptime() {
  Serial.print(F("Uptime: "));
  formatUptime(millis() / 1000UL);
}

void cmdFree() {
  Serial.print(F("Free SRAM: "));
  Serial.print(freeMemory());
  Serial.println(F(" bytes"));
}

void cmdPwd() {
  Serial.print('/');
  Serial.println(fsUseExternal ? F("ext") : F("int"));
}

void cmdEcho(char *text) {
  if (text == NULL) {
    Serial.println();
    return;
  }
  Serial.println(text);
}

void cmdSleep(char *msText) {
  int ms;
  if (!parseIntSafe(msText, ms) || ms < 0 || ms > 30000) {
    Serial.println(F("Usage: sleep <ms>  (0..30000)"));
    return;
  }

  delay((unsigned long)ms);
}

void cmdLs() {
  char name[FS_NAME_LEN + 1];
  bool any = false;

  Serial.println(F("Files:"));
  Serial.print(F("Backend: "));
  Serial.println(fsBackendName());
  for (uint8_t slot = 0; slot < fsMaxFiles(); slot++) {
    if (!fsSlotUsed(slot)) {
      continue;
    }

    fsReadName(slot, name);
    Serial.print(slot);
    Serial.print(F("  "));
    Serial.print(name);
    Serial.print(F("  "));
    Serial.print(fsSlotSize(slot));
    Serial.println(F(" B"));
    any = true;
  }

  if (!any) {
    Serial.println(F("No files."));
  }

  if (ramFileActive) {
    Serial.print(F("RAM: "));
    Serial.print(ramFileName);
    Serial.print(F("  "));
    Serial.print(ramFileSize);
    Serial.print(F(" B"));
    if (ramFileDirty) {
      Serial.print(F("  modified"));
    }
    Serial.println();
  }
}

void cmdDf() {
  uint8_t usedSlots = 0;
  uint16_t usedBytes = 0;

  for (uint8_t slot = 0; slot < fsMaxFiles(); slot++) {
    if (fsSlotUsed(slot)) {
      usedSlots++;
      usedBytes += fsSlotSizeBytes();
    }
  }

  uint16_t totalBytes = fsTotalDataBytes();
  uint16_t freeBytes = totalBytes - usedBytes;

  Serial.print(F("Backend:        "));
  Serial.println(fsBackendName());

  Serial.print(F("EEPROM FS slots: "));
  Serial.print(usedSlots);
  Serial.print('/');
  Serial.println(fsMaxFiles());

  Serial.print(F("EEPROM FS data:  "));
  Serial.print(usedBytes);
  Serial.print('/');
  Serial.print(totalBytes);
  Serial.print(F(" B used, "));
  Serial.print(freeBytes);
  Serial.println(F(" B free"));

  Serial.print(F("File max size:    "));
  Serial.print(fsSlotSizeBytes());
  Serial.println(F(" B"));

  Serial.print(F("RAM edit buffer:  "));
  Serial.print(ramFileSize);
  Serial.print('/');
  Serial.print(RAM_FILE_SIZE);
  Serial.println(F(" B"));
}

void cmdMkfs(char *target) {
  bool external;
  if (!parseBackend(target, external)) {
    Serial.println(F("Usage: mkfs int|ext"));
    return;
  }

  bool oldBackend = fsUseExternal;

  if (!switchFsBackend(external)) {
    Serial.println(F("Error: external EEPROM not found at 0x50."));
    return;
  }

  fsFormatCurrentBackend();
  clearRamFile();

  Serial.print(F("Formatted EEPROM FS: "));
  Serial.println(fsBackendName());

  switchFsBackend(oldBackend);
}

void cmdNew(char *name) {
  if (!isValidFileName(name)) {
    Serial.println(F("Usage: new <name>  (1-8 chars: A-Z a-z 0-9 _ -)"));
    return;
  }

  clearRamFile();
  copyFileName(ramFileName, name);
  ramFileActive = true;
  ramFileDirty = true;

  Serial.print(F("Created RAM file: "));
  Serial.println(ramFileName);
}

void cmdEdit(char *name) {
  if (!isValidFileName(name)) {
    Serial.println(F("Usage: edit <name>"));
    return;
  }

  if (!ramFileActive || strncmp(ramFileName, name, FS_NAME_LEN) != 0) {
    if (!fsLoadToRam(name)) {
      clearRamFile();
      copyFileName(ramFileName, name);
      ramFileActive = true;
      ramFileDirty = true;
    }
  }

  editorMode = true;
  Serial.print(F("Editing "));
  Serial.print(ramFileName);
  Serial.println(F(". Enter lines. Use .save or .quit"));
  Serial.print(F("edit> "));
}

void cmdSave(char *name) {
  if (!ramFileActive) {
    Serial.println(F("No RAM file active. Use new <name> or edit <name>."));
    return;
  }

  if (name == NULL) {
    name = ramFileName;
  }

  if (!isValidFileName(name)) {
    Serial.println(F("Usage: save <name>"));
    return;
  }

  if (ramFileSize > fsSlotSizeBytes()) {
    Serial.println(F("Warning: file truncated to EEPROM slot size."));
  }

  if (!fsSaveFromRam(name)) {
    Serial.println(F("Error: could not save file. EEPROM FS full?"));
    return;
  }

  Serial.print(F("Saved: "));
  Serial.println(ramFileName);

}


void cmdLoad(char *name) {
  if (!isValidFileName(name)) {
    Serial.println(F("Usage: load <name>"));
    return;
  }

  if (!fsLoadToRam(name)) {
    Serial.println(F("File not found."));
    return;
  }

  Serial.print(F("Loaded into RAM: "));
  Serial.print(ramFileName);
  Serial.print(F("  "));
  Serial.print(ramFileSize);
  Serial.println(F(" B"));
}

void cmdRun(char *name) {
  if (!isValidFileName(name)) {
    Serial.println(F("Usage: run <name>"));
    return;
  }

  char oldRamName[FS_NAME_LEN + 1];
  char oldRamBuffer[RAM_FILE_SIZE + 1];
  uint16_t oldRamSize = ramFileSize;
  bool oldRamActive = ramFileActive;
  bool oldRamDirty = ramFileDirty;

  copyFileName(oldRamName, ramFileName);
  memcpy(oldRamBuffer, ramFileBuffer, RAM_FILE_SIZE + 1);

  if (!fsLoadToRam(name)) {
    Serial.println(F("File not found."));
    return;
  }

  char scriptBuffer[RAM_FILE_SIZE + 1];
  uint16_t scriptSize = ramFileSize;
  memcpy(scriptBuffer, ramFileBuffer, RAM_FILE_SIZE + 1);

  copyFileName(ramFileName, oldRamName);
  memcpy(ramFileBuffer, oldRamBuffer, RAM_FILE_SIZE + 1);
  ramFileSize = oldRamSize;
  ramFileActive = oldRamActive;
  ramFileDirty = oldRamDirty;

  Serial.print(F("Running script: "));
  Serial.println(name);

  char line[INPUT_BUFFER_SIZE];
  uint16_t index = 0;
  uint16_t executed = 0;
  uint16_t skipped = 0;

  while (extractScriptLine(scriptBuffer, scriptSize, index, line)) {
    uint8_t loopCount;

    if (parseLoopStartLine(line, loopCount)) {
      uint16_t loopStartIndex = index;
      uint16_t scanIndex = index;
      uint16_t loopEndIndex = scriptSize;
      bool foundEnd = false;
      char scanLine[INPUT_BUFFER_SIZE];

      while (scanIndex < scriptSize) {
        uint16_t lineStartIndex = scanIndex;

        if (!extractScriptLine(scriptBuffer, scriptSize, scanIndex, scanLine)) {
          break;
        }

        uint8_t nestedCount;

        if (parseLoopStartLine(scanLine, nestedCount)) {
          Serial.println(F("! nested loops are not supported"));
          skipped++;
        }

        if (isLoopEndLine(scanLine)) {
          loopEndIndex = lineStartIndex;
          foundEnd = true;
          break;
        }
      }

      if (!foundEnd) {
        Serial.println(F("! loop start without loop end"));
        skipped++;
        break;
      }

      Serial.print(F("Loop start, count="));
      Serial.println(loopCount);

      for (uint8_t r = 0; r < loopCount; r++) {
        Serial.print(F("Loop iteration "));
        Serial.print(r + 1);
        Serial.print('/');
        Serial.println(loopCount);

        executeScriptRange(scriptBuffer, loopStartIndex, loopEndIndex, executed, skipped);
      }

      index = scanIndex;
    } else {
      executeScriptCommandLine(line, executed, skipped);
    }
  }

  Serial.print(F("Script done. Executed: "));
  Serial.print(executed);
  Serial.print(F(", skipped: "));
  Serial.println(skipped);
}

void cmdCat(char *name) {
  if (!isValidFileName(name)) {
    Serial.println(F("Usage: cat <name>"));
    return;
  }

  bool fromRam = ramFileActive && strncmp(ramFileName, name, FS_NAME_LEN) == 0;

  if (!fromRam) {
    if (!fsLoadToRam(name)) {
      Serial.println(F("File not found."));
      return;
    }
  }

  Serial.print(ramFileBuffer);
  if (ramFileSize == 0 || ramFileBuffer[ramFileSize - 1] != '\n') {
    Serial.println();
  }
}

void cmdRm(char *name) {
  if (!isValidFileName(name)) {
    Serial.println(F("Usage: rm <name>"));
    return;
  }

  int8_t slot = fsFindSlotByName(name);
  if (slot < 0) {
    Serial.println(F("File not found."));
    return;
  }

  fsClearEntry(slot);

  if (ramFileActive && strncmp(ramFileName, name, FS_NAME_LEN) == 0) {
    clearRamFile();
  }

  Serial.print(F("Removed: "));
  Serial.println(name);
}

void cmdMount(char *target) {
  if (target == NULL || strcmp(target, "status") == 0) {
    Serial.print(F("Mounted EEPROM FS: "));
    Serial.println(fsBackendName());
    Serial.print(F("External EEPROM 0x"));
    if (extEepromAddress < 16) Serial.print('0');
    Serial.print(extEepromAddress, HEX);
    Serial.print(F(": "));
    Serial.println(i2cDevicePresent(extEepromAddress) ? F("present") : F("not found"));
    return;
  }

  bool external;
  if (!parseBackend(target, external)) {
    Serial.println(F("Usage: mount status|int|ext"));
    return;
  }

  if (!switchFsBackend(external)) {
    Serial.println(F("Error: external EEPROM not found at 0x50."));
    return;
  }

  if (!fsHasValidMagic()) {
    Serial.println(F("Warning: mounted filesystem is not formatted. Use mkfs int|ext."));
  }

  clearRamFile();

  Serial.print(F("Mounted EEPROM FS: "));
  Serial.println(fsBackendName());
}

bool copyFileBetweenBackends(bool fromExternal, bool toExternal, const char *name, bool removeSource) {
  if (!isValidFileName(name)) {
    return false;
  }

  bool oldBackend = fsUseExternal;
  char oldRamName[FS_NAME_LEN + 1];
  char oldRamBuffer[RAM_FILE_SIZE + 1];
  uint16_t oldRamSize = ramFileSize;
  bool oldRamActive = ramFileActive;
  bool oldRamDirty = ramFileDirty;

  copyFileName(oldRamName, ramFileName);
  memcpy(oldRamBuffer, ramFileBuffer, RAM_FILE_SIZE + 1);

  if (!switchFsBackend(fromExternal)) {
    fsUseExternal = oldBackend;
    return false;
  }
  if (!fsHasValidMagic()) {
    switchFsBackend(oldBackend);
    copyFileName(ramFileName, oldRamName);
    memcpy(ramFileBuffer, oldRamBuffer, RAM_FILE_SIZE + 1);
    ramFileSize = oldRamSize;
    ramFileActive = oldRamActive;
    ramFileDirty = oldRamDirty;
    return false;
  }

  if (!fsLoadToRam(name)) {
    switchFsBackend(oldBackend);
    copyFileName(ramFileName, oldRamName);
    memcpy(ramFileBuffer, oldRamBuffer, RAM_FILE_SIZE + 1);
    ramFileSize = oldRamSize;
    ramFileActive = oldRamActive;
    ramFileDirty = oldRamDirty;
    return false;
  }

  char tmpName[FS_NAME_LEN + 1];
  char tmpBuffer[RAM_FILE_SIZE + 1];
  uint16_t tmpSize = ramFileSize;
  copyFileName(tmpName, ramFileName);
  memcpy(tmpBuffer, ramFileBuffer, RAM_FILE_SIZE + 1);

  if (tmpSize > (toExternal ? EXT_FS_SLOT_SIZE : INT_FS_SLOT_SIZE)) {
    switchFsBackend(oldBackend);
    copyFileName(ramFileName, oldRamName);
    memcpy(ramFileBuffer, oldRamBuffer, RAM_FILE_SIZE + 1);
    ramFileSize = oldRamSize;
    ramFileActive = oldRamActive;
    ramFileDirty = oldRamDirty;
    return false;
  }

  if (!switchFsBackend(toExternal)) {
    switchFsBackend(oldBackend);
    copyFileName(ramFileName, oldRamName);
    memcpy(ramFileBuffer, oldRamBuffer, RAM_FILE_SIZE + 1);
    ramFileSize = oldRamSize;
    ramFileActive = oldRamActive;
    ramFileDirty = oldRamDirty;
    return false;
  }
  if (!fsHasValidMagic()) {
    switchFsBackend(oldBackend);
    copyFileName(ramFileName, oldRamName);
    memcpy(ramFileBuffer, oldRamBuffer, RAM_FILE_SIZE + 1);
    ramFileSize = oldRamSize;
    ramFileActive = oldRamActive;
    ramFileDirty = oldRamDirty;
    return false;
  }

  copyFileName(ramFileName, tmpName);
  memcpy(ramFileBuffer, tmpBuffer, RAM_FILE_SIZE + 1);
  ramFileSize = tmpSize;
  ramFileActive = true;
  ramFileDirty = true;

  bool saved = fsSaveFromRam(name);

  if (saved && removeSource) {
    switchFsBackend(fromExternal);
    int8_t slot = fsFindSlotByName(name);
    if (slot >= 0) {
      fsClearEntry(slot);
    }
  }

  switchFsBackend(oldBackend);
  copyFileName(ramFileName, oldRamName);
  memcpy(ramFileBuffer, oldRamBuffer, RAM_FILE_SIZE + 1);
  ramFileSize = oldRamSize;
  ramFileActive = oldRamActive;
  ramFileDirty = oldRamDirty;

  return saved;
}

void cmdCp(char *fromText, char *toText, char *name) {
  bool fromExternal;
  bool toExternal;

  if (!parseBackend(fromText, fromExternal) || !parseBackend(toText, toExternal) || !isValidFileName(name)) {
    Serial.println(F("Usage: cp int|ext int|ext <name>"));
    return;
  }

  if (fromExternal && !i2cDevicePresent(extEepromAddress)) {
    Serial.println(F("Error: external EEPROM not found."));
    return;
  }

  if (toExternal && !i2cDevicePresent(extEepromAddress)) {
    Serial.println(F("Error: external EEPROM not found."));
    return;
  }

  if (!copyFileBetweenBackends(fromExternal, toExternal, name, false)) {
    Serial.println(F("Error: copy failed. File missing, filesystem unformatted, target full, or truncation would occur."));
    return;
  }

  Serial.println(F("Copy complete."));
}

void cmdMv(char *fromText, char *toText, char *name) {
  bool fromExternal;
  bool toExternal;

  if (!parseBackend(fromText, fromExternal) || !parseBackend(toText, toExternal) || !isValidFileName(name)) {
    Serial.println(F("Usage: mv int|ext int|ext <name>"));
    return;
  }

  if (fromExternal == toExternal) {
    Serial.println(F("Source and target are the same backend."));
    return;
  }

  if (fromExternal && !i2cDevicePresent(extEepromAddress)) {
    Serial.println(F("Error: external EEPROM not found."));
    return;
  }

  if (toExternal && !i2cDevicePresent(extEepromAddress)) {
    Serial.println(F("Error: external EEPROM not found."));
    return;
  }

  if (!copyFileBetweenBackends(fromExternal, toExternal, name, true)) {
    Serial.println(F("Error: move failed. File missing, filesystem unformatted, target full, or truncation would occur."));
    return;
  }

  Serial.println(F("Move complete."));
}

void cmdEeRead(char *backendText, char *addrText, char *lenText) {
  bool external;
  int addr;
  int len = 1;

  if (!parseBackend(backendText, external) || !parseIntSafe(addrText, addr)) {
    Serial.println(F("Usage: eeread int|ext <addr> [len]"));
    return;
  }

  if (lenText != NULL && !parseIntSafe(lenText, len)) {
    Serial.println(F("Usage: eeread int|ext <addr> [len]"));
    return;
  }

  if (addr < 0 || len < 1 || len > 32) {
    Serial.println(F("Error: addr must be >=0 and len 1..32."));
    return;
  }

  if (!external && addr + len > EEPROM.length()) {
    Serial.println(F("Error: internal EEPROM range out of bounds."));
    return;
  }

  if (external && (!i2cDevicePresent(extEepromAddress) || addr + len > EXT_EEPROM_SIZE_BYTES)) {
    Serial.println(F("Error: external EEPROM missing or range out of bounds."));
    return;
  }

  for (int i = 0; i < len; i++) {
    byte value = 0;
    if (external) {
      extEepromReadByte((uint16_t)(addr + i), value);
    } else {
      value = EEPROM.read(addr + i);
    }

    Serial.print(addr + i);
    Serial.print(F(": 0x"));
    printHexByte(value);
    Serial.print(F("  "));
    Serial.println(value);
  }
}

void cmdEeWrite(char *backendText, char *addrText, char *valueText) {
  bool external;
  int addr;
  int value;

  if (!parseBackend(backendText, external) || !parseIntSafe(addrText, addr) || !parseIntSafe(valueText, value)) {
    Serial.println(F("Usage: eewrite int|ext <addr> <value>"));
    return;
  }

  if (addr < 0 || value < 0 || value > 255) {
    Serial.println(F("Error: addr must be >=0 and value 0..255."));
    return;
  }

  if (!external && addr >= EEPROM.length()) {
    Serial.println(F("Error: internal EEPROM address out of bounds."));
    return;
  }

  if (external && (!i2cDevicePresent(extEepromAddress) || addr >= EXT_EEPROM_SIZE_BYTES)) {
    Serial.println(F("Error: external EEPROM missing or address out of bounds."));
    return;
  }

  bool ok = external ? extEepromUpdateByte((uint16_t)addr, (byte)value) : true;
  if (!external) {
    EEPROM.update(addr, (byte)value);
  }

  if (!ok) {
    Serial.println(F("Error: write failed."));
    return;
  }

  Serial.println(F("EEPROM byte written."));
}

void cmdI2CScan() {
  byte error;
  byte address;
  uint8_t count = 0;

  Serial.println(F("Scanning I2C bus..."));

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print(F("Found device at 0x"));

      if (address < 16) {
        Serial.print('0');
      }

      Serial.print(address, HEX);

      if (address == 0x68) {
        Serial.print(F("  DS3231/DS1307/RTC candidate"));
      } else if (address >= 0x50 && address <= 0x57) {
        Serial.print(F("  EEPROM candidate"));
      } else if (address == 0x27 || address == 0x3F) {
        Serial.print(F("  I2C LCD candidate"));
      }

      Serial.println();
      count++;
    } else if (error == 4) {
      Serial.print(F("Unknown error at 0x"));
      if (address < 16) {
        Serial.print('0');
      }
      Serial.println(address, HEX);
    }
  }

  if (count == 0) {
    Serial.println(F("No I2C devices found."));
  } else {
    Serial.print(F("Devices found: "));
    Serial.println(count);
  }

  Serial.println(F("Done."));
}

void cmdDate() {
  uint16_t year;
  uint8_t month, day, hour, minute, second;

  if (!readRtcDateTime(year, month, day, hour, minute, second)) {
    Serial.println(F("No RTC detected at 0x68. Use 'uptime' instead."));
    return;
  }

  printRtcDateTime(year, month, day, hour, minute, second);
}

void cmdRtcGet() {
  uint16_t year;
  uint8_t month, day, hour, minute, second;

  if (!readRtcDateTime(year, month, day, hour, minute, second)) {
    Serial.println(F("No RTC detected at 0x68."));
    return;
  }

  Serial.print(F("RTC: "));
  printRtcDateTime(year, month, day, hour, minute, second);

  byte status;
  if (readRegister(DS3231_ADDRESS, 0x0F, status)) {
    Serial.print(F("Status: 0x"));
    if (status < 16) Serial.print('0');
    Serial.print(status, HEX);
    if (status & 0x80) {
      Serial.print(F(" OSF=1"));
    }
    if (status & 0x02) {
      Serial.print(F(" A2F=1"));
    }
    if (status & 0x01) {
      Serial.print(F(" A1F=1"));
    }
    Serial.println();
  }

  byte control;
  if (readRegister(DS3231_ADDRESS, 0x0E, control)) {
    Serial.print(F("Control: 0x"));
    if (control < 16) Serial.print('0');
    Serial.println(control, HEX);
  }
}

void cmdRtcSet(char *dateText, char *timeText) {
  uint16_t year;
  uint8_t month, day, hour, minute, second;

  if (!parseDate(dateText, year, month, day) || !parseTime(timeText, hour, minute, second)) {
    Serial.println(F("Usage: rtcset YYYY-MM-DD HH:MM:SS"));
    return;
  }

  if (!writeRtcDateTime(year, month, day, hour, minute, second)) {
    Serial.println(F("Error: could not write RTC at 0x68."));
    return;
  }

  // Clear oscillator stop flag on DS3231 if present.
  byte status;
  if (readRegister(DS3231_ADDRESS, 0x0F, status)) {
    status &= ~0x80;
    writeRegister(DS3231_ADDRESS, 0x0F, status);
  }

  Serial.print(F("RTC set to: "));
  printRtcDateTime(year, month, day, hour, minute, second);
}

void cmdTemp() {
  if (!i2cDevicePresent(DS3231_ADDRESS)) {
    Serial.println(F("No DS3231 detected at 0x68."));
    return;
  }

  byte msb;
  byte lsb;
  if (!readRegister(DS3231_ADDRESS, 0x11, msb) || !readRegister(DS3231_ADDRESS, 0x12, lsb)) {
    Serial.println(F("Error: could not read DS3231 temperature registers."));
    return;
  }

  int8_t integerPart = (int8_t)msb;
  uint8_t fractionBits = (lsb >> 6) & 0x03;
  uint8_t fractionHundredths = fractionBits * 25;

  Serial.print(F("DS3231 temperature: "));
  Serial.print(integerPart);
  Serial.print('.');
  if (fractionHundredths < 10) {
    Serial.print('0');
  }
  Serial.print(fractionHundredths);
  Serial.println(F(" C"));
}

void cmdSqw(char *mode) {
  if (mode == NULL) {
    Serial.println(F("Usage: sqw status|off|1hz|1024hz|4096hz|8192hz"));
    return;
  }

  if (!i2cDevicePresent(DS3231_ADDRESS)) {
    Serial.println(F("No DS3231 detected at 0x68."));
    return;
  }

  byte control;
  if (!readRegister(DS3231_ADDRESS, 0x0E, control)) {
    Serial.println(F("Error: could not read DS3231 control register."));
    return;
  }

  if (strcmp(mode, "status") == 0) {
    Serial.print(F("SQW control: 0x"));
    if (control < 16) Serial.print('0');
    Serial.print(control, HEX);

    if (control & 0x04) {
      Serial.println(F(" SQW=off (INTCN=1)"));
    } else {
      byte rate = control & 0x18;
      Serial.print(F(" SQW="));
      if (rate == 0x00) {
        Serial.println(F("1Hz"));
      } else if (rate == 0x08) {
        Serial.println(F("1024Hz"));
      } else if (rate == 0x10) {
        Serial.println(F("4096Hz"));
      } else if (rate == 0x18) {
        Serial.println(F("8192Hz"));
      }
    }
    return;
  }

  if (strcmp(mode, "off") == 0) {
    control |= 0x04; // INTCN=1 disables square-wave output.
  } else if (strcmp(mode, "1hz") == 0) {
    control &= ~0x1C;
    control |= 0x00;
  } else if (strcmp(mode, "1024hz") == 0) {
    control &= ~0x1C;
    control |= 0x08;
  } else if (strcmp(mode, "4096hz") == 0) {
    control &= ~0x1C;
    control |= 0x10;
  } else if (strcmp(mode, "8192hz") == 0) {
    control &= ~0x1C;
    control |= 0x18;
  } else {
    Serial.println(F("Usage: sqw status|off|1hz|1024hz|4096hz|8192hz"));
    return;
  }

  if (!writeRegister(DS3231_ADDRESS, 0x0E, control)) {
    Serial.println(F("Error: could not write DS3231 control register."));
    return;
  }

  Serial.println(F("SQW updated."));
}

void cmdAlarm(char *mode, char *arg1, char *arg2, char *arg3) {
  if (mode == NULL) {
    Serial.println(F("Usage: alarm status|clear|off|once|sec SS|minsec MM SS|hms HH MM SS"));
    return;
  }

  if (!i2cDevicePresent(DS3231_ADDRESS)) {
    Serial.println(F("No DS3231 detected at 0x68."));
    return;
  }

  if (strcmp(mode, "status") == 0) {
    byte alarmRegs[4];
    byte control;
    byte status;

    if (!readRtcRegisters(0x07, alarmRegs, 4) || !readRegister(DS3231_ADDRESS, 0x0E, control) || !readRegister(DS3231_ADDRESS, 0x0F, status)) {
      Serial.println(F("Error: could not read DS3231 alarm registers."));
      return;
    }

    Serial.print(F("Alarm1 registers: 0x"));
    printHexByte(alarmRegs[0]);
    Serial.print(F(" 0x"));
    printHexByte(alarmRegs[1]);
    Serial.print(F(" 0x"));
    printHexByte(alarmRegs[2]);
    Serial.print(F(" 0x"));
    printHexByte(alarmRegs[3]);
    Serial.println();

    printAlarm1Mode(alarmRegs[0], alarmRegs[1], alarmRegs[2], alarmRegs[3]);

    Serial.print(F("Alarm1 interrupt: "));
    Serial.println((control & 0x01) ? F("enabled") : F("disabled"));

    Serial.print(F("Alarm flags: A1F="));
    Serial.print((status & 0x01) ? F("1") : F("0"));
    Serial.print(F(" A2F="));
    Serial.println((status & 0x02) ? F("1") : F("0"));
    return;
  }

  if (strcmp(mode, "clear") == 0) {
    if (!clearDs3231AlarmFlags()) {
      Serial.println(F("Error: could not clear DS3231 alarm flags."));
      return;
    }
    Serial.println(F("Alarm flags cleared."));
    return;
  }

  if (strcmp(mode, "off") == 0) {
    if (!setDs3231Alarm1Enabled(false)) {
      Serial.println(F("Error: could not disable DS3231 Alarm 1."));
      return;
    }
    Serial.println(F("Alarm 1 interrupt disabled."));
    return;
  }

  byte alarmRegs[4];

  if (strcmp(mode, "once") == 0) {
    alarmRegs[0] = 0x80;
    alarmRegs[1] = 0x80;
    alarmRegs[2] = 0x80;
    alarmRegs[3] = 0x80;
  } else if (strcmp(mode, "sec") == 0) {
    uint8_t second;
    if (!parseByteRange(arg1, 0, 59, second)) {
      Serial.println(F("Usage: alarm sec SS"));
      return;
    }
    alarmRegs[0] = decToBcd(second); // A1M1=0
    alarmRegs[1] = 0x80;
    alarmRegs[2] = 0x80;
    alarmRegs[3] = 0x80;
  } else if (strcmp(mode, "minsec") == 0) {
    uint8_t minute;
    uint8_t second;
    if (!parseByteRange(arg1, 0, 59, minute) || !parseByteRange(arg2, 0, 59, second)) {
      Serial.println(F("Usage: alarm minsec MM SS"));
      return;
    }
    alarmRegs[0] = decToBcd(second); // A1M1=0
    alarmRegs[1] = decToBcd(minute); // A1M2=0
    alarmRegs[2] = 0x80;
    alarmRegs[3] = 0x80;
  } else if (strcmp(mode, "hms") == 0) {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    if (!parseByteRange(arg1, 0, 23, hour) || !parseByteRange(arg2, 0, 59, minute) || !parseByteRange(arg3, 0, 59, second)) {
      Serial.println(F("Usage: alarm hms HH MM SS"));
      return;
    }
    alarmRegs[0] = decToBcd(second); // A1M1=0
    alarmRegs[1] = decToBcd(minute); // A1M2=0
    alarmRegs[2] = decToBcd(hour);   // A1M3=0, 24-hour mode
    alarmRegs[3] = 0x80;
  } else {
    Serial.println(F("Usage: alarm status|clear|off|once|sec SS|minsec MM SS|hms HH MM SS"));
    return;
  }

  if (!writeRtcRegisters(0x07, alarmRegs, 4)) {
    Serial.println(F("Error: could not write DS3231 Alarm 1 registers."));
    return;
  }

  if (!clearDs3231AlarmFlags()) {
    Serial.println(F("Warning: could not clear existing alarm flags."));
  }

  if (!setDs3231Alarm1Enabled(true)) {
    Serial.println(F("Error: could not enable DS3231 Alarm 1 interrupt."));
    return;
  }

  Serial.println(F("Alarm 1 configured and enabled."));
}

void blinkPin(uint8_t pin, int times) {
  if (times < 1) {
    times = 1;
  }

  pinMode(pin, OUTPUT);

  Serial.print(F("Blinking pin "));
  Serial.print(pin);
  Serial.print(F(" "));
  Serial.print(times);
  Serial.println(F(" time(s)."));

  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(200);
    digitalWrite(pin, LOW);
    delay(200);
  }
}

void cmdBlink(char *arg1, char *arg2) {
  int pin = LED_BUILTIN;
  int times = 1;

  if (arg1 != NULL) {
    if (!parseIntSafe(arg1, pin)) {
      Serial.println(F("Error: invalid pin."));
      return;
    }
  }

  if (arg2 != NULL) {
    if (!parseIntSafe(arg2, times)) {
      Serial.println(F("Error: invalid blink count."));
      return;
    }
  }

  if (!validPin(pin)) {
    Serial.println(F("Error: pin out of range."));
    return;
  }

  blinkPin((uint8_t)pin, times);
}

void cmdPin(char *action, char *pinText) {
  if (action == NULL || pinText == NULL) {
    Serial.println(F("Usage: pin read|high|low|input|output <pin>"));
    return;
  }

  int pin;
  if (!parseIntSafe(pinText, pin) || !validPin(pin)) {
    Serial.println(F("Error: invalid pin."));
    return;
  }

  if (strcmp(action, "read") == 0) {
    pinMode(pin, INPUT);
    int state = digitalRead(pin);

    Serial.print(F("Pin "));
    Serial.print(pin);
    Serial.print(F(" = "));
    Serial.println(state == HIGH ? F("HIGH") : F("LOW"));
  } else if (strcmp(action, "high") == 0) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);

    Serial.print(F("Pin "));
    Serial.print(pin);
    Serial.println(F(" HIGH"));
  } else if (strcmp(action, "low") == 0) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);

    Serial.print(F("Pin "));
    Serial.print(pin);
    Serial.println(F(" LOW"));
  } else if (strcmp(action, "input") == 0) {
    pinMode(pin, INPUT);

    Serial.print(F("Pin "));
    Serial.print(pin);
    Serial.println(F(" set to INPUT"));
  } else if (strcmp(action, "output") == 0) {
    pinMode(pin, OUTPUT);

    Serial.print(F("Pin "));
    Serial.print(pin);
    Serial.println(F(" set to OUTPUT"));
  } else {
    Serial.println(F("Error: unknown pin action."));
    Serial.println(F("Usage: pin read|high|low|input|output <pin>"));
  }
}

// ------------------------------------------------------------
// Command parser
// ------------------------------------------------------------
void executeCommand(char *line) {
  char *cmd = strtok(line, " ");
  char *arg1 = strtok(NULL, " ");
  char *arg2 = strtok(NULL, " ");
  char *arg3 = strtok(NULL, " ");
  char *arg4 = strtok(NULL, " ");
  char *rest = strtok(NULL, "");

  if (cmd == NULL) {
    return;
  }

  if (strcmp(cmd, "echo") == 0 && arg1 != NULL && rest != NULL) {
    Serial.print(arg1);
    Serial.print(' ');
    Serial.println(rest);
    return;
  }

  if (strcmp(cmd, "help") == 0) {
    cmdHelp();
  } else if (strcmp(cmd, "uptime") == 0) {
    cmdUptime();
  } else if (strcmp(cmd, "free") == 0) {
    cmdFree();
  } else if (strcmp(cmd, "pwd") == 0) {
    cmdPwd();
  } else if (strcmp(cmd, "echo") == 0) {
    cmdEcho(arg1);
  } else if (strcmp(cmd, "sleep") == 0) {
    cmdSleep(arg1);
  } else if (strcmp(cmd, "ls") == 0) {
    cmdLs();
  } else if (strcmp(cmd, "df") == 0) {
    cmdDf();
  } else if (strcmp(cmd, "mkfs") == 0) {
    cmdMkfs(arg1);
  } else if (strcmp(cmd, "mount") == 0) {
    cmdMount(arg1);
  } else if (strcmp(cmd, "cp") == 0) {
    cmdCp(arg1, arg2, arg3);
  } else if (strcmp(cmd, "mv") == 0) {
    cmdMv(arg1, arg2, arg3);
  } else if (strcmp(cmd, "eeread") == 0) {
    cmdEeRead(arg1, arg2, arg3);
  } else if (strcmp(cmd, "eewrite") == 0) {
    cmdEeWrite(arg1, arg2, arg3);
  } else if (strcmp(cmd, "new") == 0) {
    cmdNew(arg1);
  } else if (strcmp(cmd, "edit") == 0) {
    cmdEdit(arg1);
  } else if (strcmp(cmd, "save") == 0) {
    cmdSave(arg1);
  } else if (strcmp(cmd, "load") == 0) {
    cmdLoad(arg1);
  } else if (strcmp(cmd, "run") == 0) {
    cmdRun(arg1);
  } else if (strcmp(cmd, "cat") == 0) {
    cmdCat(arg1);
  } else if (strcmp(cmd, "rm") == 0) {
    cmdRm(arg1);
  } else if (strcmp(cmd, "i2cscan") == 0) {
    cmdI2CScan();
  } else if (strcmp(cmd, "date") == 0) {
    cmdDate();
  } else if (strcmp(cmd, "rtcget") == 0) {
    cmdRtcGet();
  } else if (strcmp(cmd, "rtcset") == 0) {
    cmdRtcSet(arg1, arg2);
  } else if (strcmp(cmd, "temp") == 0) {
    cmdTemp();
  } else if (strcmp(cmd, "sqw") == 0) {
    cmdSqw(arg1);
  } else if (strcmp(cmd, "alarm") == 0) {
    cmdAlarm(arg1, arg2, arg3, arg4);
  } else if (strcmp(cmd, "blink") == 0) {
    cmdBlink(arg1, arg2);
  } else if (strcmp(cmd, "pin") == 0) {
    cmdPin(arg1, arg2);
  } else {
    Serial.print(F("Unknown command: "));
    Serial.println(cmd);
    Serial.println(F("Type 'help' for commands."));
  }

  if (rest != NULL && strcmp(cmd, "alarm") != 0) {
    Serial.println(F("Note: extra arguments ignored."));
  }
}

// ------------------------------------------------------------
// Serial line input
// ------------------------------------------------------------
void handleSerialInput() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      inputBuffer[inputPos] = '\0';

      Serial.println();

      if (editorMode) {
        if (strcmp(inputBuffer, ".save") == 0) {
          editorMode = false;
          cmdSave(ramFileName);
          inputPos = 0;
          printPrompt();
        } else if (strcmp(inputBuffer, ".quit") == 0) {
          editorMode = false;
          inputPos = 0;
          Serial.println(F("Editor closed."));
          printPrompt();
        } else if (strcmp(inputBuffer, ".show") == 0) {
          printEditorBufferNumbered();
          inputPos = 0;
          Serial.print(F("edit> "));
        } else if (strcmp(inputBuffer, ".clear") == 0) {
          ramFileBuffer[0] = '\0';
          ramFileSize = 0;
          ramFileDirty = true;
          inputPos = 0;
          Serial.println(F("Buffer cleared."));
          Serial.print(F("edit> "));
        } else if (strncmp(inputBuffer, ".del", 4) == 0) {
          uint16_t lineNumber;
          char *unusedText;
          if (!parseEditorLineCommand(inputBuffer, ".del", lineNumber, unusedText)) {
            Serial.println(F("Usage: .del N"));
          } else if (!editorDeleteLine(lineNumber)) {
            Serial.println(F("Error: line not found."));
          } else {
            Serial.println(F("Line deleted."));
          }
          inputPos = 0;
          Serial.print(F("edit> "));
        } else if (strncmp(inputBuffer, ".set", 4) == 0) {
          uint16_t lineNumber;
          char *text;
          if (!parseEditorLineCommand(inputBuffer, ".set", lineNumber, text)) {
            Serial.println(F("Usage: .set N TEXT"));
          } else if (!editorSetLine(lineNumber, text)) {
            Serial.println(F("Error: line not found or buffer full."));
          } else {
            Serial.println(F("Line updated."));
          }
          inputPos = 0;
          Serial.print(F("edit> "));
        } else if (strncmp(inputBuffer, ".ins", 4) == 0) {
          uint16_t lineNumber;
          char *text;
          if (!parseEditorLineCommand(inputBuffer, ".ins", lineNumber, text)) {
            Serial.println(F("Usage: .ins N TEXT"));
          } else if (!editorInsertLine(lineNumber, text)) {
            Serial.println(F("Error: invalid line or buffer full."));
          } else {
            Serial.println(F("Line inserted."));
          }
          inputPos = 0;
          Serial.print(F("edit> "));
        } else {
          appendEditorLine(inputBuffer);
          inputPos = 0;
          Serial.print(F("edit> "));
        }
      } else {
        if (inputPos > 0) {
          executeCommand(inputBuffer);
        }

        inputPos = 0;
        printPrompt();
      }
    } else if (c == '\b' || c == 127) {
      if (inputPos > 0) {
        inputPos--;
        Serial.print(F("\b \b"));
      }
    } else {
      if (inputPos < INPUT_BUFFER_SIZE - 1) {
        inputBuffer[inputPos++] = c;
        Serial.print(c);
      } else {
        Serial.println();
        Serial.println(F("Error: input line too long."));
        inputPos = 0;
        if (editorMode) {
          Serial.print(F("edit> "));
        } else {
          printPrompt();
        }
      }
    }
  }
}

// ------------------------------------------------------------
// Arduino setup / loop
// ------------------------------------------------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  Wire.begin();
  Wire.setClock(100000L);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  fsFormatIfNeeded();
  clearRamFile();

  delay(300);
  printBootMessage();
}

void loop() {
  handleSerialInput();
}
