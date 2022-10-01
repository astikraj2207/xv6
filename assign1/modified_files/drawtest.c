#include "types.h"
#include "user.h"

char myBuffer[1024];


int main(void){
    int n = draw(myBuffer, 1024);
    if(n<0){
        printf(1,"Image Error in drawtest.c\n");
    }else if(write(1, myBuffer, n) != n){
        printf(1,"Write Error in drawtest.c\n");
    }
    exit();
}
