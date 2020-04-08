/*
 * Sequential implementation of the Conjugate Gradient Method.
 *
 * Authors : Lilia Ziane Khodja & Charles Bouillaguet
 *
 * v1.01 (2020-03-11)
 *
 * CHANGE LOG:
 *    v1.01 : fix a minor printing bug in load_mm (incorrect CSR matrix size)
 *
 * USAGE:
 * 	$ ./cg --matrix bcsstk13.mtx                # loading matrix from file
 *      $ ./cg --matrix bcsstk13.mtx > /dev/null    # ignoring solution
 *	$ ./cg < bcsstk13.mtx > /dev/null           # loading matrix from stdin
 *      $  zcat matrix.mtx.gz | ./cg                # loading gziped matrix from
 *      $ ./cg --matrix bcsstk13.mtx --seed 42      # changing right-hand side
 *      $ ./cg --no-check < bcsstk13.mtx            # no safety check
 *
 * PRO-TIP :
 *      # downloading and uncompressing the matrix on the fly
 *	$ curl --silent http://hpc.fil.cool/matrix/bcsstk13.mtx.gz | zcat | ./cg
 */
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <math.h>
#include <getopt.h>
#include <sys/time.h>
#include <mpi.h>
#include "mmio.h"
#include <unistd.h>
/*Variables pour MPI*/
//=======================================
int rang,nbp;
MPI_Status status;
MPI_Request request;
int N;
#define NoDEBUG
//=======================================


#define THRESHOLD 1e-8		// maximum tolerance threshold

struct csr_matrix_t {
	int n;			// dimension
	int nz;			// number of non-zero entries
	int *Ap;		// row pointers
	int *Aj;		// column indices
	double *Ax;		// actual coefficient
};

/*************************** Utility functions ********************************/

/* Seconds (wall-clock time) since an arbitrary point in the past */
double wtime()
{
	struct timeval ts;
	gettimeofday(&ts, NULL);
	return (double)ts.tv_sec + ts.tv_usec / 1e6;
}

/* Pseudo-random function to initialize b (rumors says it comes from the NSA) */
#define ROR(x, r) ((x >> r) | (x << (64 - r)))
#define ROL(x, r) ((x << r) | (x >> (64 - r)))
#define R(x, y, k) (x = ROR(x, 8), x += y, x ^= k, y = ROL(y, 3), y ^= x)
double PRF(int i, unsigned long long seed)
{
	unsigned long long y = i, x = 0xBaadCafe, b = 0xDeadBeef, a = seed;
	R(x, y, b);
	for (int i = 0; i < 31; i++) {
		R(a, b, i);
		R(x, y, b);
	}
	x += i;
	union { double d; unsigned long long l;	} res;
	res.l = ((x << 2) >> 2) | (((1 << 10) - 1ll) << 52);
	return 2 * (res.d - 1.5);
}

/*************************** Matrix IO ****************************************/

/* Load MatrixMarket sparse symetric matrix from the file descriptor f */
struct csr_matrix_t *load_mm(FILE * f)
{
	// On parallélise pour que chacun n'ai que sa partie de A
	MM_typecode matcode;
	int n, m, nnz;
	/* -------- STEP 1 : load the matrix in COOrdinate format */
	double start = wtime();

	/* read the header, check format */
	int *Ti ;
	int *Tj;
	double *Tx;
	double stop;
	if (mm_read_banner(f, &matcode) != 0)
		errx(1, "Could not process Matrix Market banner.\n");
	if (!mm_is_matrix(matcode) || !mm_is_sparse(matcode))
		errx(1, "Matrix Market type: [%s] not supported (only sparse matrices are OK)", mm_typecode_to_str(matcode));
	if (!mm_is_symmetric(matcode) || !mm_is_real(matcode))
		errx(1, "Matrix type [%s] not supported (only real symmetric are OK)", mm_typecode_to_str(matcode));
	if (mm_read_mtx_crd_size(f, &n, &m, &nnz) != 0)
		errx(1, "Cannot read matrix size");
	if (rang == 0) {
		fprintf(stderr, "[IO] Loading [%s] %d x %d with %d nz in triplet format\n", mm_typecode_to_str(matcode), n, n, nnz);
		fprintf(stderr, "     ---> for this, I will allocate %.1f MByte\n", 1e-6 * (40.0 * nnz + 8.0 * n));
		/* Allocate memory for the COOrdinate representation of the matrix (lower-triangle only) */
		Ti = malloc(nnz * sizeof(*Ti));
		Tj = malloc(nnz * sizeof(*Tj));
		Tx = malloc(nnz * sizeof(*Tx));
		if (Ti == NULL || Tj == NULL || Tx == NULL)
			err(1, "Cannot allocate (triplet) sparse matrix");

		/* Parse and load actual entries */
		for (int u = 0; u < nnz; u++) {
			int i, j;
			double x;
			if (3 != fscanf(f, "%d %d %lg\n", &i, &j, &x))
				errx(1, "parse error entry %d\n", u);
			Ti[u] = i - 1;	/* MatrixMarket is 1-based */
			Tj[u] = j - 1;
			Tx[u] = x;
		}

		double stop = wtime();
		fprintf(stderr, "     ---> loaded in %.1fs\n", stop - start);
		start = wtime();
	}

