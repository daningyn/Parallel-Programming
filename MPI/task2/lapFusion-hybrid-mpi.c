#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <stdbool.h>
#include <omp.h>

float stencil ( float v1, float v2, float v3, float v4)
{
  return (v1 + v2 + v3 + v4) * 0.25f;
}

float max_error ( float prev_error, float old, float new )
{
  float t= fabsf( new - old );
  return t>prev_error? t: prev_error;
}

float laplace_step(float *in, float *out, int n, int nprocs, int me, bool isInner)
{
  // If isInner == true, do loops inside matrix
  // If isInner == false, do loops for border of matrix
  int i, j;
  float error=0.0f;

  int block_size  = (int)(n/nprocs);  
  int source = isInner == true ? 1 : 0;
  int destination = isInner == true ? block_size - 1 : block_size;
  int step = isInner == true ? 1 : block_size - 1;

  // Check if me == fisrt matrix, source = 1 | if isInner, source should be 2
  // me == last matrix, destination = block_size - 1 | if isInner, destination should be block_size - 2
  // other cases, i should be from 0 to the end | if isInner, i should be from 1 to the end - 1

  // step is to decide the loops do inner or outter
  // if inner => should be step 1, outer => should do only 2 loops those are 0 and block_size - 1
  if (me == 0) {
    source++;
    if (isInner == false) step--;
  } else if (me == nprocs - 1) {
    destination--;
    if (isInner == false) step--;
  }

  int ri = block_size*n; // above row of sub matrix
  int rf = (block_size+1)*n; // below row of sub matrix

  #pragma omp for nowait
  for ( i=source; i < destination; i+=step )
    for ( j=1; j < n-1; j++ )
    {
      float aboveE = i == 0 ? in[ri + j] : in[(i-1)*n + j]; // it is above element of this point
      float belowE = i == block_size - 1 ? in[rf + j] : in[(i+1)*n + j]; // it is below element of this point
      out[i*n+j]= stencil(in[i*n+j+1], in[i*n+j-1], aboveE, belowE);
      error = max_error( error, out[i*n+j], in[i*n+j] );
    }
  return error;
}

void laplace_init(float *in, int n, int nprocs, int me)
{
  int i;
  const float pi  = 2.0f * asinf(1.0f);
  int block_size  = (int)(n/nprocs);
  memset(in, 0, (block_size+2)*n*sizeof(float));
  
  // should calculate index_in_big_matrix because i inside sinf's equation is depended on index of big matrix
  for (i=0; i<block_size; i++) {
    int index_in_big_matrix = i + me*block_size;
    float V = in[i*n] = sinf(pi*index_in_big_matrix / (n-1));
    in[ i*n+n-1 ] = V*expf(-pi);
  }
}

int main(int argc, char** argv)
{ 
  
  double t_before, t_after;
  int n = 4096;
  int iter_max = 1000;
  int threads = 8;
  
  const float tol = 1.0e-5f;

  float *A, *temp;

  MPI_Request firstRowRequest, lastRowRequest;
  MPI_Request firstRowSendRequest, lastRowSendRequest;
  MPI_Status firstRowStatus, lastRowStatus;

  int err, me, nprocs;
  
  // get runtime arguments 
  if (argc>1) {  n        = atoi(argv[1]); }
  if (argc>2) {  iter_max = atoi(argv[2]); }
  if (argc>3) {  threads  = atoi(argv[3]); }

  MPI_Init( &argc, &argv );

  t_before = MPI_Wtime();

  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &me);

  int block_size                = (int)(n/nprocs);
  
  // we assumed that the row at block_size position will be data receiving from last row of the previous matrix
  // the row at block_size + 1 position will be data receiving from first row of the next matrix
  A    = (float*) malloc( (block_size+2)*n*sizeof(float) );
  temp = (float*) malloc( (block_size+2)*n*sizeof(float) ); 

  //  set boundary conditions
  laplace_init (A, n, nprocs, me);
  laplace_init (temp, n, nprocs, me);
  //  A[(n/128)*n+n/128] = 1.0f; // set singular point

  printf("Processor %d is running Jacobi relaxation Calculation: %d x %d mesh,"
         " maximum of %d iterations\n", 
        me, block_size, n, iter_max );
         
  int iter = 0;
  float my_error = 1.0f;
  #pragma omp parallel firstprivate(iter, A, temp) num_threads(threads)
  {
    while ( my_error > tol*tol && iter < iter_max )
    {
      iter++;
      #pragma omp master
      {
        if (me > 0) {
          // send first row and receive last row from the previous processor
          MPI_Isend(A, n, MPI_FLOAT, me - 1, 0, MPI_COMM_WORLD, &firstRowSendRequest);
          MPI_Irecv(A + block_size*n, n, MPI_FLOAT, me - 1, 0, MPI_COMM_WORLD, &firstRowRequest);
        }
        if (me < nprocs - 1) {
          // send last row and receive first row from the next processor
          MPI_Irecv(A + (block_size+1)*n, n, MPI_FLOAT, me + 1, 0, MPI_COMM_WORLD, &lastRowRequest);
          MPI_Isend(A + (block_size-1)*n, n, MPI_FLOAT, me + 1, 0, MPI_COMM_WORLD, &lastRowSendRequest);
        }
      }

      // calculate inner values in waiting time
      my_error= laplace_step (A, temp, n, nprocs, me, true);

      #pragma omp master
      {
        if (me != 0) {
          MPI_Wait(&firstRowRequest, &firstRowStatus);      
        }
        if (me != nprocs - 1) {
          MPI_Wait(&lastRowRequest, &lastRowStatus);
        }      
      }

      // have to calculate my_error by finding max_error because we already have my_error from calculating inner above
      my_error= max_error(my_error, laplace_step (A, temp, n, nprocs, me, false), 0);
      float *swap= A; A=temp; temp= swap; // swap pointers A & temp
      #pragma omp master
      {
        MPI_Barrier(MPI_COMM_WORLD);
      }
    }

    // After finishing calculation, choose processor 0 to calculate the bigest error among them by sending the others to processor 0
    #pragma omp master
    {
      if (me > 0) {
        MPI_Send(&my_error, 1, MPI_FLOAT, 0, 0, MPI_COMM_WORLD);
      } else if (me == 0) {
        float receiver;
        for (int iMe=1; iMe<nprocs; iMe++) {
          MPI_Irecv(&receiver, 1, MPI_FLOAT, iMe, 0, MPI_COMM_WORLD, &lastRowRequest);
          MPI_Wait(&lastRowRequest, &lastRowStatus);
          my_error = max_error(my_error, 0, receiver);
        }
      }
    }
  }
  free(A); free(temp);

  MPI_Barrier(MPI_COMM_WORLD);

  err=MPI_Finalize();
  //check finalization

  if (me == 0) {
    my_error = sqrtf( my_error );
    printf("Total Iterations: %d, ERROR: %0.6f\n", iter, my_error);
    t_after = MPI_Wtime();
    printf("Total seconds the program took: %f\n", t_after - t_before);
  }
  
  return 0;
}
