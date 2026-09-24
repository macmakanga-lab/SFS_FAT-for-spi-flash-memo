#include <SPI.h>
#include <string.h>

// ====================== CONFIGURATION ======================
#define FLASH_SIZE 1048576UL
#define FLASH_STARTING 0  //ASSUMNING THE STARTING ADDRESS OF THE MEMORY IS
#define SECTOR_SIZE 4096
#define PAGE_SIZE 256
#define DIR_ENTRIES 128
#define DIR_ENTRY_SIZE 32
#define DIR_TABLE_SIZE (DIR_ENTRIES * DIR_ENTRY_SIZE)
#define DATA_START DIR_TABLE_SIZE

#define STATUS_FREE 0xFF
#define STATUS_USED 0x01
#define STATUS_DELETED 0x00

// ====================== STRUCTURES ======================
struct __attribute__((packed)) DirEntry {
  uint8_t dir_index;
  char file_name[21];
  uint8_t file_index;
  uint32_t file_start_address;
  uint32_t size;
  uint8_t dir_status;
};

// ========== FAT BANNER ===============================
struct__attribute__((packed)) FatBanner{
  
}
// ====================== GLOBALS ======================
const uint8_t CS_PIN = 8;
const uint8_t LED_PIN = 6;

uint32_t next_free_address = DATA_START;
uint32_t address_last_file = 0;

// ====================== SPI PRIMITIVES ======================
void flash_write_enable() {
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x06);
  digitalWrite(CS_PIN, HIGH);
}
void flash_wait_ready() {
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x05);
  unsigned long start = millis();
  while (SPI.transfer(0x00) & 0x01) {
    digitalWrite(LED_PIN, HIGH);
    if (millis() - start > 1000) break;
  }
  digitalWrite(LED_PIN, LOW);
  digitalWrite(CS_PIN, HIGH);
}
void flash_sector_erase(uint32_t addr) {
  flash_write_enable();
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x20);
  SPI.transfer((addr >> 16) & 0xFF);
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);
  digitalWrite(CS_PIN, HIGH);
  flash_wait_ready();
}
void flash_page_program(uint32_t addr, const uint8_t *data, uint16_t len) {
  flash_write_enable();
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x20);
  SPI.transfer((addr >> 16) & 0xFF);
  SPI.transfer((addr >> 8) & 0XFF);
  SPI.transfer(addr & 0xFF);
  for (uint16_t i = 0; i < len; i++) {
    SPI.transfer(data[i]);
  }
  digitalWrite(CS_PIN, HIGH);
  flash_wait_ready();
}
uint8_t flash_read_byte(uint32_t addr) {
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x03);
  SPI.transfer((addr >> 16) & 0xFF);
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(CS_PIN, HIGH);
  return val;
}
uint32_t flash_read_u32(uint32_t addr) {
  uint32_t val = 0;
  val |= ((uint32_t)flash_read_byte(addr)) << 24;
  val |= ((uint32_t)flash_read_byte(addr + 1)) << 16;
  val |= ((uint32_t)flash_read_byte(addr + 2)) << 8;
  val |= ((uint32_t)flash_read_byte(addr + 3));
  return val;
}
void flash_read_buffer(uint32_t addr, uint8_t *buf, uint32_t len) {
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x03);  //Read command
  // sending address to the
  SPI.transfer((addr >> 16) & 0xFF);
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);
  //reading len times
  for (uint32_t i = 0; i < len; i++) {
    buf[i] = SPI.transfer(0x00);
  }
  digitalWrite(CS_PIN, HIGH);
}

// ====================== FILE SYSTEM ======================
void fs_format() {
  for (uint32_t addr = 0; addr < DIR_TABLE_SIZE; addr += SECTOR_SIZE) {
    flash_sector_erase(addr);
  }
  next_free_address = DATA_START;
  address_last_file = 0;
}