	/* -------- STEP 2: Convert to CSR (compressed sparse row) representation ----- */
<<<<<<< HEAD:Projet/MPI/cg_load.c
=======
	double start2 = wtime();
	MPI_Bcast(&n,1,MPI_INT,0,MPI_COMM_WORLD);
	MPI_Bcast(&nnz,1,MPI_INT,0,MPI_COMM_WORLD);
	double stop2 = wtime();
	if(rang!=0)
		fprintf(stderr, "     ---> envoie de n et nnz %.1fs\n", stop2 - start2);
>>>>>>> 36581539cb9d13b1358e8d76fcfb698550cb7090:Projet/MPI/cg_same_A.c
	/* allocate CSR matrix */
	struct csr_matrix_t *A = malloc(sizeof(*A));
	if (A == NULL)
		err(1, "malloc failed");
	int *w;
	int *Ap;
	int *Aj;
	N = n;
	// Fixing the total size to be divided
	while (n%nbp != 0) {
		n++;
	}
	fprintf(stderr, "\nn = %d\n",n );
	double *Ax;
	int sum = 0;
	int sum2 = 0;
	int m1 = 0;
	int tab[nbp];
	if (rang == 0) {
		w  = calloc((n + 1) , sizeof(*w )); 			//Initialise à 0
		Ap = malloc((n + 1) * sizeof(*Ap));
		Aj = malloc(2 * nnz * sizeof(*Ap));		//Peut on diviser par nbp??
		Ax = malloc(2 * nnz * sizeof(*Ax));		//Peut on diviser par nbp??
		if (w == NULL || Ap == NULL || Aj == NULL || Ax == NULL)
			err(1, "Cannot allocate (CSR) sparse matrix");
		/* the following is essentially a bucket sort */
		/* Count the number of entries in each row */
		for (int u = 0; u < nnz; u++) {
			int i = Ti[u];
			int j = Tj[u];
			w[i]++; 		// je stocke dans w une info sur chaque nnz
			if (i != j)	/* the file contains only the lower triangular part */
				w[j]++;
		}
	/* Compute row pointers (prefix-sum) */
		for (int i = 0; i <n ; i++) {
			Ap[i] = sum;
			sum += w[i];
			w[i] = Ap[i];
			if ((i%(n/nbp) == 0 || i==n-1) && i !=0)	{
				if (m1==0){
					tab[m1] = sum;
					sum2 = sum;
					m1++;

				}
				else{
					tab[m1] = sum-sum2;
					sum2 = sum;
					m1++;
				}
			}

		}
		Ap[n] = sum;
	}
	/* on distribue le bon nnz pour chaque proccesseur */
	MPI_Scatter(&tab,1,MPI_INT,&sum,1,MPI_INT,0,MPI_COMM_WORLD);
	fprintf(stderr, "processeur %d : nnz %d \n",rang,sum);
	// Above is OK!!
	if (rang == 0) {
	/* Dispatch entries in the right rows */
	//fprintf(stderr, "processeur %d : nnz %d \n",rang,sum);
		for (int u = 0; u < nnz; u++) {
			int i = Ti[u];
			double x = Tx[u]; // les nnz sont dans Ti TJ et Tx
			int j = Tj[u];
			Aj[w[i]] = j; // pour chaque éléments non nul , je les ajoutes au bonne endroit dans Aj ou Ax (je crois)
			Ax[w[i]] = x;
			w[i]++;
			if (i != j) {	/* off-diagonal entries are duplicated */
				Aj[w[j]] = i;
				Ax[w[j]] = x;
				w[j]++;
			}
		}
		free(w);
		free(Ti);
		free(Tj);
		free(Tx);
		stop = wtime();
		fprintf(stderr, "     ---> converted to CSR format in %.1fs\n", stop - start);
		fprintf(stderr, "     ---> CSR matrix size = %.1fMbyte\n", 1e-6 * (24. * nnz + 4. * n));
	}
<<<<<<< HEAD:Projet/MPI/cg_load.c
	else	{
		Ap = malloc(((n/nbp)+1) * sizeof(*Ap)); //n+1 le plus 1 vient de la diagonale
		Aj = malloc((-Ap[(rang*n/nbp)]+Ap[(rang+1)*n/nbp]) * sizeof(*Ap));
		Ax = malloc((-Ap[(rang*n/nbp)]+Ap[(rang+1)*n/nbp]) * sizeof(*Ax));
	}
	// OK
	A->n = n/nbp;
=======

