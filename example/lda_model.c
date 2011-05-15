#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/queue.h>
 
#define M 2 //vector dimensions
#define NR_IMAGES 5
#define NR_TEST 4
#define MODEL_TIGER 1
#define MODEL_ELEPH 2

typedef struct {
    float *buf;
} image_t;
typedef image_t *image;

typedef	float **MATRIX;

/*image structures for the 2 learning sets*/
image tiger_images[NR_IMAGES];
image eleph_images[NR_IMAGES];
/*and for the test set*/
image test_images;

void add_elements(int class)
{
	int i;
	if(class==1)
	{
		for(i=0;i<NR_IMAGES;i++)
		{
			tiger_images[i] = (image)malloc(sizeof(image));
			tiger_images[i]->buf=(float *)malloc(2*sizeof(float));
		}
		tiger_images[0]->buf[0]=4; tiger_images[0]->buf[1]=2;
		tiger_images[1]->buf[0]=2; tiger_images[1]->buf[1]=4;
		tiger_images[2]->buf[0]=2; tiger_images[2]->buf[1]=3;
		tiger_images[3]->buf[0]=3; tiger_images[3]->buf[1]=6;
		tiger_images[4]->buf[0]=4; tiger_images[4]->buf[1]=4;
	}
	if(class==2)
	{
		for(i=0;i<NR_IMAGES;i++)
		{			
			eleph_images[i] = (image)malloc(sizeof(image));
			eleph_images[i]->buf=(float *)malloc(2*sizeof(float));
		}
		eleph_images[0]->buf[0]=9; eleph_images[0]->buf[1]=10;
		eleph_images[1]->buf[0]=6; eleph_images[1]->buf[1]=8;
		eleph_images[2]->buf[0]=9; eleph_images[2]->buf[1]=5;
		eleph_images[3]->buf[0]=8; eleph_images[3]->buf[1]=7;
		eleph_images[4]->buf[0]=10; eleph_images[4]->buf[1]=8;
	}
}

image get_random()
{
	image aux = (image)malloc(sizeof(image));
	aux->buf=(float *)malloc(2*sizeof(float));

	aux->buf[0]= rand() % 10 + 1;
	aux->buf[1]= rand() % 10 + 1;

	return aux;
}

void print_mat(MATRIX A, int row, int col)
{
	int i,j;
	for(i=0;i<col;i++)
	{
		for(j=0;j<row;j++)
			printf("%f ",A[i][j]);
		printf("\n");
	}
}

/* creates a matrixa and in case init!=-1 fills it with init */
MATRIX create_matrix(int dim_l, int dim_h, int init)
{
	unsigned int i,j;

	MATRIX mat_aux = (MATRIX) malloc(dim_l * sizeof(float *));
	for(i=0;i<dim_l;i++)
		mat_aux[i] = (float *) malloc(dim_h*sizeof(double));

	if(init==-1)
		return mat_aux;

	for(i=0;i<dim_l;i++)
		for(j=0;j<dim_h;j++)
			mat_aux[i][j]=init;

	return mat_aux;
}

/* returns the vector that represents the mean values for each pixel of the image */
float *comp_mean(int class)
{
	unsigned int i,j;
	image* image_array = ((class == MODEL_TIGER) ? tiger_images : eleph_images);

	float *mean = (float *)malloc(M*sizeof(float));
	for(j=0; j<M; j++)
	{
		mean[j]=0;
	}
	
	for (i = 0; i < NR_IMAGES; i++)
	{
		for(j=0; j<M; j++)
		{
        		mean[j] += (float) image_array[i]->buf[j];
		}
        }

	for (j=0;j<M;j++)
	{
		mean[j] = mean[j]/(float)NR_IMAGES;
	}

	return mean;
}

/* computes the difference between an image and the class mean */
MATRIX comp_diff(int img_no, int class, float *mean)
{
	unsigned int i;
	image* image_array = ((class == MODEL_TIGER) ? tiger_images : eleph_images);

	MATRIX diff = create_matrix(1,M,-1);
	
	for (i = 0; i < M; i++)
	{
        	diff[0][i] = (float) image_array[img_no]->buf[i] - mean[i];
        }

	return diff;
}

/* computes the transpose matrix for the argument */
MATRIX comp_trans(MATRIX max_aux, int row, int col)
{
	unsigned int i,j;
	MATRIX trans = create_matrix(col,row,-1);
	
	for (i=0; i<col; i++)
	{
		for (j=0; j<row; j++)
		{
        		trans[i][j] = max_aux[j][i];
		}
        }

	return trans;
}

