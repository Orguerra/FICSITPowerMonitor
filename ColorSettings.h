#pragma once
#include <stdint.h>
#include <string.h>
inline bool parseRGBColor(const char* text,uint32_t& color){
  if(!text || strlen(text)!=7 || text[0]!='#')return false;
  uint32_t value=0;
  for(int i=1;i<7;i++){
    char c=text[i];int digit;
    if(c>='0' && c<='9')digit=c-'0';
    else if(c>='a' && c<='f')digit=c-'a'+10;
    else if(c>='A' && c<='F')digit=c-'A'+10;
    else return false;
    value=(value<<4)|(uint32_t)digit;
  }
  color=value;return true;
}