	start = wtime();
	MPI_Bcast(&sum,1,MPI_INT,0,MPI_COMM_WORLD);
	MPI_Bcast(Ap,n + 1,MPI_INT,0,MPI_COMM_WORLD);
	MPI_Bcast(Aj,2*nnz,MPI_INT,0,MPI_COMM_WORLD);
	MPI_Bcast(Ax,2*nnz,MPI_DOUBLE,0,MPI_COMM_WORLD);
	stop = wtime();
	if(rang!=0)
		fprintf(stderr, "     ---> Envoie de sum, Ap, Aj et Ax %.1fs\n", stop - start);

	A->n = n;
>>>>>>> 36581539cb9d13b1358e8d76fcfb698550cb7090:Projet/MPI/cg_same_A.c
	A->nz = sum;
	if (rang==0) {
		for (int i = 1; i < nbp; i++) {
			MPI_Send(&Ap[i*n/nbp], (n/nbp)+1,MPI_INT,i,0,MPI_COMM_WORLD);
		}
	}
	else{
		MPI_Recv(Ap,(n/nbp)+1,MPI_INT,0,0,MPI_COMM_WORLD,&status);
	}
	A->Ap = Ap;
	if (rang==0) {
		for (int i = 1; i < nbp; i++) {
			int u = Ap[i*n/nbp];
			MPI_Send(&Aj[u], (-Ap[(i*n/nbp)]+Ap[(i+1)*n/nbp]),MPI_INT,i,0,MPI_COMM_WORLD);
			MPI_Send(&Ax[u], (-Ap[(i*n/nbp)]+Ap[(i+1)*n/nbp]),MPI_DOUBLE,i,0,MPI_COMM_WORLD);
		}
	}
	else{
		MPI_Recv(Aj,(-Ap[(rang*n/nbp)]+Ap[(rang+1)*n/nbp]),MPI_INT,0,0,MPI_COMM_WORLD,&status);
		MPI_Recv(Ax,(-Ap[(rang*n/nbp)]+Ap[(rang+1)*n/nbp]),MPI_DOUBLE,0,0,MPI_COMM_WORLD,&status);
	}
	// if (rang == 1) {
	// 	fprintf(stderr,"\n______________--------_____----_-_-____-_-_-__-_-_-____------\n" );
	// 	for (int i = 0; i < n; i++) {
	// 		for (int u = Ap[i]; u < Ap[i + 1]; u++){
	// 			fprintf(stderr,"\nAp[i+1] = %d\tsum = %d,[u] = %d\t",Ap[i+1],2*sum,u);
	// 			int j = Aj[u];
	// 			fprintf(stderr,"Aj = %d\t",j);
	// 			double A_ij = Ax[u];
	// 		}
	// 	}
	// }
	A->Aj = Aj;
	A->Ax = Ax;
	return A;
}

/*************************** Matrix accessors *********************************/

/* Copy the diagonal of A into the vector d. */
void extract_diagonal(const struct csr_matrix_t *A, double *d)
{
	int n = A->n;
	int *Ap = A->Ap;
	int *Aj = A->Aj;
	double *Ax = A->Ax;
	//fprintf(stderr,"\n%d : extract_diagonal\n",rang);
	for (int i = 0; i < n; i++) {
		d[i] = 0.0;
		for (int u = Ap[i]; u < Ap[i + 1]; u++){
			//fprintf(stderr,"\n%d \n",i);
			//fprintf(stderr,"\n Aj[u]=%d \n",Aj[u]);

			if (rang==0 && i == Aj[u]){
				d[i] += Ax[u];
				//if(rang==0)
				fprintf(stderr,"\n%d : extract_diagonal i=%d u=%d\n",rang,i,u);
			}
			else{
				if (i*rang*n == Aj[u]){
					d[i] += Ax[u];
					//if(rang==0)
					fprintf(stderr,"\n%d : extract_diagonal i=%d u=%d\n",rang,i,u);
				}
			}
		}
	}
}

