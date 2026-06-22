#include <stdio.h>
#include "pico/stdlib.h"

#define R1 2
#define C1 2
#define R2 2
#define C2 3
//Possibly set the breakpoints for the entire matrix where is can be any size we want
int mat1[R1][C1] = {
    {1, 1},
    {2, 2}
};

int mat2[R2][C2] = {
    {1, 1, 1},
    {2, 2, 2}
};

int result_matrix[R1][C2];

int main(void) {
    stdio_init_all();
    sleep_ms(10000);
//possible breakpoint for array calcultation
    printf("Starting Matrix Multiplication Benchmark...\n");

    for (int i = 0; i < R1; i++) {
        for (int j = 0; j < C2; j++) {
            result_matrix[i][j] = 0;

            for (int k = 0; k < R2; k++) {
                result_matrix[i][j] += mat1[i][k] * mat2[k][j];
            }
        }
    }

    printf("Matrix Multiplication Result:\n");

    for (int i = 0; i < R1; i++) {
        for (int j = 0; j < C2; j++) {
            printf("%d ", result_matrix[i][j]);
        }
        printf("\n");
    }

    printf("Matrix Multiplication Benchmark Complete.\n");

    while (true) {
        sleep_ms(1000);
    }
}

