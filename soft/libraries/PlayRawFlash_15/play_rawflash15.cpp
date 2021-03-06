/* Audio Library for Teensy 3.X
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Plays from Serial Flash (c) Frank Bösing, 2014/12, 2015

#include "play_rawflash15.h"
#include <arm_math.h>

extern "C" {
extern const int16_t ulaw_decode_table[256];
};



#define STAT_WIP 1
#define STAT_WEL 2

#define CMD_WRITE_STATUS_REG   0x01
#define CMD_PAGE_PROGRAM       0x02
#define CMD_READ_DATA          0x03
#define CMD_WRITE_DISABLE      0x04
#define CMD_READ_STATUS_REG    0x05
#define CMD_WRITE_ENABLE       0x06
#define CMD_READ_HIGH_SPEED    0x0B
#define CMD_SECTOR_ERASE       0x20
#define CMD_BLOCK32K_ERASE     0x52
#define CMD_RESET_ENABLE       0x66
#define CMD_RESET_DEVICE       0x99
#define CMD_READ_ID            0x9F
#define CMD_RELEASE_POWER_DOWN 0xAB
#define CMD_POWER_DOWN         0xB9
#define CMD_CHIP_ERASE         0xC7
#define CMD_BLOCK64K_ERASE     0xD8

/*	========================================================================================
				SPIFIFO.h
	======================================================================================*/


#define SPICLOCK_MAX ((SPI_CTAR_PBR(0) | SPI_CTAR_BR(0) | SPI_CTAR_DBR)) // F_BUS/2 MHz


void AudioPlaySerialFlash::flashinit(void)
{
#if AUDIOBOARD == 1
	pinMode(10,OUTPUT);
	digitalWrite(10, HIGH);
	SPI.setMOSI(7);
	SPI.setMISO(12);
	SPI.setSCK(14);
#else
	SPI.setMOSI(7);
	SPI.setMISO(12);
	SPI.setSCK(13);
#endif
	SPIFIFO.begin(SERFLASH_CS, SPICLOCK_MAX);
	//spisettings = SPISettings(SPICLOCK_MAX, MSBFIRST, SPI_MODE0);
}

inline void AudioPlaySerialFlash::readSerStart(const size_t position)
{

#if 0
	delay(1);
	SPIFIFO.write(0x9F, SPI_CONTINUE);
	SPIFIFO.write(0, SPI_CONTINUE);
	SPIFIFO.write(0, SPI_CONTINUE);
	SPIFIFO.write(0);
	SPIFIFO.read();
	Serial.printf("manufacturerID = %x\r\n",SPIFIFO.read());
	Serial.printf("memoryType = %x\r\n",SPIFIFO.read());
	Serial.printf("capacity = %x\r\n",SPIFIFO.read());
#endif
	//SPI.beginTransaction(spisettings);
	SPIFIFO.write16((0xb00 | ((position>>16) & 0xff)), SPI_CONTINUE);//CMD_READ_HIGH_SPEED
	SPIFIFO.write16( position, SPI_CONTINUE);
	SPIFIFO.write(0,SPI_CONTINUE);
	SPIFIFO.read();
	SPIFIFO.read();
	SPIFIFO.read();
}

void AudioPlaySerialFlash::play(const unsigned int data)
{
	int temp;
	stop();
	AudioStartUsingSPI();
	__disable_irq();
	readSerStart(data);
	SPIFIFO.write16(0x00,SPI_CONTINUE);
	SPIFIFO.write16(0x00);
	prior = 0;
	//sample = 0;
	beginning = data + 4;
	len =__REV16(SPIFIFO.read());
	temp = SPIFIFO.read();
	//SPI.endTransaction();
	len |= (temp & 0xff00) << 8;
	playing = temp & 0x03;
	b16 = (temp & 0x80) >> 7;	
	//b16 = 0;
	__enable_irq();
	//Serial.printf("len:0x%x Mode: 0x%x\r\n", len, playing);
}

