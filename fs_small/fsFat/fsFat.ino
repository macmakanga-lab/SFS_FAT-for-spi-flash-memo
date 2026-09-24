#define MANI_ADDRESS 0

void flash_REMS(){ //Read Manufacture ID/ Device ID (REMS) (90H) 
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x90); //Sending function
  
  SPI.transfer((MANI_ADDRESS >> 16) & 0xFF);
  SPI.transfer((MANI_ADDRESS >>  8) & 0xFF);
  SPI.transfer(MANI_ADDRESS         & 0xFF);

  ManufacturerID = SPI.transfer(0x00);
  deviceID = SPI.transfer(0x00);
  digitalWrite(CS_PIN, HIGH);

}

void flash_read_id(){ //Read Identification (RDID) (9FH)  
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x9F); //Sending function

  ManufacturerID = SPI.transfer(0x00);
  memory_type = SPI.transfer(0x00);
  capacity = SPI.transfer(0x00);
  digitalWrite(CS_PIN, HIGH);

}
void flash_read_status(){
  
}