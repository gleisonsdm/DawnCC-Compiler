#include <stdlib.h>
#include <stdio.h>

void func(int a, int b, int *n){
  for (int i = 0; i < b; i++) {
  	  n[i] = n[i] + 1;
  }
}

int main(){
  int pont[43],i;
  for(i = 0; i <= 42; i++)
    pont[i] = 0;
  for(i = 0; i < 42; i++)
    pont[i] += 100;
  return 0;
}