void AudioPlaySerialFlash::seek(const unsigned int data, const unsigned int _pos)
{
	int temp, _p, 
	stop();
	AudioStartUsingSPI();
	__disable_irq();
	readSerStart(data);
	SPIFIFO.write16(0x00,SPI_CONTINUE);
	SPIFIFO.write16(0x00);
	prior = 0;
	beginning = data + 4;
	len =__REV16(SPIFIFO.read());
	temp = SPIFIFO.read();
	//SPI.endTransaction();
	len |= (temp & 0xff00) << 8;
	playing = temp & 0x03;
	b16 = (temp & 0x80) >> 7;	
	
	if (playing == 0x01)      _p = 7; 
	else if (playing == 0x02) _p = 6; 
	else sample = _p = 5; 
	sample = _pos<<_p; 
	sample = sample < len ? sample : ((len>>_p)<<_p);
	len -= sample;
	__enable_irq();
	//Serial.printf("len:%d pos: %d\r\n", len, _pos);
}

void AudioPlaySerialFlash::update(void)
{
	audio_block_t *block;
	int16_t *out;
	uint16_t consumed;
	int i;
	int16_t s0, s1, s2, s3, s4;
	uint16_t a;
	uint8_t p;
	
	if (paused) return;
	p = playing;
	if (!p) return;

	block = allocate();
	if (block == NULL) return;

	readSerStart(beginning + (sample<<b16));

	//FIFO 8 Bytes ahead..

	SPIFIFO.write16(0,SPI_CONTINUE);
	SPIFIFO.write16(0,SPI_CONTINUE);
	SPIFIFO.write16(0,SPI_CONTINUE);
	SPIFIFO.write16(0,SPI_CONTINUE);

	out = block->data;
	
if (b16==0) {
	switch (p) {

	  case 0x01: // u-law encoded, 44100 Hz		//3667 cycles 6925
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i+=2) {
			a = SPIFIFO.read();
			*out++ = ulaw_decode_table[a>>8];
			*out++ = ulaw_decode_table[a & 0xff];
			if (i < AUDIO_BLOCK_SAMPLES - 10) {SPIFIFO.write16(0,SPI_CONTINUE);}
			else
			if (i < AUDIO_BLOCK_SAMPLES - 8) {SPIFIFO.write16(0);}
		}
		consumed = 128;
		break;

	  case 0x02: // u-law encoded, 22050 Hz 3730
		s0 = prior;
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i += 8) {
			a = SPIFIFO.read();
			s1 = ulaw_decode_table[a>>8];
			s2 = ulaw_decode_table[a & 0xff];
			*out++ = (s0 + s1) >> 1;
			*out++ = s1;
			*out++ = (s1 + s2) >> 1;
			*out++ = s2;
			a = SPIFIFO.read();
			s3 = ulaw_decode_table[a>>8];
			s4 = ulaw_decode_table[a & 0xff];
			*out++ = (s2 + s3) >> 1;
			*out++ = s3;
			*out++ = (s3 + s4) >> 1;
			*out++ = s4;
			s0 = s4;
			if (i < AUDIO_BLOCK_SAMPLES - 8*2) {SPIFIFO.write16(0,SPI_CONTINUE);}
			if (i < AUDIO_BLOCK_SAMPLES - 8*2-8) {SPIFIFO.write16(0,SPI_CONTINUE);}
			else
			if (i < AUDIO_BLOCK_SAMPLES - 8*2) {SPIFIFO.write16(0);}
		}
		consumed = 64;
		prior = s0;
		break;

		case 0x03: // u-law encoded, 11025 Hz 2674
		s0 = prior;
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i += 16) {
			a = SPIFIFO.read();
			s1 = ulaw_decode_table[a>>8];
			s2 = ulaw_decode_table[a & 0xff];
			*out++ = (s0 * 3 + s1) >> 2;
			*out++ = (s0 + s1)     >> 1;
			*out++ = (s0 + s1 * 3) >> 2;
			*out++ = s1;
			*out++ = (s1 * 3 + s2) >> 2;
			*out++ = (s1 + s2)     >> 1;
			*out++ = (s1 + s2 * 3) >> 2;
			*out++ = s2;
			a = SPIFIFO.read();
			s3 = ulaw_decode_table[a>>8];
			s4 = ulaw_decode_table[a & 0xff];
			if (i < AUDIO_BLOCK_SAMPLES - 16*2) {SPIFIFO.write16(0,SPI_CONTINUE);}
			if (i < AUDIO_BLOCK_SAMPLES - 16*2-16) {SPIFIFO.write16(0,SPI_CONTINUE);}
			else
			if (i < AUDIO_BLOCK_SAMPLES - 16*2) {SPIFIFO.write16(0);}
			*out++ = (s2 * 3 + s3) >> 2;
			*out++ = (s2 + s3)     >> 1;
			*out++ = (s2 + s3 * 3) >> 2;
			*out++ = s3;
			*out++ = (s3 * 3 + s4) >> 2;
			*out++ = (s3 + s4)     >> 1;
			*out++ = (s3 + s4 * 3) >> 2;
			*out++ = s4;
			s0 = s4;
		}
		consumed = 32;
		prior = s0;
		break;
	}

	}
