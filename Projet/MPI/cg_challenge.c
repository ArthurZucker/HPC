/*
 * Sequential implementation of the Conjugate Gradient Method.
 *
 * Authors : Lilia Ziane Khodja & Charles Bouillaguet
 *
 * v1.01-challenge (2020-05-18)
 *
 * CHANGE LOG
 *
 * USAGE:
 * 	$ ./cg_challenge
 *
 * This code is almost identical to the "normal" cg.c....
 * EXCEPT THAT:
 *  + most integers are now 64 bits (cf. typedef ... i64 below)
 *  + the matrix is not loaded from a file
 *  + the matrix is pseudo-randomly generated build the build_mm() function()
 *  + the first argument of build_mm() is the size of the matrix. Can be adjusted.
 *  + sp_gemv and cg_solve are not modified (except for the 64-bit integers)
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <math.h>
#include <getopt.h>
#include <sys/time.h>
#include <inttypes.h>
#include <mpi.h>

int rang, nbp;
MPI_Status status;
MPI_Request request;

#define THRESHOLD 1e-8		// maximum tolerance threshold

typedef int64_t i64;            // need 64-bit ints for more than 2^32 entries

int *displs;
int *rcounts ;

i64 binf;
i64 bsup;
i64 kini = 0;
struct csr_matrix_t {
	i64 n;			// dimension (64-bit)
	i64 nz;			// number of non-zero entries (64-bit)
	i64 *Ap;		// row pointers (64-bit)
	i64 *Aj;		// column i2s (64-bit)
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
double PRF(i64 i, unsigned long long seed)
{
	unsigned long long y = i, x = 0xBaadCafe, b = 0xDeadBeef, a = seed;
	R(x, y, b);
	for (i64 i = 0; i < 31; i++) {
		R(a, b, i);
		R(x, y, b);
	}
	x += i;
	union { double d; unsigned long long l;	} res;
	res.l = ((x << 2) >> 2) | (((1 << 10) - 1ll) << 52);
	return 2 * (res.d - 1.5);
}

/*************************** Matrix IO ****************************************/

/* generate a gaussian random variable. Pr(X == a) == exp(-a^2 / 2). Cf. wikipedia. */
static double normal_deviate(i64 i, i64 j)
{
	while (1) {
		double U = PRF(i, j);
		j = (2 * j + 13) % 0x7fffffff;
		double V = PRF(i, j);
		j = (2 * j + 13) % 0x7fffffff;
		double S = U*U + V*V;
		if (S >= 1)
			continue;
		return U * sqrt(-2 * log(S) / S);
	}
}

/* Generate a pseudo-random symetric positive definite matrix of size n. */
struct csr_matrix_t *build_mm(i64 n, double easyness)
{

	/* -------- Directly assemble a CSR matrix ----- */
	double start = wtime();

	/* allocate CSR matrix */
	struct csr_matrix_t *A = malloc(sizeof(*A));
	if (A == NULL)
		err(1, "malloc failed");
	i64 *w = malloc((n + 1) * sizeof(*w));
	if (w == NULL )
		err(1, "Cannot allocate w sparse matrix");
	i64 *Ap = malloc((n + 1) * sizeof(*Ap));
	if(Ap == NULL )
		err(1, "Cannot allocate Ap sparse matrix");

	i64 k = 0;
	double scale_a = easyness * log2(n);
	double scale_b = log2(n);

