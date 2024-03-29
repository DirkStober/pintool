/*
    Distributed Under the MIT license
    Bellman-Ford/Dijkstra Algorithm to find shortest path distances
    Programs by Masab Ahmad (UConn)
*/

#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include "read_graph.h"
//#include "carbon_user.h"    /*For the Graphite Simulator*/
#include <time.h>
#include <sys/timeb.h>
#include "../CRONO/common/barrier.h"

#define MAX            100000000
#define INT_MAX        100000000
#define BILLION 1E9

//Thread Argument Structure
typedef struct
{
   int*      local_min;
   int*      global_min;
   int*      Q;
   int*      D;
   int*      D_temp;
   int**     W;
   int**     W_index;
   int*      d_count;
   int       tid;
   int       P;
   int       N;
   int       DEG;
   pthread_barrier_t* barrier;
} thread_arg_t;

//Function Initializers
int initialize_single_source(int* D, int* D_temp, int* Q, int source, int N);
void relax(int u, int i, volatile int* D, int** W, int** W_index, int N);
int get_local_min(volatile int* Q, volatile int* D, int start, int stop, int N, int** W_index, int** W, int u);
void init_weights(int N, int DEG, int** W, int** W_index);

//Global Variables
int min = INT_MAX;
int min_index = 0;
pthread_mutex_t lock;
pthread_mutex_t locks[7000000]; //change the number of locks to approx or greater N
int u = -1;
int local_min_buffer[1024];
int global_min_buffer;
int terminate = 0;
int cntr = 0;
int *exist;
int *id;
int P_max=64;
double largest_d;
thread_arg_t thread_arg[1024];
pthread_t   thread_handle[1024];

//Primary Parallel Function
void* do_work(void* args)
{
   volatile thread_arg_t* arg = (thread_arg_t*) args;

   int tid                  = arg->tid;      //thread id
   int P                    = arg->P;        //Max threads
   int* D_temp              = arg->D_temp;   //Temporary Distance Array
   int* D                   = arg->D;        //distabces
   int** W                  = arg->W;        //edge weights
   int** W_index            = arg->W_index;  //graph structure
   const int N              = arg->N;        //Max vertices
   const int DEG            = arg->DEG;      //edges per vertex
   int v = 0;

   int cntr_0 = 0;
   int start = 0;
   int stop = 1;
   int neighbor=0;

   //For precision dynamic work allocation
   double P_d = P;
   //double range_d = 1.0;
   double tid_d = tid;

   double start_d = (tid_d) * (largest_d/P_d);
   double stop_d  = (tid_d+1.0) * (largest_d/P_d);
   start =  start_d;//tid    *  (largest+1) / (P);
   stop =  stop_d;//(tid+1) *  (largest+1) / (P); 
   

   barrier_wait();

   while(terminate==0)
   {
         //printf("\n Start:%d",tid);
         for(v=start;v<stop;v++)
         {
           D[v] = D_temp[v];
         }

         barrier_wait();

         if(tid==0)
           terminate = 1;

         for(v=start;v<stop;v++)
         {

            if(exist[v]==0)
               continue;

            for(int i = 0; i < DEG; i++)
            {
               //if(v<N)
                  neighbor = W_index[v][i];

               if(neighbor>=N)
                  break;


               //relax
               if((D[W_index[v][i]] > (D[v] + W[v][i])))    //relax, update distance
                  D_temp[W_index[v][i]] = D[v] + W[v][i];
            }
         }
         
         barrier_wait();

         for(v=start;v<stop;v++)
         {
           if(D[v] != D_temp[v]) 
           {
             terminate = 0;
           }
         }

         barrier_wait();
         cntr_0++; 
   }

   if(tid==0)
     cntr = cntr_0;

   barrier_wait();

   return NULL;
}

// Create a dotty graph named 'fn'.
void make_dot_graph(int **W,int **W_index,int *exist,int *D,int N,int DEG,const char *fn)
{
   FILE *of = fopen(fn,"w");
   if (!of) {
      printf ("Unable to open output file %s.\n",fn);
      exit (EXIT_FAILURE);
   }

   fprintf (of,"digraph D {\n"
         "  rankdir=LR\n"
         "  size=\"4,3\"\n"
         "  ratio=\"fill\"\n"
         "  edge[style=\"bold\"]\n"
         "  node[shape=\"circle\",style=\"filled\"]\n");

   // Write out all edges.
   for (int i = 0; i != N; ++i) {
      if (exist[i]) {
         for (int j = 0; j != DEG; ++j) {
            if (W_index[i][j] != INT_MAX) {
               fprintf (of,"%d -> %d [label=\"%d\"]\n",i,W_index[i][j],W[i][j]);
            }
         }
      }
   }

# ifdef DISTANCE_LABELS
   // We label the vertices with a distance, if there is one.
   fprintf (of,"0 [fillcolor=\"red\"]\n");
   for (int i = 0; i != N; ++i) {
      if (D[i] != INT_MAX) {
         fprintf (of,"%d [label=\"%d (%d)\"]\n",i,i,D[i]);
      }
   }
# endif

   fprintf (of,"}\n");

   fclose (of);
}

