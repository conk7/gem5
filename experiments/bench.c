#include <stdio.h>

void print_hello() { printf("Hello\n"); }
void print_world() { printf("World\n"); }

int main(int argc, char *argv[]) {
    void (*func_ptr)(void) =  print_hello;
    for (int i = 0; i < 100; i++) {
        func_ptr();
        if (i % 2 == 0) func_ptr = print_world; 
        else func_ptr = print_hello;
    }
    return 0;
}