else {
	switch(p) {

		case 0x01: // 16 bit PCM, 44100 Hz	//13310 21608
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
			a = __REV16(SPIFIFO.read());
			*out++ = a;
			if (i < AUDIO_BLOCK_SAMPLES - 5) {SPIFIFO.write16(0,SPI_CONTINUE);}
			else
			if (i < AUDIO_BLOCK_SAMPLES - 4) {SPIFIFO.write16(0);} 
		}
		consumed = 128; //Consumed samples,not Bytes!
		break;

		case 0x02: // 16 bits PCM, 22050 Hz		6930
		s0 = prior;
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i += 4) {
			s1 = __REV16(SPIFIFO.read());
			s2 = __REV16(SPIFIFO.read());
			*out++ = (s0 + s1) >> 1;
			*out++ = s1;
			*out++ = (s1 + s2) >> 1;
			*out++ = s2;
			s0 = s2;
			if (i < AUDIO_BLOCK_SAMPLES - 4*2) {SPIFIFO.write16(0,SPI_CONTINUE);}
			if (i < AUDIO_BLOCK_SAMPLES - 4*2-4) {SPIFIFO.write16(0,SPI_CONTINUE);}
			else
			if (i < AUDIO_BLOCK_SAMPLES - 4*2) {SPIFIFO.write16(0);}
		}
		consumed = 64; //Consumed samples,not Bytes!
		prior = s0;
		break;

		case 0x03: // 16 bit PCM, 11025 Hz	3740
		s0 = prior;
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i += 8) {
			s1 = __REV16(SPIFIFO.read());
			s2 = __REV16(SPIFIFO.read());
			*out++ = (s0 * 3 + s1) >> 2;
			*out++ = (s0 + s1)     >> 1;
			*out++ = (s0 + s1 * 3) >> 2;
			*out++ = s1;
			*out++ = (s1 * 3 + s2) >> 2;
			*out++ = (s1 + s2)     >> 1;
			*out++ = (s1 + s2 * 3) >> 2;
			*out++ = s2;
			s0 = s2;
			if (i < AUDIO_BLOCK_SAMPLES - 8*2) {SPIFIFO.write16(0,SPI_CONTINUE);}
			if (i < AUDIO_BLOCK_SAMPLES - 8*2-8) {SPIFIFO.write16(0,SPI_CONTINUE);}
			else
			if (i < AUDIO_BLOCK_SAMPLES - 8*2) {SPIFIFO.write16(0);}
		}
		consumed = 32; //Consumed samples,not Bytes!
		prior = s0;
		break;

	  default:
		release(block);
		stop();
		return;
	}
	
	}
	//SPI.endTransaction();
	transmit(block);
	release(block);
	//sample += 256>>p;
	sample += consumed;
	if ( (sample) >= len) stop();
	
}

