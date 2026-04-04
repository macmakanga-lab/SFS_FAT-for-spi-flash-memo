#include <SPI.h>
#define file_table_start_address  0
#define flash_size 1048576
#define block_size 131072
#define sector_size 4096
#define page_size 256
#define file_entry 32
// ====================== DIRECTORY ENTRY STRUCTURE (32 bytes exactly) ======================
  struct directoryentry{
    uint8_t dir_index;                    // 1 byte : 0xFF = free, 0x01 = used, 0x00 = deleted
    char file_name[21];                   // 21 bytes (max 20 chars + null terminator)
    uint8_t file_index;                   // 1 byte
    uint32_t file_start_address;          // 1 byte
    uint32_t size;                        // 4 bytes
    uint8_t dir_status;                   // 4 bytes
  }__attridute__((packed));               // 1 byte
// ====================== RUNTIME VARIABLES (all sized correctly) ======================
  bool valid;
  uint32_t address_last_file;
  uint32_t previous_starting_addreess;
  uint32_t previous_size;
  uint32_t previous_starting_address;
  uint8_t data;


// ====================== PINS ======================
const uint8_t cs = 8;
const uint8_t led = 6;
//===================FUNCTIONS=================================================


void setup() {
  // put your setup code here, to run once:
  pinMode(cs, OUTPUT);
  pinMode(led, OUTPUT);
  
  pinMode(2, OUTPUT);
   digitalWrite(2, HIGH); // Uncomment this line to use pin 2 to pull up (e.g.) the WP/IO2 pin
  pinMode(3, OUTPUT); 
   digitalWrite(3, HIGH); // Uncomment this line to use pin 3 to pull up (e.g.) the HOLD/IO3 pin
   

  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
}