/* Matrix-vector product (with A in CSR format) : y = Ax */
void sp_gemv(const struct csr_matrix_t *A, const double *x, double *y)
{
	//=======================================
	int n = A->n;
	int *Ap = A->Ap;
	int *Aj = A->Aj;
	double *Ax = A->Ax;
	fprintf(stderr,"\n%d : sp_gemv\n",rang);
	for (int i = 0; i < n; i++) {
		y[i] = 0;
		for (int u = Ap[i]; u < Ap[i + 1]; u++) {
			int j = Aj[u];
			double A_ij = Ax[u];
			// Issue with Aj's value left un initialized
			if(j>u && u > 55000){
				j = 0;
				fprintf(stderr, "%d : \\%d|%d/ fails, A[u+1] == %d\n",rang,j,u+n,Ap[i+1] );
			}

			y[i] += A_ij * x[j];
		}

	}
}

/*************************** Vector operations ********************************/

/* dot product */
double dot(const int n, const double *x, const double *y)
{
	double sum = 0.0;
	for (int i = 0; i < n; i++)
		sum += x[i] * y[i];
	MPI_Allreduce(MPI_IN_PLACE,&sum,1,MPI_DOUBLE,MPI_SUM,MPI_COMM_WORLD);
	fprintf(stderr, "%d : summmmmm = %f\n",rang,sum );
	return sum;
}

/* euclidean norm (a.k.a 2-norm) */
double norm(const int n, const double *x)
{
	return sqrt(dot(n, x, x));
}

/*********************** conjugate gradient algorithm *************************/

/* Solve Ax == b (the solution is written in x). Scratch must be preallocated of size 6n */
void cg_solve(const struct csr_matrix_t *A, const double *b, double *x, const double epsilon, double *scratch)
{
	int n = A->n;
	int nz = A->nz;
	if (rang == 0) {
		fprintf(stderr, "[CG] Starting iterative solver\n");
		fprintf(stderr, "     ---> Working set : %.1fMbyte\n", 1e-6 * (12.0 * nz + 52.0 * n));
		fprintf(stderr, "     ---> Per iteration: %.2g FLOP in sp_gemv() and %.2g FLOP in the rest\n", 2. * nz, 12. * n);
	}

	double *r = scratch + n;	// residue
	double *z = scratch + 2 * n;	// preconditioned-residue
	double *p = scratch + 3 * n;	// search direction
	double *q = scratch + 4 * n;	// q == Ap
	double *d = scratch + 5 * n;	// diagonal entries of A (Jacobi preconditioning)

	/* Isolate diagonal */
	extract_diagonal(A, d);

	/*
	 * This function follows closely the pseudo-code given in the (english)
	 * Wikipedia page "Conjugate gradient method". This is the version with
	 * preconditionning.
	 */

	/* We use x == 0 --- this avoids the first matrix-vector product. */
	for (int i = 0; i < n; i++)
		x[i] = 0.0;
	for (int i = 0; i < n; i++)	// r <-- b - Ax == b
		r[i] = b[i];
	for (int i = 0; i < n; i++)	// z <-- M^(-1).r
		z[i] = r[i] / d[i];
	for (int i = 0; i < n; i++)	// p <-- z
		p[i] = z[i];
	if (rang==0) {
		sleep(3);
	}else{
		fprintf(stderr, "0 should be sleeping");
		for (int i = 0; i < n; i++) {
			//fprintf(stderr, "%d : z[i] = %f\n", rang,z[i]);
		}
	}

	double rz = dot(n, r, z);
	fprintf(stderr, "First dot of cg_solve done\n" );
	double start = wtime();
	double last_display = start;
	int iter = 0;

	while (norm(n, r) > epsilon) {

		/* loop invariant : rz = dot(r, z) */
		double old_rz = rz;
		sp_gemv(A, p, q);	/* q <-- A.p */
		double alpha = old_rz / dot(n, p, q);
		for (int i = 0; i < n; i++)	// x <-- x + alpha*p
			x[i] += alpha * p[i];
		for (int i = 0; i < n; i++)	// r <-- r - alpha*q
			r[i] -= alpha * q[i];
		for (int i = 0; i < n; i++)	// z <-- M^(-1).r
			z[i] = r[i] / d[i];
		rz = dot(n, r, z);	// restore invariant
		double beta = rz / old_rz;
		for (int i = 0; i < n; i++)	// p <-- z + beta*p
			p[i] = z[i] + beta * p[i];
		iter++;
		double t = wtime();
		if (t - last_display > 0.5) {
			/* verbosity */
			double rate = iter / (t - start);	// iterations per s.
			double GFLOPs = 1e-9 * rate * (2 * nz + 12 * n);
			fprintf(stderr, "\r     ---> error : %2.2e, iter : %d (%.1f it/s, %.2f GFLOPs)", norm(n, r), iter, rate, GFLOPs);
			fflush(stdout);
			last_display = t;
		}
		fprintf(stderr, "norme > eps?%d\n",norm(n, r) > epsilon );
	}
	fprintf(stderr, "\n     ---> Finished in %.1fs and %d iterations\n", wtime() - start, iter);
}

