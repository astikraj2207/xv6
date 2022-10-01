#include "types.h"
#include "defs.h"

const char art[] = "___________________________$$\n\
_________________________$$$$\n\
_______________________$$$$$$\n\
______________________$$$$$$\n\
______________________$$$$\n\
______________________$$\n\
_________$$$$$$$$$$$$$_$$$$$$$$$$$$$\n\
______$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\
____$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\
___$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\
__$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\
_$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\
_$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\
_$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\
_$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\
__$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\
___$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\
____$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\
_____$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\
______$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\
________$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\
__________$$$$$$$$$$$$$$$$$$$$$$$$$\n\
____________$$$$$$$$$$$$$$$$$$$$$\n\
______________$$$$$$$$__$$$$$$$\n";


int sys_draw(void){
    int buffer_size;
    char *buffer;
    
    int sz = sizeof(art);

    // Checking whether the buffer is valid
    int invalidBuffer = argint(1, &buffer_size) < 0;
    
    // Checking whether the buffer size is sufficient to copy image
    int insufficientSize = buffer_size < sz;
    
    // Checking whether the pointer provided is correct 
    int bufError = argptr(0, &buffer, buffer_size) < 0;

    // If any error, return -1
    if(invalidBuffer || insufficientSize || bufError){
        return -1;
    }

    // copying ASCII ART
    memmove(buffer, art, (uint)sz);
    
    // returning size
    return sz;
}