void loop() {
  // put your main code here, to run repeatedly:



}

  
  void reading_dir(){

  }


  void initializing_file_table(){
     address = file_table_start_address;
    sector_erase();
     dir_index=  0;
     valid = 1;
   //indexing sector
   for(int i = 0; i < 128; i++){
     dir_index = i;
    index_pp();
     address = address + 31;
     data = 255;
    page_program();
     address = address + 1;
   }

  }

  void scanning_file_table(){
       address = file_table_start_address;
    while(dir_index < 128){
      index_read();
       address = address + 31;
      read_command();
      dir_status = data;
       page_program();
       address = address + 1;
       if(dir_status==255){ 
        valid = 0;// dir not occupied
       }else{
        valid = 1;/// dir occupied
       }
       /*Going to add some more functionality to this function which will be respond for verifying the memory  */
    }
  }

  void create_file(){
     address = file_table_start_address;
    while(dir_index < 128){
      index_read();
        address = address + 31;
      read_command();
        dir_status = data;
      if(dir_status==255){ 
        valid = 0;// dir not occupied
        
      }else{
        valid = 1;/// dir occupied
      }
       if(valid == 0){
        dir_index = file_index;
        dir_index = 128;
       }
       if(valid == 0){
       address = address + 1;
       }
    }
    // storing file index
    address = address - 30; 
    // index_pp();

      for(int x = 0; x < 21 ; x++){
        data = file_name[x];
         page_program();
        address = address + 1;
      }
      //address = address + 1;
      index_pp();
      address = address + 1;
      fetch_fs_address();
       //storing file_starting address
        // in future version file index is part of meta used in data verification
        write_enable();
        digitalWrite(cs, LOW);
        SPI.transfer(0x02);
        SPI.transfer((address >> 16) & 0xFF);
        SPI.transfer((address >> 8)  & 0xFF);
        SPI.transfer(address & 0xFF);
        //data in transit:file starting address
        SPI.transfer((fs_address >> 24) & 0xFF);
        SPI.transfer((fs_address >> 16) & 0xFF);
        SPI.transfer((fs_address >> 8)  & 0xFF);
        SPI.transfer(fs_address & 0xFF);
        digitalWrite(cs, HIGH); 
       wip();
       //
      address = address + 3; 
        //storing file size
        write_enable();
        digitalWrite(cs, LOW);
        SPI.transfer(0x02);
        SPI.transfer((address >> 16) & 0xFF);
        SPI.transfer((address >> 8)  & 0xFF);
        SPI.transfer(address & 0xFF);
        //data in transit:file size
        SPI.transfer((file_size >> 24) & 0xFF);
        SPI.transfer((file_size >> 16) & 0xFF);
        SPI.transfer((file_size >> 8)  & 0xFF);
        SPI.transfer(file_size & 0xFF);
        digitalWrite(cs, HIGH); 
       wip(); 
      address = address + 3;
      //storing dir_status
      dir_status = 1;
      data = dir_status;
      page_program();

  }

  void fetch_fs_address(){
    //position 27 
    //go back tho the previous dir_entry fetch 
    //it's starting address to integrate the starting address of the new file
    address = address_last_file - 31;
    if(address > 27){
      read_x4();
      data = previous_starting_address; 
      address = address + 1;
      read_x4();
      data = previous_size;
      fs_address = (previous_starting_address + previous_size) - 1;
    }else{
      fs_address = 4096;
    }
  }

  void index_read(){ 

    }

  void index_pp(){
     write_enable();
    digitalWrite(cs, LOW);
    SPI.transfer(0x02);
      SPI.transfer((address >> 16) & 0xFF);
      SPI.transfer((address >> 8)  & 0xFF);
      SPI.transfer(address & 0xFF);
      //data in transit
      SPI.transfer(dir_index);
    digitalWrite(cs, HIGH); 
     wip();
  }


  void block_erase(){
    write_enable();
    digitalWrite(cs, LOW);
    SPI.transfer(0xD8);
    digitalWrite(cs, HIGH);
    wip();
  }

  void sector_erase(){
    write_enable();
    digitalWrite(cs, LOW);
    SPI.transfer(0x20);//command
    byte addr1 = (address >> 16) & 0xFF;
    byte addr2 = (address >> 8)  & 0xFF;
    byte addr3 = (address)       & 0xFF;
    SPI.transfer(addr1);
    SPI.transfer(addr2);
    SPI.transfer(addr3);
    digitalWrite(cs, HIGH);
    wip(); 
  }
  

  void page_program(){
    write_enable();
    digitalWrite(cs, LOW);

    SPI.transfer(0x02);

      SPI.transfer((address >> 16) & 0xFF);
      SPI.transfer((address >> 8)  & 0xFF);
      SPI.transfer(address & 0xFF);

      SPI.transfer(data);
    digitalWrite(cs, HIGH); 

    wip();
  }

  void write_enable(){
    digitalWrite(cs, LOW);
    SPI.transfer(0x06);
    digitalWrite(cs, HIGH);
  }


  void wip(){
     digitalWrite(cs, LOW);
     SPI.transfer(0x05);
     unsigned long start = millis();
    while (SPI.transfer(0x00) & 0x01) {
        digitalWrite(led, HIGH);

        if (millis() - start > 1000) {
            break;  // Safety exit
        }
    }
    digitalWrite(led, LOW);
    digitalWrite(cs, HIGH);
  }


  void read_command(){
   digitalWrite(cs, LOW);
   SPI.transfer(0x03);
   SPI.transfer((address >> 16) & 0xFF);
   SPI.transfer((address >> 8) & 0xFF);
   SPI.transfer(address 0xFF);
   data = SPI.transfer(0x00);
   digitalWrite(cs, HIGH);

  }

  uint8_t read_byte(uint32_t addr){
   digitalWrite(cs, LOW);
   SPI.transfer(0x03);
   SPI.transfer((addr >> 16) & 0xFF);
   SPI.transfer((addr >>  8) & 0xFF);
   SPI.transfer(addr           0xFF);
   uint8_t data = SPI.transfer(0x00);
   digitalWrite(cs, HIGH);
   return data;
  }

  