/******************************* main program *********************************/

/* options descriptor */
struct option longopts[6] = {
	{"seed", required_argument, NULL, 's'},
	{"rhs", required_argument, NULL, 'r'},
	{"matrix", required_argument, NULL, 'm'},
	{"solution", required_argument, NULL, 'o'},
	{"no-check", no_argument, NULL, 'c'},
	{NULL, 0, NULL, 0}
};


int main(int argc, char **argv)
{
	MPI_Init(&argc,&argv);
	MPI_Comm_size (MPI_COMM_WORLD, &nbp);
	MPI_Comm_rank (MPI_COMM_WORLD,&rang);

	long long seed = 0;
	char *rhs_filename = NULL;
	char *matrix_filename = NULL;
	char *solution_filename = NULL;
	int safety_check = 1;
	char ch;
	while ((ch = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
		switch (ch) {
		case 's':
			seed = atoll(optarg);
			break;
		case 'r':
			rhs_filename = optarg;
			break;
		case 'm':
			matrix_filename = optarg;
			break;
		case 'o':
			solution_filename = optarg;
			break;
		case 'c':
			safety_check = 0;
			break;
		default:
			errx(1, "Unknown option");
		}
	}
	/* Parse command-line options */

	/* Load the matrix */
	FILE *f_mat = stdin;
	if (matrix_filename) {
		f_mat = fopen(matrix_filename, "r");
		if (f_mat == NULL)
			err(1, "cannot matrix file %s", matrix_filename);
	}
	/* Initialisation de MPI */
	struct csr_matrix_t *A = load_mm(f_mat);
	/* Allocate memory */
	int n = A->n;
	double *mem = malloc(8 * n * sizeof(double));
	if (mem == NULL)
		err(1, "cannot allocate dense vectors");
	double *x = mem;	/* solution vector */
	double *b = mem + n;	/* right-hand side */
	double *scratch = mem + 2 * n;	/* workspace for cg_solve() */

	/* Prepare right-hand size */
	if (rhs_filename) {	/* load from file */
		if (rang == 0) {
			FILE *f_b = fopen(rhs_filename, "r");
			if (f_b == NULL)
				err(1, "cannot open %s", rhs_filename);
			fprintf(stderr, "[IO] Loading b from %s\n", rhs_filename);
			for (int i = 0; i < n; i++) {
				if (1 != fscanf(f_b, "%lg\n", &b[i]))
					errx(1, "parse error entry %d\n", i);
			}
			fclose(f_b);
		}
		MPI_Scatter(b, n, MPI_DOUBLE, MPI_IN_PLACE, 0,MPI_DOUBLE,0,MPI_COMM_WORLD);
	}
	else {
		for (int i = 0; i < n; i++)
			b[i] = PRF(i*(rang+1)*n, seed);
	}
	/* solve Ax == b */
	cg_solve(A, b, x, THRESHOLD, scratch);

	if (safety_check) {
		double *y = scratch;
		sp_gemv(A, x, y);	// y = Ax
		for (int i = 0; i < n; i++)	// y = Ax - b
			y[i] -= b[i];
		double norme = norm(n, y);
		if (rang == 0) {
			fprintf(stderr, "[check] max error = %2.2e\n", norme);
		}
	}
	// TO DO : parralléliser la lecture de fichier
	double *X = NULL;
	if (rang == 0) {
		X = malloc(N*sizeof(double));
		MPI_Gather(X,n/nbp,MPI_DOUBLE,x,n/nbp,MPI_DOUBLE,0,MPI_COMM_WORLD);
		/* Dump the solution vector */
			FILE *f_x = stdout;
			if (solution_filename != NULL) {
				f_x = fopen(solution_filename, "w");
				if (f_x == NULL)
					err(1, "cannot open solution file %s", solution_filename);
				fprintf(stderr, "[IO] writing solution to %s\n", solution_filename);
			}
			for (int i = 0; i < N; i++)
				fprintf(f_x, "%a\n", X[i]);
	}
	else{
		MPI_Gather(x,n/nbp,MPI_DOUBLE,X,n/nbp,MPI_DOUBLE,0,MPI_COMM_WORLD);
	}
	MPI_Finalize();
	return EXIT_SUCCESS;
}