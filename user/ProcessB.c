#include <Syscall.h>
#include <Printf.h>

void userMain() {
    for (int i = 1; i <= 10; ++ i) {
        printf("This is process B\n");
        //syscallPutchar('b');
    }   
    printf("process finish\n");
}