/* add two matrix of same dimension */
MATRIX comp_add(MATRIX A, MATRIX B, int row, int col)
{
	unsigned int i,j;
	MATRIX C= create_matrix(row, col,-1);

	for (i=0; i<row; i++)
		for (j=0; j<col; j++)
			C[i][j]=A[i][j]+B[i][j];

	return C;
}

/* substract two vectors; store the result into a matrix*/
MATRIX comp_mean_diff(float *mean_tigre, float *mean_eleph)
{
	unsigned int i;
	MATRIX mean_dif = create_matrix(1,M,-1);
	for(i=0; i<M; i++)
		mean_dif[0][i] = mean_tigre[i] - mean_eleph[i];

	return mean_dif;
}

/* matrix multiplication */
MATRIX comp_mul(MATRIX A, int rowA, int colA, MATRIX B, int rowB, int colB)
{
	unsigned int i, j, k;
	MATRIX C= create_matrix(rowB, colA,-1);

	for (i=0; i<rowB; i++)
		for (j=0; j<colA; j++)
		{
			C[i][j]=0;
			for (k=0; k<colB; k++)
			{
				C[i][j] += A[k][j] * B[i][k];
			}
		}
	return C;
}

/* The inverse of the matrix - the next 3 functions are neccessary for it */
float comp_detrm(MATRIX a, int k)
{
    float s=1, det=0;
    int i, j, m, n, c;
 
    MATRIX b=create_matrix(k,k,-1);

    if (k == 1)
    {
            return a[0][0];
    }
    else
    {
            det=0;
            for(c=0;c<k;c++)
            {
                    m = 0;
                    n = 0;
                    for(i=0;i<k;i++)
                    {
                            for(j=0;j<k;j++)
                            {
                                    b[i][j] = 0;
                                    if(i != 0 && j != c)
                                    {
                                            b[m][n] = a[i][j];
                                            if (n < (k - 2))
                                                n++;
                                            else
                                            {
                                                    n = 0;
                                                    m++;
                                            }
                                    }
                            }
                    }
 
                    det = det + s * ( a[0][c] * comp_detrm(b, k - 1));
                    s = -1 * s;
                }
        }
 
    return det;
}

MATRIX trans_inv(MATRIX num, MATRIX fac, int dim)
{
    int i, j;
    MATRIX b, inv;
    b = create_matrix(dim, dim, -1);
    inv = create_matrix(dim, dim, -1);

    float d;
 
    for (i=0; i<dim; i++)
    {
            for (j=0; j<dim; j++)
            {
                    b[i][j] = fac[j][i];
            }
    }

    d = comp_detrm(num, dim);

    for (i=0; i<dim; i++)
    {
            for (j=0; j<dim; j++)
            {
                    inv[i][j] = b[i][j] / d;
            }
    }

    return inv;
}

MATRIX comp_inv(MATRIX num, int dim)
{
    MATRIX b, fac;
    b = create_matrix(dim, dim, -1);
    fac = create_matrix(dim, dim, -1);

    int p, q, m, n, j, i;
 
    for(q=0; q<dim; q++)
    {
            for(p=0; p<dim; p++)
            {
                    m = 0;
                    n = 0;
 
                    for(i=0; i<dim; i++)
                    {
                            for(j=0; j<dim; j++)
                            {
                                    b[i][j] = 0;
 
                                    if (i!=q && j!=p)
                                    {
                                            b[m][n] = num[i][j];
 
                                            if (n < (dim - 2))
                                                n++;
                                            else
                                            {
                                                    n = 0;
                                                    m++;
                                            }
                                    }
                           }
                    }

                    fac[q][p] = pow(-1, q+p) * comp_detrm(b, dim-1);

            }
    }

    return trans_inv(num, fac, dim);
}


/* training function - follows the pseudocode presented in the homework */
MATRIX extract_lda()
{
	float *mean_tigre, *mean_eleph;
	int i;

	// step 1 from the pseudocode - computing the means for images in each class
	mean_tigre = comp_mean(MODEL_TIGER);
	mean_eleph = comp_mean(MODEL_ELEPH);

	// step 2&3 - compute Sw
	// Sw is Sw1+Sw2 so we compute both in one loop

	MATRIX Sw = create_matrix(M,M,0);
	for(i=0; i<NR_IMAGES; i++)
	{
		MATRIX matrix1, matrix2;

		matrix1 = comp_diff(i, MODEL_TIGER, mean_tigre);
		matrix2 = comp_trans(matrix1, 1, M);

		// for tigres
		Sw = comp_add(Sw, comp_mul(matrix1, 1, M, matrix2, M, 1), M, M);

		matrix1 = comp_diff(i, MODEL_ELEPH, mean_eleph);
		matrix2 = comp_trans(matrix1, 1, M);

		// for elephants
		Sw = comp_add(Sw, comp_mul(matrix1, 1, M, matrix2, M, 1), M, M);
	}

	print_mat(Sw,M,M);

	printf("Sw computed\n");
	float detSw = comp_detrm(Sw, M);
	printf(" - with determinant %f\n",detSw); 

	//step 4 - comput the inverse of Sw

	if(detSw==0) //the matrix cannot have an inverse
		return NULL;

	MATRIX Swinv;
	Swinv = comp_inv(Sw, M);

	print_mat(Swinv,M,M);
	printf("Sw^-1 computed\n");

	// step 5 - compute W
	MATRIX matrix1 = comp_mean_diff(mean_tigre, mean_eleph);
	MATRIX w = comp_mul(Swinv, M, M, matrix1, 1, M);

	print_mat(w,M,1);
	printf("Transformation matrix W computed\n");

	return w;
}

