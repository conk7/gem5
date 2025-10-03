#include <stdio.h>

void print_hello() { printf("Hello\n"); }
void print_world() { printf("World\n"); }
void print_another() { printf("Another\n"); }
void print_extra() { printf("Extra\n"); }

int main(int argc, char *argv[]) {
    void (*func_ptr)(void) = (argc > 1) ? print_hello : print_world;
    for (int i = 0; i < 1000; i++) { 
        func_ptr();
        switch (i % 4) {
            case 0: func_ptr = print_world; break;
            case 1: func_ptr = print_hello; break;
            case 2: func_ptr = print_another; break;
            case 3: func_ptr = print_extra; break;
        }
    }
    return 0;
}