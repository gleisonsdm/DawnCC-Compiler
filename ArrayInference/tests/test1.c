typedef struct var{
  long int a;
  long int b;
} var;

var n[10000];

void func(int a){
  int b = 100;
  for(int i = 0; i <= b; i++){
  	  n[i+2].a = n[i+2].b + 1;
  }
}