/*apply the W matrix on the image and return the projection value */
float get_projection(image img, MATRIX w)
{
	int i;
	MATRIX aux= create_matrix(1,M,-1);
	for(i=0;i<M;i++)
	{
		aux[0][i] = img->buf[i];
	}

	MATRIX val = comp_mul(comp_trans(w, 1, M), M, 1, aux, 1, M);

	return val[0][0];
}


int main(void)
{
    unsigned int i;
   
    /*read all the existing images*/
    add_elements(1);
    add_elements(2);

    printf("--->LDA Training Model<---\n");
    MATRIX w = extract_lda();

    if(w==NULL)
    {
	printf("Could not extract the model\n");
	return -1;
    }

    // step 7 - get the mean and standard deviation for the two classes
    float mean_proj_tigres=0;
    float mean_proj_eleph=0, val;
    float sd_proj_eleph=0, sd_proj_tigres=0;
    float val_tigres[NR_IMAGES], val_eleph[NR_IMAGES];

    for(i=0; i<NR_IMAGES; i++)
    {
	val = get_projection(tiger_images[i], w);
	mean_proj_tigres += val;
	val_tigres[i] = val;

	val = get_projection(eleph_images[i], w);
	mean_proj_eleph += val;
	val_eleph[i] = val;
    }

    mean_proj_tigres = mean_proj_tigres / NR_IMAGES;
    mean_proj_eleph = mean_proj_eleph / NR_IMAGES;

   for(i=0;i<NR_IMAGES;i++)
   {
	sd_proj_tigres += (val_tigres[i]-mean_proj_tigres)*(val_tigres[i]-mean_proj_tigres);
	sd_proj_eleph += (val_eleph[i]-mean_proj_eleph)*(val_eleph[i]-mean_proj_eleph);	
   }

   sd_proj_tigres = sqrt(sd_proj_tigres/NR_IMAGES);
   sd_proj_eleph = sqrt(sd_proj_eleph/NR_IMAGES);

   printf("Tigres - mean: %f - sd: %f\n",mean_proj_tigres, sd_proj_tigres);
   printf("Elephants - mean: %f - sd: %f\n",mean_proj_eleph, sd_proj_eleph);

    printf("--->Classification<---\n");
    //test images
    srand ( time(NULL) );
    for (i = 0; i < NR_TEST; i++)
    {
        test_images = get_random();

	printf("------------------\n(%f %f)\n",test_images->buf[0], test_images->buf[1]);
	float val = get_projection(test_images, w);

	printf("Projection: %f\n",val);

	int class_tigru=0, class_eleph=0;
	if(val<mean_proj_tigres+2*sd_proj_tigres && val>mean_proj_tigres-2*sd_proj_tigres)
		class_tigru++;
	if(val<mean_proj_eleph+2*sd_proj_eleph && val>mean_proj_eleph-2*sd_proj_eleph)
		class_eleph++;

	if(class_tigru==0 && class_eleph==0)
	{
		printf("Image %d - Not classified\n",i);
		continue;
	}

	if(class_tigru==1 && class_eleph==0)
	{
		printf("Image %d - Classified as tigre\n",i);
		continue;
	}

	if(class_tigru==0 && class_eleph==1)
	{
		printf("Image %d - Classified as elephant\n",i);
		continue;
	}
	
	//if both class tigru and elephant are 1 we check the distance to the means
	if(abs(val-mean_proj_tigres)<abs(val-mean_proj_eleph))
	{
		printf("Image %d - Classified as tigre\n",i);
		continue;
	}

	if(abs(val-mean_proj_tigres)>abs(val-mean_proj_eleph))
	{
		printf("Image %d - Classified as elephant\n",i);
		continue;
	}

	printf("Image %d - Not classified\n",i);
    }


    return 1;
}
