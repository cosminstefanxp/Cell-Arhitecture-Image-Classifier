/* --------------------------------------------------------------  */
/* (C)Copyright 2008                                               */
/* International Business Machines Corporation                     */
/* All Rights Reserved.                                            */
/*                                                                 */
/* Redistribution and use in source and binary forms, with or      */
/* without modification, are permitted provided that the           */
/* following conditions are met:                                   */
/*                                                                 */
/* - Redistributions of source code must retain the above copyright*/
/*   notice, this list of conditions and the following disclaimer. */
/*                                                                 */
/* - Redistributions in binary form must reproduce the above       */
/*   copyright notice, this list of conditions and the following   */
/*   disclaimer in the documentation and/or other materials        */
/*   provided with the distribution.                               */
/*                                                                 */
/* - Neither the name of IBM Corporation nor the names of its      */
/*   contributors may be used to endorse or promote products       */
/*   derived from this software without specific prior written     */
/*   permission.                                                   */
/*                                                                 */
/* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND          */
/* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,     */
/* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF        */
/* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE        */
/* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR            */
/* CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,    */
/* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT    */
/* NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;    */
/* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)        */
/* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN       */
/* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR    */
/* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  */
/* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.              */
/* --------------------------------------------------------------  */
/* PROLOG END TAG zYx                                              */
#include <stdlib.h>
#include <stdio.h>
#include "lapack.h"

int main( int argc, char *argv[] ) 
{
    int info = 0; 
    double *a;    
    int *ipiv;
    int n;
    int i,j;
    
    if( argc < 2 )
    {
        fprintf(stderr, "%s N\n", argv[0]);
        return 0;
    }
    n = atoi(argv[1]);

    posix_memalign((void **)((void*)(&ipiv)), 128, sizeof(int)*n);
    posix_memalign((void **)((void*)(&a)), 128, sizeof(double)*n*n);
    if( a == NULL || ipiv == NULL )
    {
        fprintf(stderr, "a/ipiv malloc error\n");
        return 0;
    }
    
    for( i = 0; i < n; i++ )
    {
        for( j = 0; j < n; j++ )
        {
            a[i+j*n]= (drand48() - 0.5f)*4; 
        }
    }
   for( i = 0; i < n; i++ )
    {
        for( j = 0; j < n; j++ )
        {
            printf("%2.1f ",a[i+j*n]);
        }
	printf("\n");
    } 

    /*---------Call Cell LAPACK library---------*/
    dgetrf_(&n, &n, a, &n, ipiv, &info);
    if( info != 0 )
    {
        fprintf(stderr, "Call dgetrf error\n");
        goto end;
    }
   printf("getrf_ \n");
   for( i = 0; i < n; i++ )
    {
        for( j = 0; j < n; j++ )
        {
            printf("%2.1f ",a[i+j*n]);
        }
	printf("\n");
    } 
    /*---------Query workspace-------*/
    double workspace;
    int tmp=-1;
    int lwork;
    double *work;
    dgetri_(&n, a, &n, ipiv, &workspace, &tmp, &info);
    lwork = (int)workspace;
    work = malloc(sizeof(double)*lwork);
    if(work == NULL)
    {
        printf("work malloc error\n");
        goto end;
    }
    printf("getri_ (1)\n");
   for( i = 0; i < n; i++ )
    {
        for( j = 0; j < n; j++ )
        {
            printf("%2.1f ",a[i+j*n]);
        }
	printf("\n");
    } 
    /*---------Call Cell LAPACK library---------*/
    dgetri_(&n, a, &n, ipiv, work, &lwork, &info); 
    if( info != 0 )
    {
        fprintf(stderr, "Call dgetri error\n");
        free(work);
        goto end;
    }
    printf("Inverse matrix completed!\n");

   printf("getri_ (2)\n");
   for( i = 0; i < n; i++ )
    {
        for( j = 0; j < n; j++ )
        {
            printf("%2.1f ",a[i+j*n]);
        }
	printf("\n");
    } 
end:        
    free(ipiv);
    free(a);
    return 0;
}