int Usage(){
	printf("No input: ./sssp 0 <Max Input Threads> <N> <DEG>\n");
	printf("Input file: ./sssp 1 <Max Input Threads> <FileName>\n");
	return 0;	
};

int main(int argc, char** argv)
{
   if (argc < 4) {
	   Usage();
      return 1;
   }
   int N = 0;
   int DEG = 0;
   FILE *file0 = NULL;

   const int select = atoi(argv[1]);
   const int P = atoi(argv[2]);
   if(select==0)
   {
      N = atoi(argv[3]);
      DEG = atoi(argv[4]);
      printf("\nGraph with Parameters: N:%d DEG:%d\n",N,DEG);
   }

   if (!P) {
      printf ("Error:  Thread count must be a valid integer greater than 0.");
      return 1;
   }

   if(select==1)
   {
      const char *filename = argv[3];
      file0 = fopen(filename,"r");
      if (!file0) {
         printf ("Error:  Unable to open input file '%s'\n",filename);
         return 1;
      }
      N = read_N(file0);
      DEG = 20;     //also can be read from file if needed, upper limit here again
   }


   if (DEG > N)
   {
      fprintf(stderr, "Degree of graph cannot be grater than number of Vertices\n");
      exit(EXIT_FAILURE);
   }

   int* D;
   int* D_temp;
   int* Q;

   if (posix_memalign((void**) &D, 64, N * sizeof(int))) 
   {
      fprintf(stderr, "Allocation of memory failed\n");
      exit(EXIT_FAILURE);
   }
   if(posix_memalign((void**) &D_temp, 64, N * sizeof(int)))
   {
     fprintf(stderr, "Allocation of memory failed\n");
     exit(EXIT_FAILURE);
   }
   if( posix_memalign((void**) &Q, 64, N * sizeof(int)))
   {
      fprintf(stderr, "Allocation of memory failed\n");
      exit(EXIT_FAILURE);
   }
   if( posix_memalign((void**) &exist, 64, N * sizeof(int)))
   {
      fprintf(stderr, "Allocation of memory failed\n");
      exit(EXIT_FAILURE);
   }
   if(posix_memalign((void**) &id, 64, N * sizeof(int)))
   {
      fprintf(stderr, "Allocation of memory failed\n");
      exit(EXIT_FAILURE);
   }
   int d_count = N;
   pthread_barrier_t barrier;

   int** W = (int**) malloc(N*sizeof(int*));
   int** W_index = (int**) malloc(N*sizeof(int*));
   for(int i = 0; i < N; i++)
   {
      int ret = posix_memalign((void**) &W[i], 64, DEG*sizeof(int));
      int re1 = posix_memalign((void**) &W_index[i], 64, DEG*sizeof(int));
      if (ret != 0 || re1!=0)
      {
         fprintf(stderr, "Could not allocate memory\n");
         exit(EXIT_FAILURE);
      }
   }

   for(int i=0;i<N;i++)
   {
      for(int j=0;j<DEG;j++)
      {
         W[i][j] = INT_MAX;
         W_index[i][j] = INT_MAX;
      }
      exist[i]=0;
      id[0] = 0;
   }

   if(select==1)
   {
	   read_graph(W,W_index,exist,id,file0,DEG,N);
   }

   //Generate a uniform random graph
   if(select==0)
   {
      init_weights(N, DEG, W, W_index);
   }

   //Synchronization Variables
   pthread_barrier_init(&barrier, NULL, P);
   pthread_mutex_init(&lock, NULL);
   for(int i=0; i<N; i++)
   {
      pthread_mutex_init(&locks[i], NULL);
      if(select==0)
         exist[i]=1;
   } 

   int largest = N;
   largest_d = largest;

   //Initialize data structures
   initialize_single_source(D, D_temp, Q, 0, N);

   PMAX = P; //for atomic barrier

   //Thread Arguments
   for(int j = 0; j < P; j++) {
      thread_arg[j].local_min  = local_min_buffer;
      thread_arg[j].global_min = &global_min_buffer;
      thread_arg[j].Q          = Q;
      thread_arg[j].D          = D;
      thread_arg[j].D_temp     = D_temp;
      thread_arg[j].W          = W;
      thread_arg[j].W_index    = W_index;
      thread_arg[j].d_count    = &d_count;
      thread_arg[j].tid        = j;
      thread_arg[j].P          = P;
      thread_arg[j].N          = N;
      thread_arg[j].DEG        = DEG;
      thread_arg[j].barrier    = &barrier;
   }

   //for clock time
   struct timespec requestStart, requestEnd;
   clock_gettime(CLOCK_REALTIME, &requestStart);

   // Enable Graphite performance and energy models
   //CarbonEnableModels();

   //create threads
   for(int j = 1; j < P; j++) {
      pthread_create(thread_handle+j,
            NULL,
            do_work,
            (void*)&thread_arg[j]);
   }
   do_work((void *) &thread_arg[0]);

   //join threads
   for(int j = 1; j < P; j++) { //mul = mul*2;
      pthread_join(thread_handle[j],NULL);
   }

   // Disable Graphite performance and energy models
   //CarbonDisableModels();

   //read clock for time
   clock_gettime(CLOCK_REALTIME, &requestEnd);
   double accum = ( requestEnd.tv_sec - requestStart.tv_sec ) + ( requestEnd.tv_nsec - requestStart.tv_nsec ) / BILLION;
   printf( "Elapsed time: %lfs\n", accum );

   //printf("\ndistance:%d \n",D[N-1]);

   make_dot_graph(W,W_index,exist,D,N,DEG,"rgraph.dot");

   printf("\n Iterations taken:%d",cntr);

   //for distance values check
   FILE * pfile;
   pfile = fopen("myfile.txt","w");
   fprintf (pfile,"distances:\n");
   for(int i = 0; i < N; i++) {
      if(D[i] != INT_MAX) {
         fprintf(pfile,"distance(%d) = %d\n", i, D[i]);
      }
   }
   fclose(pfile);
   printf("\n");

   return 0;
}