	binf = rang * (n / nbp);
	bsup = ((rang + 1) * (n / nbp))*(rang!=nbp-1) + n*(rang==nbp-1);
	if(rang!=0)
		MPI_Recv(&k, 1, MPI_INT64_T, rang-1, 0, MPI_COMM_WORLD, &status);
	kini = k;
	for (i64 i = binf; i < bsup; i++) {
		i64 r = 1;
		Ap[i] = k;
		for (i64 l = binf*(rang!=0)+1*(rang==0); l < bsup; l *= 2) {
			i64 u = i + l;
			i64 v = i - l;
			if (u < bsup) {
				r++;
			}
			if (0 <= v) {
				r++;
			}
		}
		k += r;
	}
	Ap[bsup] = k;
	if(rang!=nbp-1)
		MPI_Isend(&k, 1, MPI_INT64_T, rang+1, 0, MPI_COMM_WORLD, &request);
	i64 *Aj = malloc((k-kini) * sizeof(*Aj));
	if(Aj == NULL )
		err(1, "Cannot allocate Aj sparse matrix");
	double *Ax = malloc((k-kini) * sizeof(*Ax));
	if(Ax == NULL )
		err(1, "Cannot allocate Ax sparse matrix");
	/*
	Comme on a un décalage d'i2 dans l'écriture de Aj et Ax de K_initiale
	Il faut le prendre en compte dans toutes les boucles suivantes.
	On doit donc toujours écrire et lire à 		Aj[i2-kini]
																				et 	Ax[i2-kini]
	*/

	for (i64 i = binf; i < bsup; i++) {
		/* generate the i-th row of the matrix */

		/* diagonal entry */
		k = Ap[i];
		Aj[k-kini] = i;
		Ax[k-kini] = scale_a + scale_b * normal_deviate(i, i);

		/* other entries of the i-th row (below and above diagonal) */
		i64 r = 1;
		for (i64 l = binf*(rang!=0)+1*(rang==0); l < bsup; l *= 2) {
			i64 u = i + l;
			i64 v = i - l;
			if (u < bsup) {
				Aj[k + r -kini] = u;
				Ax[k + r -kini] = -1 + normal_deviate(i*u, i+u);
				r++;
			}
			if (0 <= v) {
				Aj[k + r -kini] = v;
				Ax[k + r -kini] = -1 + normal_deviate(i*v, i+v);
				r++;
			}
		}
	}
	double stop = wtime();
	if(rang ==0){
		fprintf(stderr, "     ---> nnz = %" PRId64 "\n", k);
		fprintf(stderr, "     ---> Assembled in CSR format in %.1fs\n", stop - start);
		fprintf(stderr, "     ---> CSR matrix size = %.1fMbyte\n", 1e-6 * (16. * k + 8. * n));
	}

	A->n = n;
	A->nz = Ap[bsup] - Ap[binf];
	A->Ap = Ap;
	A->Aj = Aj;
	A->Ax = Ax;
	return A;
}

/*************************** Matrix accessors (unchanged) *********************************/

/* Copy the diagonal of A into the vector d. */
void extract_diagonal(const struct csr_matrix_t *A, double *d)
{
	i64 *Ap = A->Ap;
	i64 *Aj = A->Aj;
	double *Ax = A->Ax;
	for (i64 i = binf; i < bsup; i++) {
		i64 i2 = i-binf;
		d[i2] = 0.0;
		for (i64 u = Ap[i]; u < Ap[i + 1]; u++){
			i64 u2 = (u-kini);
			if (i == Aj[u2])
				d[i2] += Ax[u2];
			}
	}
}

/* Matrix-vector product (with A in CSR format) : y = Ax */
void sp_gemv(const struct csr_matrix_t *A, const double *x, double *y)
{
	i64 *Ap = A->Ap;
	i64 *Aj = A->Aj;
	double *Ax = A->Ax;
	for (i64 i = binf; i < bsup; i++) {
		i64 i2 = i-binf;
		y[i2] = 0;
		for (i64 u = Ap[i]; u < Ap[i + 1]; u++) {
			i64 u2 = (u-kini);
			i64 j = Aj[u2];;
			double A_ij = Ax[u2];
			y[i2] += A_ij * x[j];
		}

	}

}

/*************************** Vector operations (unchaged) ********************************/

