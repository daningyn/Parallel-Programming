#include <stdlib.h>

void loop_0(float * restrict x, float *y, int N, float a1, float a2)
{
  #pragma acc kernels present(x,y)
  for (int i = 0; i < N; i++) 
  {
    x[i] += a1;
    y[i] += a2;
  }
}

void loop_1(float *x, float *y, int N, float a1, float a2)
{
  #pragma acc kernels present(x)
  for (int i = 0; i < N; i++) 
    x[i] += i*a1;

  #pragma acc kernels present(y)
  for (int i = 0; i < N; i++) 
    y[i] += i*a2;
}

void loop_2(float * restrict x, float * y, int N, float a)
{
  #pragma acc kernels present(x,y)
  for (int i = 0; i < N; i++) 
    y[i] = a * x[i] + y[i];
}	

void loop_3(float *x, int N, float a)
{
  #pragma acc kernels present(x)
  for (int i = 0; i < N; i++) 
    x[i] = a * x[i] ;
} 

void loop_4(float * restrict x, float * y, int N, float a)
{
  #pragma acc kernels present(x,y)
  {
    for (int i = 0; i < N-1; i++) 
      y[i] = a * x[i+1] ;
  }  
  y[N-1] = x[N-1];
} 

void loop_5(float *x, int N, float a)
{
  #pragma acc kernels present(x)
  for (int i = 1; i < N; i++) 
    x[i] = a * x[i-1] ;
} 

void loop_6(float *x, int N, float a)
{
  #pragma acc kernels present(x)
  for (int i = 0; i < N; i++) 
    if (x[i] > a) 
     x[i] = 0;
} 

float loop_7(float *x, int N)
{
  float S=0.0;
  #pragma acc kernels present(x)
  for (int i = 0; i < N; i++) 
    S += x[i];
  return S;
} 

void initVec ( float *x, int N, int seed ) 
{
  srand48 (seed);  // set initial seed for random number generation
  for (int i=0; i<N; i++)
    x[i]=drand48();
}

int main(int argc, char **argv) 
{
  int N= 1000000;
  float a1=3.0, a2=5.2, a3= 4.0, a4= -3.7, a5= 0.95, a6= 1.12, a7= -0.91, a8= 0.98, a9= 0.8;

  float S;
  float * V = (float*)malloc(N * sizeof(float));
  float * W = (float*)malloc(N * sizeof(float));
  float * temp = (float*)malloc(N * sizeof(float));

  initVec (V, N, 0);

  #pragma acc data copyin(V[:N],W[:N],temp[:N])
  {
    loop_0(V, W, N, a1, a2);
    loop_1(V, W, N, a3, a4);
    loop_2(V, W, N, a5);
    loop_3(W, N, a6);
    loop_4(V, temp, N, a7);
    V = temp;
    loop_5(W, N, a8);
    loop_6(V, N, a9);
    S = loop_7(V, N) + loop_7(W, N);
  }

  printf("Checksum= %12.7f\n", S);
  return 0;
}