int initialize_single_source(int*  D,
      int*  D_temp,
      int*  Q,
      int   source,
      int   N)
{
   for(int i = 0; i < N+1; i++)
   {
      D[i] = INT_MAX;
      D_temp[i] = INT_MAX;
      Q[i] = 1;
   }

   D[source] = 0;
   D_temp[source] = 0;
   return 0;
}

int get_local_min(volatile int* Q, volatile int* D, int start, int stop, int N, int** W_index, int** W, int u)
{
   int min = INT_MAX;
   int min_index = N;

   for(int i = start; i < stop; i++) 
   {
      if(W_index[u][i]==-1 || W[u][i]==INT_MAX)
         continue;
      if(D[i] < min && Q[i]) 
      {
         min = D[i];
         min_index = W_index[u][i];
      }
   }
   return min_index;
}

void relax(int u, int i, volatile int* D, int** W, int** W_index, int N)
{
   if((D[W_index[u][i]] > (D[u] + W[u][i]) && (W_index[u][i]!=-1 && W_index[u][i]<N && W[u][i]!=INT_MAX)))
      D[W_index[u][i]] = D[u] + W[u][i];
}

void init_weights(int N, int DEG, int** W, int** W_index)
{
   // Initialize to -1
   for(int i = 0; i < N; i++)
      for(int j = 0; j < DEG; j++)
         W_index[i][j]= -1;

   // Populate Index Array
   for(int i = 0; i < N; i++)
   {
      int last = 0;
      for(int j = 0; j < DEG; j++)
      {
         if(W_index[i][j] == -1)
         {
            int neighbor = i + j;//rand()%(max);
            if(neighbor > last)
            {
               W_index[i][j] = neighbor;
               last = W_index[i][j];
            }
            else
            {
               if(last < (N-1))
               {
                  W_index[i][j] = (last + 1);
                  last = W_index[i][j];
               }
            }
         }
         else
         {
            last = W_index[i][j];
         }
         if(W_index[i][j]>=N)
         {
            W_index[i][j] = N-1;
         }
      }
   }

   // Populate Cost Array
   for(int i = 0; i < N; i++)
   {
      for(int j = 0; j < DEG; j++)
      {
         double v = drand48();
         /*if(v > 0.8 || W_index[i][j] == -1)
           {       W[i][j] = MAX;
           W_index[i][j] = -1;
           }

           else*/ if(W_index[i][j] == i)
         W[i][j] = 0;

         else
            W[i][j] = (int) (v*100) + 1;
         //printf("   %d  ",W_index[i][j]);
      }
      //printf("\n");
   }
}
