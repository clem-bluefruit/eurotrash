/*
*       
* some things to deal with the wav files. mainly opening + swapping the audio objects
*
*/

void init_channels(uint8_t f) {
  
  uint8_t _file = f;
  for (int i = 0; i < CHANNELS; i ++) {
        
        audioChannels[i]->id = i;
        audioChannels[i]->file_wav = _file;
        audioChannels[i]->pos0 = 0;
        audioChannels[i]->pos1 = CTRL_RESOLUTION;
        audioChannels[i]->ctrl_res = CTRL_RES[_file];
        audioChannels[i]->ctrl_res_eof = CTRL_RES_EOF[_file];
        audioChannels[i]->eof = CTRL_RESOLUTION * CTRL_RES_EOF[_file];
        audioChannels[i]->_gain = DEFAULT_GAIN;  
        audioChannels[i]->swap = false;
        audioChannels[i]->bank = true;
  } 
}  

/* ====================check clocks================ */

void leftright() {
  
 if (LCLK) {  // clock?
  
       _play(audioChannels[LEFT]);
       LCLK = false;
       FADE_LEFT = false;
       last_LCLK = millis();
  } 
  if (RCLK) { // clock?
 
       _play(audioChannels[RIGHT]);
       RCLK = false;
       FADE_RIGHT = false;
       last_RCLK = millis();
 
   } 
}

/* =============================================== */

void _play(struct audioChannel* _channel) {
  
      uint8_t _swap, _bank, _select, _id;
      uint16_t _file;
      int32_t _startPos;
      
      _swap   = _channel->swap;   
      _bank   = _channel->bank;     
      _id     = _channel->id; 
      _file   = _channel->file_wav; 
      
      _select   = _id*CHANNELS + _swap;               // select audio object # 1,2 (LEFT) or 3,4 (RIGHT)  
      _startPos = (HALFSCALE - CV[0x3-_id]) >> 0x5;   // CV
      _startPos += _channel->pos0;                    // add manual offset
      
      _startPos = _startPos < 0 ?  0 : _startPos;      
      _startPos = _startPos < CTRL_RESOLUTION ? _startPos : CTRL_RESOLUTION-1;
      _startPos *= _channel->ctrl_res; 
       
      _bank ? fade[_select+0x4]->fadeIn(1) : fade[_select]->fadeIn(FADE_IN);
      
       if (_bank) {
            const unsigned int f_adr = RAW_FILE_ADR[_file];    
            raw[_select]->play(f_adr);    
       }
       else { 
             String playthis = FILES[_file];  
             wav[_select]->seek(&playthis[0], _startPos>>9); 
       }     
       
       /*  swap the file and fade out previous file: */
        _swap = ~_swap & 1u;
        _select = (_id*CHANNELS) + _swap;
        fade[_select]->fadeOut(FADE_OUT);
        _channel->swap = _swap;
       /*  update channel data: */
        _channel->ctrl_res = CTRL_RES[_file + _bank*MAXFILES];
        _channel->ctrl_res_eof = CTRL_RES_EOF[_file + _bank*MAXFILES]; 
}
 
/* =============================================== */

void eof_left() {
  
    
    //uint32_t _pos  = _bank ? raw[_swap]->positionBytes() : wav[_swap]->positionBytes();
  
   if (millis() - last_LCLK > audioChannels[LEFT]->eof) {
  
        FADE_LEFT = true;
        uint8_t  _bank = audioChannels[LEFT]->bank;
        uint8_t  _swap = ~audioChannels[LEFT]->swap & 1u;
      
        digitalWriteFast(EOF_L, HIGH); 
        
        _bank ? fade[_swap+0x4]->fadeOut(FADE_OUT) : fade[_swap]->fadeOut(FADE_OUT); 
        last_EOF_L = millis();
        EOF_L_OFF = FADE_LEFT = true;
     }  
}

void eof_right() {
  
    // uint32_t _pos  = _bank ? raw[_swap]->positionBytes() : wav[_swap]->positionBytes();
  
    if (millis() - last_RCLK > audioChannels[RIGHT]->eof) {
       
        FADE_RIGHT = true;
        uint8_t  _bank = audioChannels[RIGHT]->bank;
        uint8_t  _swap = (~audioChannels[RIGHT]->swap & 1u) + CHANNELS; 
          
        digitalWriteFast(EOF_R, HIGH);  
            
        _bank ? fade[_swap+0x4]->fadeOut(FADE_OUT) : fade[_swap]->fadeOut(FADE_OUT); 
           
        last_EOF_R = millis();
        EOF_R_OFF = FADE_RIGHT = true;
     } 
}


