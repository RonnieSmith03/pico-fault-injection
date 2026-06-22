#include <stdio.h>
#include "pico/stdlib.h"

#define ARRAY_SIZE 9

void swap_int(int *a, int *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

int partition(int arr[], int low, int high) {
    int pivot = arr[high];
    int i = low - 1;

    for (int j = low; j <= high - 1; j++) {
        if (arr[j] <= pivot) {
            i++;
            swap_int(&arr[i], &arr[j]);
        }
    }

    swap_int(&arr[i + 1], &arr[high]);
    return i + 1;
}

void quickSort(int arr[], int low, int high) {
    if (low < high) {
        int pi = partition(arr, low, high);

        quickSort(arr, low, pi - 1);
        quickSort(arr, pi + 1, high);
    }
}

void print_array(int arr[], int size) {
    for (int i = 0; i < size; i++) {
        printf("%d ", arr[i]);
    }

    printf("\n");
}

int main(void) {
    stdio_init_all();

    while (true) {
        sleep_ms(3000);

        int data[ARRAY_SIZE] = {
            10, 7, 8, 4, 9, 1, 5, 3, 12
        };

        printf("\nStarting QuickSort Benchmark...\n");

        printf("Original array: ");
        print_array(data, ARRAY_SIZE);

        quickSort(data, 0, ARRAY_SIZE - 1);

        printf("Sorted array: ");
        print_array(data, ARRAY_SIZE);

        printf("QuickSort Benchmark Complete.\n");
    }

    return 0;
}