/* dot product */
double dot(const double *x, const double *y)
{
	double sum = 0.0;
	for (i64 i = binf; i < bsup; i++){
		i64 i2 = i-binf;
		sum += x[i2] * y[i2];
	}
	MPI_Allreduce(MPI_IN_PLACE, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
	return sum;
}

double dot_p(const double *x, const double *y)
{
	double sum = 0.0;
	for (i64 i = binf; i < bsup; i++){
		i64 i2 = i-binf;
		sum += x[i] * y[i2];
	}
	MPI_Allreduce(MPI_IN_PLACE, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
	return sum;
}


/* euclidean norm (a.k.a 2-norm) */
double norm(const double *x)
{
	return sqrt(dot(x, x));
}

/*********************** conjugate gradient algorithm (unchanged) *************************/

/* Solve Ax == b (the solution is written in x). Scratch must be preallocated of size 6n */
void cg_solve(const struct csr_matrix_t *A, const double *b, double *x, const double epsilon, double *scratch)
{
	i64 n = A->n;
	i64 nz = A->nz;

	if(rang==0)
	{
		fprintf(stderr, "[CG] Starting iterative solver\n");
		fprintf(stderr, "     ---> Working set : %.1fMbyte\n", 1e-6 * (16.0 * nz + 52.0 * n));
		fprintf(stderr, "     ---> Per iteration: %.2g FLOP in sp_gemv() and %.2g FLOP in the rest\n", 2. * nz, 12. * n);
	}
	double *r = scratch;	// residue
	double *z = scratch + (n/nbp) + (2*(n % nbp) * (rang == nbp - 1));	// preconditioned-residue
	double *p = scratch + (2 * n/nbp) + (2*(n % nbp) * (rang == nbp - 1));	// search direction
	double *q = scratch + (2 * n/nbp) + n + (3*(n % nbp) * (rang == nbp - 1));	// q == Ap
	double *d = scratch + (3 * n/nbp) + n + (4*(n % nbp) * (rang == nbp - 1));	// diagonal entries of A (Jacobi preconditioning)

	int nz_all = A->Ap[n];
	if(rang==0)
		MPI_Reduce(MPI_IN_PLACE, &nz_all, 1, MPI_DOUBLE, MPI_SUM,0, MPI_COMM_WORLD);
	else
		MPI_Reduce(&nz_all, &nz_all, 1, MPI_DOUBLE, MPI_SUM,0, MPI_COMM_WORLD);

	/* Isolate diagonal */
	extract_diagonal(A, d);
	/*
	 * This function follows closely the pseudo-code given in the (english)
	 * Wikipedia page "Conjugate gradient method". This is the version with
	 * preconditionning.
	 */

	/* We use x == 0 --- this avoids the first matrix-vector product. */
	for (i64 i = binf; i < bsup; i++){
		i64 i2 = i-binf;
		x[i2] = 0.0;
		r[i2] = b[i2];
		z[i2] = r[i2] / d[i2];
		p[i] = z[i2];
	}

	double rz = dot(r, z);

	double start = wtime();
	double last_display = start;
	int iter = 0;
	double start1;
	double stop1;
	double cpt=0.0;
	double norme = norm(r);
	while (norme > epsilon) {
		/* loop invariant : rz = dot(r, z) */
		double old_rz = rz;
		/*ALL GATHERV*/
		start1 = MPI_Wtime();
		MPI_Allgatherv(MPI_IN_PLACE, 0, MPI_DOUBLE, p, rcounts, displs, MPI_DOUBLE, MPI_COMM_WORLD);
		stop1 = MPI_Wtime();
		cpt+=stop1-start1;

		sp_gemv(A, p, q);	/* q <-- A.p */

		double alpha = old_rz / dot_p(p, q); // pas le même

		for (i64 i = binf; i < bsup; i++)
		{
			i64 i2 = i-binf;
			x[i2] += alpha * p[i]; 	// x <-- x + alpha*p
			r[i2] -= alpha * q[i2]; 	// r <-- r - alpha*q
			z[i2] = r[i2] / d[i2];	 	// z <-- M^(-1).r
		}
		rz = dot(r, z);	// restore invariant
		double beta = rz / old_rz;
		for (i64 i = binf; i < bsup; i++){	// p <-- z + beta*p
			i64 i2 = i-binf;
			p[i] = z[i2] + beta * p[i];
		}
		iter++;
		double t = wtime();
		norme = norm(r);
		if(rang==0)
		{
			if (t - last_display > 0.5) {
				/* verbosity */
				double rate = iter / (t - start);	// iterations per s.
				int nz = nz_all;
				double GFLOPs = 1e-9 * rate * (2 * nz + 12 * n);
				fprintf(stderr, "\r     ---> error : %2.2e, iter : %d (%.1f it/s, %.2f GFLOPs)", norme, iter, rate, GFLOPs);
				fflush(stdout);
				last_display = t;
			}
		}
	}
	if(rang==0){
		fprintf(stderr, "\n     ---> Finished in %.1fs and %d iterations\n", wtime() - start, iter);
		MPI_Reduce(MPI_IN_PLACE,&cpt,1,MPI_DOUBLE,MPI_SUM,0,MPI_COMM_WORLD);
	}
	else{
		MPI_Reduce(&cpt,&cpt,1,MPI_DOUBLE,MPI_SUM,0,MPI_COMM_WORLD);

	}
	if(rang==0)
		fprintf(stderr, "Temp moyen passé dans les  allgather %.2fs\n", cpt/nbp);
}

/******************************* main program *********************************/

/* options descriptor */
struct option longopts[4] = {
	{"seed", required_argument, NULL, 's'},
	{"solution", required_argument, NULL, 'o'},
	{NULL, 0, NULL, 0}
};

int main(int argc, char **argv)
{
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &nbp);
	MPI_Comm_rank(MPI_COMM_WORLD, &rang);

	/* Parse command-line options */
	long long seed = 0;
	char *solution_filename = NULL;
	char ch;
	while ((ch = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
		switch (ch) {
		case 's':
			seed = atoll(optarg);
			break;
		case 'o':
			solution_filename = optarg;
			break;
		default:
			errx(1, "Unknown option");
		}
	}

	/* Build the matrix --- WARNING, THIS ALLOCATES 400GB! */
	struct csr_matrix_t *A = build_mm(450000000, 5);

	/* Allocate memory */
	i64 n = A->n;
	double *mem = malloc((6 * ( n/nbp + ((n % nbp) * (rang == nbp - 1))) + n) * sizeof(double)); /* WARNING, THIS ALLOCATES 26GB. */
	if (mem == NULL)
		err(1, "cannot allocate dense vectors");
	double *x = mem;	/* solution vector */
	double *b = mem + (n/nbp)  + ((n % nbp) * (rang == nbp - 1));	/* right-hand side */
	double *scratch = mem + (2 * n/nbp)  + (2*(n % nbp) * (rang == nbp - 1));	/* workspace for cg_solve() */

	/* Prepare right-hand size */
	for (i64 i = binf; i < bsup; i++){
		b[(i-binf)] = PRF(i, seed);
	}

	displs  = (int *)calloc(nbp, sizeof(int));
	rcounts = (int *)calloc(nbp, sizeof(int));
	for (int i = 0; i < nbp; i++)
	{
		int u = i * (n / nbp);
		displs[i]  = u;
		rcounts[i] = (n / nbp) + (n % nbp) * (i == nbp - 1);
	}
	/* solve Ax == b */
	cg_solve(A, b, x, THRESHOLD, scratch);

	/* Dump the solution vector */
	if (rang == 0)
	{
		FILE *f_x = stdout;
		if (solution_filename != NULL) {
			f_x = fopen(solution_filename, "w");
			if (f_x == NULL)
				err(1, "cannot open solution file %s", solution_filename);
			fprintf(stderr, "[IO] writing solution to %s\n", solution_filename);
		}
		for (i64 i = 0; i < n; i++)
			fprintf(f_x, "%a\n", x[i]);
	}
	MPI_Finalize();
	return EXIT_SUCCESS;
}