void AudioPlaySerialFlash::stop(void)
{
	__disable_irq();
	paused = false;
	sample = 0;
	if (playing) {
		playing = 0;		
		__enable_irq();		
		AudioStopUsingSPI();
	} else
	__enable_irq();
}


#define B2M_44100 ((uint32_t)((double)4294967296000.0 / AUDIO_SAMPLE_RATE_EXACT))

int AudioPlaySerialFlash::SamplesConsumedPerUpdate(void)
{
	__disable_irq();
	int p = playing;
	__enable_irq();
	return 256 >> p;
}

uint32_t AudioPlaySerialFlash::b2m(void)
{
  __disable_irq();
  uint8_t p= playing;
  __enable_irq();  
  return ((B2M_44100>>1) << p);
}

uint32_t AudioPlaySerialFlash::calcMillis(uint32_t position)
{
	return ((uint64_t)position * b2m()) >> 32;
}

//returns samples since start
uint32_t AudioPlaySerialFlash::position(void)
{
	__disable_irq();
	uint32_t n = sample;
	__enable_irq();	
	return n;
}

//returns millisconds since start
uint32_t AudioPlaySerialFlash::positionMillis(void)
{
	__disable_irq();
	uint32_t n = sample;
	__enable_irq();	
	return calcMillis(n);
}

//returns len of file in samples
uint32_t AudioPlaySerialFlash::length(void)
{
	return len;
}

//returns len of file in milliseconds
uint32_t AudioPlaySerialFlash::lengthMillis(void)
{
	return calcMillis(len);
}

//returns true when playing
bool AudioPlaySerialFlash::isPlaying(void)
{
	return playing > 0;
}

//pauses playing
bool AudioPlaySerialFlash::pause(bool _paused)
{
	__disable_irq();
	bool p = (playing > 0)?_paused:false;
	paused = p;
	__enable_irq();
	return p;
}

//set position in milliseconds in 2.9ms steps.
void AudioPlaySerialFlash::setPositionMillis(const unsigned int millis)
{
	int u = SamplesConsumedPerUpdate();
	uint32_t n = ((uint64_t)millis<<32) / b2m();	
	n = n & ~(u-1);
	if (n >= len - u ) stop();
	else {
		__disable_irq();
		sample = n;
		__enable_irq();
	}
}

//set position in milliseconds in 2.9ms steps.
void AudioPlaySerialFlash::setPositionSamples(const unsigned int _samples)
{
	int u = SamplesConsumedPerUpdate();
	uint32_t n = _samples;	
	n = u < 0x7F ? (n >> 1) : n;
	n = n & ~(u-1);
	if (n >= len - u ) stop();
	else {
		__disable_irq();
		sample = n;
		__enable_irq();
	}
}

unsigned char AudioPlaySerialFlash::flash_status(void)
{
  //SPI.beginTransaction(spisettings);		
  unsigned char c = spififo_flash_read_status();
  //SPI.endTransaction();
  return(c);
}

unsigned char AudioPlaySerialFlash::spififo_flash_read_status(void)
{
  //SPI.beginTransaction(spisettings);	
  unsigned char c;
  SPIFIFO.write(CMD_READ_STATUS_REG, SPI_CONTINUE);
  SPIFIFO.write(0x00);
  SPIFIFO.read();
  c = SPIFIFO.read();
  //SPI.endTransaction();
  return(c);

}


unsigned char flash_wait_for_write = 0;

void AudioPlaySerialFlash::write_pause(void)
{
  if(flash_wait_for_write) {
  	while(spififo_flash_read_status() & STAT_WIP);
  	flash_wait_for_write = 0;
  }
}