/*create File*/
bool fs_create_file(const char *name, uint32_t size) {
  if (strlen(name) > 20) return false;

  // Find free entry
  int free_entry = -1;
  for (int i = 0; i < DIR_ENTRIES; i++) {
    if (flash_read_byte(i * DIR_ENTRY_SIZE + 31) == STATUS_FREE) {
      free_entry = i;
      break;
    }
  }
  if (free_entry < 0) return false;

  // Compute address
  uint32_t file_addr;
  if (address_last_file == 0) {
    file_addr = DATA_START;
  } else {
    uint32_t prev_start = flash_read_u32(address_last_file + 23);
    uint32_t prev_size = flash_read_u32(address_last_file + 27);
    file_addr = prev_start + prev_size;
    // Page align
    uint32_t rem = file_addr % PAGE_SIZE;
    if (rem) file_addr += (PAGE_SIZE - rem);
  }

  if (file_addr + size > FLASH_SIZE) return false;

  // Build and write entry
  DirEntry entry;
  memset(&entry, 0xFF, sizeof(entry));
  entry.dir_index = free_entry;
  strncpy(entry.file_name, name, 20);
  entry.file_name[20] = '\0';
  entry.file_index = free_entry;
  entry.file_start_address = file_addr;
  entry.size = size;
  entry.dir_status = STATUS_USED;

  uint32_t entry_addr = free_entry * DIR_ENTRY_SIZE;
  flash_page_program(entry_addr, (uint8_t *)&entry, sizeof(entry));

  address_last_file = entry_addr;
  next_free_address = file_addr + size;
  return true;
}

/*findFile*/
int fs_find_file(const char *name) {
  for (int i = 0; i < DIR_ENTRIES; i++) {
    uint32_t addr = i * DIR_ENTRY_SIZE;
    if (flash_read_byte(addr + 31) != STATUS_USED) continue;

    char entry_name[21] = { 0 };
    flash_read_buffer(addr + 1, (uint8_t *)entry_name, 20);

    if (strcmp(entry_name, name) == 0) {
      return i;
    }
  }
  return -1;
}

/*write file*/
bool fs_write2_file(const char *name, uint32_t offset, const uint8_t *data, uint32_t len) {
  // Find file in directory
  int entry = fs_find_file(name);
  if (entry < 0) return false;

  uint32_t entry_addr = entry * DIR_ENTRY_SIZE;
  uint32_t file_start = flash_read_u32(entry_addr + 23);
  uint32_t file_size = flash_read_u32(entry_addr + 27);

  // Validate write range
  if (offset + len > file_size) return false;

  // Compute physical address
  uint32_t write_addr = file_start + offset;

  // Handle page boundary crossing
  while (len > 0) {
    uint32_t page_offset = write_addr % PAGE_SIZE;
    uint32_t page_remainder = PAGE_SIZE - page_offset;
    uint32_t chunk = (len < page_remainder) ? len : page_remainder;

    flash_page_program(write_addr, data, chunk);

    data += chunk;
    write_addr += chunk;
    len -= chunk;
  }

  return true;
}

/*read file*/
bool fs_read_file(const char *name, uint32_t offset, uint8_t *buffer, uint32_t len) {
  int entry = fs_find_file(name);
  if (entry < 0) return false;

  uint32_t entry_addr = entry * DIR_ENTRY_SIZE;
  uint32_t file_start = flash_read_u32(entry_addr + 23);
  uint32_t file_size = flash_read_u32(entry_addr + 27);

  if (offset + len > file_size) return false;
  uint32_t read_addr = file_start + offset;

  /*reading data*/
  flash_read_buffer(read_addr, buffer, len);
  return true;
}

bool fat_initailizing() {
  /*check for the file system signature assuming the first address is 0*/
  /*
  coding to check if the chip is known to the file system
  */
  //if new format the chip THE initialize



}


// ====================== SETUP ======================
void setup() {
  pinMode(CS_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);

  SPI.begin();
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

  // Initialize file system state from flash
  // fs_scan_directory();  // Implement this to populate next_free_address
}

void loop() {
}
