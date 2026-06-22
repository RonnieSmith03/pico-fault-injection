#include <stdio.h>
#include "pico/stdlib.h"

double f(double x, double y) {
    return x + y;
}

int main(void) {
    stdio_init_all();
    sleep_ms(10000);
//possible breakpoint for future reference
    double x = 0.0;
    double y = 1.0;
    double h = 0.025;
    double targetX = 0.1;

    printf("Starting Euler Benchmark...\n");
    printf("x\t\ty\n");
//Same for the while loop
    while (x < targetX) {
        y = y + h * f(x, y);
        x = x + h;
        printf("%.6f\t%.6f\n", x, y);
    }

    printf("Euler Benchmark Complete.\n");

    while (true) {
        sleep_ms(1000);
    }
}

