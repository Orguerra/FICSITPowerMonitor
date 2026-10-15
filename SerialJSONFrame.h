#pragma once
#include <stddef.h>

// A frame ends at its matching bracket, even without a newline. Brackets in
// strings and escaped quotes do not affect nesting. Storage belongs to caller.
struct SerialJSONFrame {
  size_t size=0;
  int depth=0;
  bool quoted=false, escaped=false, overflow=false;
  void reset() {size=0;depth=0;quoted=false;escaped=false;overflow=false;}
  bool push(char c, char* buffer, size_t capacity) {
    if(depth==0) {
      if(c!='[' && c!='{') return false;
      reset();
    }
    if(size+1<capacity) buffer[size++]=c; else overflow=true;
    if(quoted) {
      if(escaped) escaped=false;
      else if(c=='\\') escaped=true;
      else if(c=='"') quoted=false;
    } else if(c=='"') quoted=true;
    else if(c=='[' || c=='{') ++depth;
    else if(c==']' || c=='}') --depth;
    buffer[size]=0;
    return depth==0;
  }
};