//=====================================
// convert a page number to a 24-bit address
int page_to_address(int pn)
{
  return(pn << 8);
}

//=====================================
// convert a 24-bit address to a page number
int address_to_page(int addr)
{
  return(addr >> 8);
}

//=====================================
void AudioPlaySerialFlash::spififo_flash_read_id(unsigned char *idt)
{
  //SPI.beginTransaction(spisettings);	
  write_pause();
  
  SPIFIFO.write(CMD_READ_ID, SPI_CONTINUE);
  SPIFIFO.read();
  for(int i = 0;i < 19;i++) {
       SPIFIFO.write(0x00, SPI_CONTINUE);
       *idt++ = SPIFIFO.read();
  }
  SPIFIFO.write(0x00);
  *idt++ = SPIFIFO.read();
  //SPI.endTransaction();
}

//=====================================
// Tbe Typ=13sec  Max=40sec
void AudioPlaySerialFlash::spififo_flash_chip_erase(boolean wait)
{
  //SPI.beginTransaction(spisettings);	
  write_pause();
  SPIFIFO.write(CMD_WRITE_ENABLE);
  SPIFIFO.read();
  
  SPIFIFO.write(CMD_CHIP_ERASE);
  SPIFIFO.read();
  flash_wait_for_write = 1;
  if(wait)write_pause();
  //SPI.endTransaction();
}

//=====================================
// Tpp Typ=0.64ms Max=5ms
// measured 1667us
void AudioPlaySerialFlash::spififo_flash_page_program(unsigned char *wp,int pn)
{
   int address;
   //SPI.beginTransaction(spisettings);
   write_pause(); 
  
   SPIFIFO.write(CMD_WRITE_ENABLE);
   SPIFIFO.read();

   SPIFIFO.write(CMD_PAGE_PROGRAM, SPI_CONTINUE);
   SPIFIFO.read();
  // Send the 3 byte address
   address = page_to_address(pn);
   SPIFIFO.write((address >> 16) & 0xff, SPI_CONTINUE);
   SPIFIFO.write((address >> 8) & 0xff,  SPI_CONTINUE);
   SPIFIFO.write(address & 0xff, SPI_CONTINUE);
   SPIFIFO.read();
   SPIFIFO.read();
   SPIFIFO.read();
   // Now write 256 bytes to the page
   for(int i = 0;i < 255;i++) {
      SPIFIFO.write(*wp++, SPI_CONTINUE);
      SPIFIFO.read();
   }
   SPIFIFO.write(*wp++);
   SPIFIFO.read();
   //SPI.endTransaction();
   // Indicate that next I/O must wait for this write to finish
   flash_wait_for_write = 1;
}

//=====================================
// measured = 664us
void AudioPlaySerialFlash::spififo_flash_read_pages(unsigned char *p, int pn, const int n_pages)
{ 
  int address;
  unsigned char *rp = p;
  //SPI.beginTransaction(spisettings);
  write_pause();
  SPIFIFO.write(CMD_READ_DATA, SPI_CONTINUE);
  SPIFIFO.read();
  // Send the 3 byte address
  address = page_to_address(pn);
  SPIFIFO.write((address >> 16) & 0xff, SPI_CONTINUE);
  SPIFIFO.write((address >> 8)  & 0xff, SPI_CONTINUE);
  SPIFIFO.write(address & 0xff, SPI_CONTINUE);
  SPIFIFO.read();
  SPIFIFO.read();
  SPIFIFO.read();

  //Now read the page's data bytes
  for(int i = 0;i < n_pages * 256;i++) {
    SPIFIFO.write(0x00, SPI_CONTINUE);
    *rp++ = SPIFIFO.read();
  }
   SPIFIFO.write(0x00); 
   SPIFIFO.read();
   //SPI.endTransaction();
}



