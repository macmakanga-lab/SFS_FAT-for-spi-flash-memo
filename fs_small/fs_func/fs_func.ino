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
#define BANNER_START 0
#define BANNER_SIZE SECTOR_SIZE // whole sector
#define DIR_TABLE_START BANNER_SIZE
#define DATA_START (DIR_TABLE_START + DIR_TABLE_SIZE)


#define FS_VALUE 0x46415431UL
#define FS_SERIAL 1

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
struct __attribute__((packed)) FatBanner{
  uint32_t value; //value identifying this chip by filesystem
  uint8_t  serial_no;  // for the system to id itself
}
// ====================== GLOBALS ======================
const uint8_t CS_PIN = 8;
const uint8_t LED_PIN = 6;

uint32_t next_free_address = DATA_START;
uint32_t address_last_file = 0;
bool has_file = false;

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
  SPI.transfer(0x02);  // Page Program (was 0x20 - Sector Erase - bug)
  SPI.transfer((addr >> 16) & 0xFF);
  SPI.transfer((addr >> 8) & 0XFF);
  SPI.transfer(addr & 0xFF);
  for (uint16_t i = 0; i < len; i++) {
    SPI.transfer(data[i]);
  }
  digitalWrite(CS_PIN, HIGH);
  flash_wait_ready();
}

// ---- Read Identification (RDID, 9Fh) ----
// Returns Manufacturer ID, Memory Type, Capacity in the 3 bytes pointed to by id[0..2].
// For GD25Q80E: id[0]=0xC8 (GigaDevice), id[1]=0x40, id[2]=0x14 (8Mbit).
void flash_read_id(uint8_t *id) {
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x9F);
  id[0] = SPI.transfer(0x00);  // Manufacturer ID
  id[1] = SPI.transfer(0x00);  // Memory Type
  id[2] = SPI.transfer(0x00);  // Capacity
  digitalWrite(CS_PIN, HIGH);
}

// ---- Read Status Register (RDSR, 05h) ----
// Bit0 = WIP (busy), Bit1 = WEL, Bits2-5 = BP0-BP3 (block protect), Bit6 = QE (chip-dependent), Bit7 = SRP0
uint8_t flash_read_status() {
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x05);
  uint8_t status = SPI.transfer(0x00);
  digitalWrite(CS_PIN, HIGH);
  return status;
}

// ---- Write Status Register (WRSR, 01h) ----
// Used to clear BP0-BP3 so erase/program isn't silently blocked by protected sectors.
// Requires WREN first; WEL auto-clears once the write completes.
void flash_write_status(uint8_t status) {
  flash_write_enable();
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x01);
  SPI.transfer(status);
  digitalWrite(CS_PIN, HIGH);
  flash_wait_ready();
}

// ---- Write Disable (WRDI, 04h) ----
// Not required after a normal write/erase (WEL clears automatically), but useful
// to force WEL low if you abort a sequence partway through.
void flash_write_disable() {
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x04);
  digitalWrite(CS_PIN, HIGH);
}

// ---- Chip Erase (60h) ----
// Wipes the entire flash. Much faster than looping flash_sector_erase() over
// the whole chip, but obviously destroys everything, not just the dir table.
void flash_chip_erase() {
  flash_write_enable();
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x60);
  digitalWrite(CS_PIN, HIGH);
  flash_wait_ready();
}

// ---- Software Reset (66h then 99h) ----
// Recovers a wedged device without a power cycle. Must be issued as two
// separate commands back-to-back with nothing else in between.
void flash_reset() {
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x66);
  digitalWrite(CS_PIN, HIGH);
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x99);
  digitalWrite(CS_PIN, HIGH);
  delayMicroseconds(50); // give the device time to reset internally
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

//The fs_erase_range is used for erasing just the sectors a specfically needed for writing
void fs_erase_range(uint32_t addr, uint32_t len){
  uint32_t sector = addr - (addr % SECTOR_SIZE);
  uint32_t end = addr + len;
  for(; sector < end; sector += SECTOR_SIZE){
    flash_sector_erase(sector);
  }
}

void fs_format() {
  flash_chip_erase();
  next_free_address = DATA_START;
  address_last_file = 0;
  has_file = false;
}


bool fs_create_file(const char *name, uint32_t size) {
  if (strlen(name) > 20) return false;

  // Find free entry
  int free_entry = -1;
  for (int i = 0; i < DIR_ENTRIES; i++) {
    if (flash_read_byte(DIR_TABLE_START + i * DIR_ENTRY_SIZE + 31) == STATUS_FREE) {
      free_entry = i;
      break;
    }
  }
  if (free_entry < 0) return false;

  // Compute address
  uint32_t file_addr;
  if (!has_file) {
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
  // prepares the space for the file 
  if (size > 0) fs_erase_range(file_addr, size); 

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
  has_file = true;
  next_free_address = file_addr + size;
  return true;
}


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

//
bool fs_read_file(const char *name, uint32_t offset, uint8_t *buffer, uint32_t len) {
  int entry = fs_find_file(name);
  if (entry < 0) return false;

  uint32_t entry_addr = DIR_TABLE_START + entry * DIR_ENTRY_SIZE;
  uint32_t file_start = flash_read_u32(entry_addr + 23);
  uint32_t file_size = flash_read_u32(entry_addr + 27);

  if (offset + len > file_size) return false;
  uint32_t read_addr = file_start + offset;

  //reading data
  flash_read_buffer(read_addr, buffer, len);
  return true;
}

void fs_scan_directory(){
  uint32_t max_end = DATA_START;
  Uuint32_t last_entry_addr = 0;
  bool found_any = false;
  size8_t i;
  for (; = 0; i < DIR_ENTRIES; i++){
    uint32 addr = DIR_TABLE_START + i * DIR_ENTRY_SIZE;
    if(flash_read_byte(addr + 31) != STATUS_USED) countie;

    uint32_t f_start = flash_read_u32(addr + 23);
    uint32_t f_size = flash_read_u32(addr + 27);
    uint32_t f_end = start + size;

    if (f_end > max_end){
      max_end = f_end;
      last_entry_addr = addr;
    }
    found_any = true;
  }

  has_file  = found_any;
  address_last_file = found_any ? last_entry_addr : 0;
  next_free_address = max_end;
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
  fs_scan_directory();  // Implement this to populate next_free_address
}

void loop() {
}