void generate_file_list() {  // to do - sort alphabetically?
  
  uint8_t len;
  uint32_t file_len, file_len_ms;
  char tmp[DISPLAY_LEN];
  File thisfile;
  root = SD.open("/");
  
  thisfile = root.openNextFile(O_RDONLY);  
  while (thisfile && FILECOUNT < MAXFILES) {
              char* _name = thisfile.name(); 
              // wav files ?  
              len = strlen(_name) - 4; 
              if  (!strcmp(&_name[len-2], "~1.WAV")) delay(2); // skip crap
              else if  (_name[0] == '_') delay(2);             // skip crap
              else if (!strcmp(&_name[len], ".WAV")) {
                      
                      FILES[FILECOUNT] = _name;
                      //file_len  = thisfile.size() - 0x2e; // size minus header [ish]
                      /* this is annoying */
                      wav1.play(_name);
                      delay(15);
                      file_len = (float)wav1.lengthBytes() * 0.9f;
             
                      CTRL_RES[FILECOUNT]  = file_len / CTRL_RESOLUTION;       // ctrl resolution pos0/bytes
                      file_len_ms = wav1.lengthMillis();
                      CTRL_RES_EOF[FILECOUNT] = file_len_ms / CTRL_RESOLUTION; // ctrl resolution pos1/bytes
                      wav1.stop();
                      /* for the display, get rid of .wav extension + right justify */
                      int8_t justify = DISPLAY_LEN - len;
                      if (justify < 0) justify = 0; 
                      for (int i = justify; i < DISPLAY_LEN; i++) {  
                          tmp[i] = _name[i-justify];
                          if (tmp[i] >= 'A' && tmp[i]  <= 'Z' ) tmp[i] = tmp[i] + 'a' - 'A';
                      } 
                      while (justify) {
                          justify--;
                          tmp[justify] = ' '; 
                      }
                      DISPLAYFILES[FILECOUNT] = tmp;
                      FILECOUNT++;
              }    
             thisfile.close();
             thisfile = root.openNextFile(O_RDONLY);
   }   
  root.rewindDirectory(); 
  root.close();
}
  
/* =============================================== */

void update_eof(uint8_t _channel) {
  
        /* update EOF */
   if (_channel < CHANNELS) { 
       int16_t _CV, tmp, tmp2; 
       _CV = (HALFSCALE - CV[_channel])>>5;                    // CV
       tmp  = audioChannels[_channel]->pos1 + _CV;             // length
       tmp2 = CTRL_RESOLUTION - audioChannels[_channel]->pos0; // max length
       if (tmp > tmp2) tmp = tmp2;
       else if (tmp <= 1) tmp = 1;
       audioChannels[_channel]->eof = tmp * audioChannels[_channel]->ctrl_res_eof;
     
    }
}  
/* =============================================== */

void calibrate() {
  
  /*  calibrate mid point */
      float average = 0.0f;
      uint8_t save = false;
      HALFSCALE = 0;
      MENU_PAGE[LEFT]  = CALIBRATE;
      MENU_PAGE[RIGHT] = CALIBRATE;
      update_display(LEFT, HALFSCALE);
      delay(1000);
      for (int i = 0; i < 200; i++) {
   
           average +=  analogRead(CV1);
           average +=  analogRead(CV2);
           average +=  analogRead(CV3);
           average +=  analogRead(CV4);
           delay(2);
      }
      
      HALFSCALE = average / 800.0f;
      update_display(LEFT,  HALFSCALE);
      delay(500);
      update_display(RIGHT, HALFSCALE);
      // do we want to save the value?
      while(digitalRead(BUTTON_L)) {
        
           if (!digitalRead(BUTTON_R) && !save) { 
                 save = true; 
                 writeMIDpoint(HALFSCALE);
                 update_display(RIGHT, 0x0);
            }
        
      }
      delay(1000);
      MENU_PAGE[LEFT]  = FILESELECT; 
      MENU_PAGE[RIGHT] = FILESELECT; 
      LASTBUTTON = millis(); 
} 


/* some stuff to save the ADC calibration value */

void writeMIDpoint(uint16_t _val) {
   
  uint8_t byte0, byte1, adr = 0;
       
       byte0 = _val >> 8;
       byte1 = _val;
       EEPROM.write(adr, 0xFF);
       adr++;
       EEPROM.write(adr, byte0);
       adr++;
       EEPROM.write(adr, byte1);
}  

uint16_t readMIDpoint() {
  
       uint8_t byte0, byte1, adr = 0x1;
       byte0 = EEPROM.read(adr);
       adr++;
       byte1 = EEPROM.read(adr);
       
       return  (uint16_t)(byte0 << 8) + byte1;
}  
