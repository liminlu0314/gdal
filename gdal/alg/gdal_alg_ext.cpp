#include "gdal_alg_ext.h"
#include "cpl_conv.h"

//from gdal_crs.c
struct Control_Points1
{
    int  count;
    double *e1;
    double *n1;
    double *e2;
    double *n2;
    int *status;
};

#define MSUCCESS     1 /* SUCCESS */
#define MNPTERR      0 /* NOT ENOUGH POINTS */
#define MUNSOLVABLE -1 /* NOT SOLVABLE */
#define MMEMERR     -2 /* NOT ENOUGH MEMORY */
#define MPARMERR    -3 /* PARAMETER ERROR */
#define MINTERR     -4 /* INTERNAL ERROR */

/************************************************************************/
/* ==================================================================== */
/*      Everything below this point derived from the CRS.C from GRASS.  */
/* ==================================================================== */
/************************************************************************/


/* STRUCTURE FOR USE INTERNALLY WITH THESE FUNCTIONS.  THESE FUNCTIONS EXPECT
   SQUARE MATRICES SO ONLY ONE VARIABLE IS GIVEN (N) FOR THE MATRIX SIZE */

struct MATRIX
{
    int     n;     /* SIZE OF THIS MATRIX (N x N) */
    double *v;
};

/* CALCULATE OFFSET INTO ARRAY BASED ON R/C */

#define M(row,col) m->v[(((row)-1)*(m->n))+(col)-1]

/***************************************************************************/
/*
    FUNCTION PROTOTYPES FOR STATIC (INTERNAL) FUNCTIONS
*/
/***************************************************************************/

static int calccoef(struct Control_Points1 *, double, double, double *, double *, int);
static int calcls(struct Control_Points1 *, struct MATRIX *, double, double,
    double *, double *, double *, double *);
static int exactdet(struct Control_Points1 *, struct MATRIX *, double, double,
    double *, double *, double *, double *);
static int solvemat(struct MATRIX *, double *, double *, double *, double *);
static double term(int, double, double);

/***************************************************************************/
/*
    COMPUTE THE GEOREFFERENCING COEFFICIENTS BASED ON A SET OF CONTROL POINTS
*/
/***************************************************************************/

static int
calccoef(struct Control_Points1 *cp, double x_mean, double y_mean, double E[], double N[], int order)
{
    struct MATRIX m;
    double *a = NULL;
    double *b = NULL;
    int numactive = 0;   /* NUMBER OF ACTIVE CONTROL POINTS */
    int status = 0;
    int i = 0;

    memset(&m, 0, sizeof(m));

    /* CALCULATE THE NUMBER OF VALID CONTROL POINTS */

    for (i = numactive = 0; i < cp->count; i++)
    {
        if (cp->status[i] > 0)
            numactive++;
    }

    /* CALCULATE THE MINIMUM NUMBER OF CONTROL POINTS NEEDED TO DETERMINE
       A TRANSFORMATION OF THIS ORDER */

    m.n = ((order + 1) * (order + 2)) / 2;

    if (numactive < m.n)
        return(MNPTERR);

    /* INITIALIZE MATRIX */

    m.v = (double *)CPLCalloc(m.n*m.n, sizeof(double));
    if (m.v == NULL)
    {
        return(MMEMERR);
    }
    a = (double *)CPLCalloc(m.n, sizeof(double));
    if (a == NULL)
    {
        CPLFree((char *)m.v);
        return(MMEMERR);
    }
    b = (double *)CPLCalloc(m.n, sizeof(double));
    if (b == NULL)
    {
        CPLFree((char *)m.v);
        CPLFree((char *)a);
        return(MMEMERR);
    }

    if (numactive == m.n)
        status = exactdet(cp, &m, x_mean, y_mean, a, b, E, N);
    else
        status = calcls(cp, &m, x_mean, y_mean, a, b, E, N);

    CPLFree((char *)m.v);
    CPLFree((char *)a);
    CPLFree((char *)b);

    return(status);
}

/***************************************************************************/
/*
    CALCULATE THE TRANSFORMATION COEFFICIENTS WITH EXACTLY THE MINIMUM
    NUMBER OF CONTROL POINTS REQUIRED FOR THIS TRANSFORMATION.
*/
/***************************************************************************/

static int exactdet(
    struct Control_Points1 *cp,
    struct MATRIX *m,
    double x_mean,
    double y_mean,
    double a[],
    double b[],
    double E[],     /* EASTING COEFFICIENTS */
    double N[]     /* NORTHING COEFFICIENTS */
)
{
    int pntnow = 0;
    int currow = 1;
    int j = 0;

    for (pntnow = 0; pntnow < cp->count; pntnow++)
    {
        if (cp->status[pntnow] > 0)
        {
            /* POPULATE MATRIX M */

            for (j = 1; j <= m->n; j++)
            {
                M(currow, j) = term(j, cp->e1[pntnow] - x_mean, cp->n1[pntnow] - y_mean);
            }

            /* POPULATE MATRIX A AND B */

            a[currow - 1] = cp->e2[pntnow];
            b[currow - 1] = cp->n2[pntnow];

            currow++;
        }
    }

    if (currow - 1 != m->n)
        return(MINTERR);

    return(solvemat(m, a, b, E, N));
}

/***************************************************************************/
/*
    CALCULATE THE TRANSFORMATION COEFFICIENTS WITH MORE THAN THE MINIMUM
    NUMBER OF CONTROL POINTS REQUIRED FOR THIS TRANSFORMATION.  THIS
    ROUTINE USES THE LEAST SQUARES METHOD TO COMPUTE THE COEFFICIENTS.
*/
/***************************************************************************/

static int calcls(
    struct Control_Points1 *cp,
    struct MATRIX *m,
    double x_mean,
    double y_mean,
    double a[],
    double b[],
    double E[],     /* EASTING COEFFICIENTS */
    double N[]     /* NORTHING COEFFICIENTS */
)
{
    int i = 0, j = 0, n = 0, numactive = 0;

    /* INITIALIZE THE UPPER HALF OF THE MATRIX AND THE TWO COLUMN VECTORS */

    for (i = 1; i <= m->n; i++)
    {
        for (j = i; j <= m->n; j++)
            M(i, j) = 0.0;
        a[i - 1] = b[i - 1] = 0.0;
    }

    /* SUM THE UPPER HALF OF THE MATRIX AND THE COLUMN VECTORS ACCORDING TO
       THE LEAST SQUARES METHOD OF SOLVING OVER DETERMINED SYSTEMS */

    for (n = 0; n < cp->count; n++)
    {
        if (cp->status[n] > 0)
        {
            numactive++;
            for (i = 1; i <= m->n; i++)
            {
                for (j = i; j <= m->n; j++)
                    M(i, j) += term(i, cp->e1[n] - x_mean, cp->n1[n] - y_mean) * term(j, cp->e1[n] - x_mean, cp->n1[n] - y_mean);

                a[i - 1] += cp->e2[n] * term(i, cp->e1[n] - x_mean, cp->n1[n] - y_mean);
                b[i - 1] += cp->n2[n] * term(i, cp->e1[n] - x_mean, cp->n1[n] - y_mean);
            }
        }
    }

    if (numactive <= m->n)
        return(MINTERR);

    /* TRANSPOSE VALUES IN UPPER HALF OF M TO OTHER HALF */

    for (i = 2; i <= m->n; i++)
    {
        for (j = 1; j < i; j++)
            M(i, j) = M(j, i);
    }

    return(solvemat(m, a, b, E, N));
}

/***************************************************************************/
/*
    CALCULATE THE X/Y TERM BASED ON THE TERM NUMBER

ORDER\TERM   1    2    3    4    5    6    7    8    9   10
  1        e0n0 e1n0 e0n1
  2        e0n0 e1n0 e0n1 e2n0 e1n1 e0n2
  3        e0n0 e1n0 e0n1 e2n0 e1n1 e0n2 e3n0 e2n1 e1n2 e0n3
*/
/***************************************************************************/

static double term(int nTerm, double e, double n)
{
    switch (nTerm)
    {
    case  1: return((double)1.0);
    case  2: return((double)e);
    case  3: return((double)n);
    case  4: return((double)(e*e));
    case  5: return((double)(e*n));
    case  6: return((double)(n*n));
    case  7: return((double)(e*e*e));
    case  8: return((double)(e*e*n));
    case  9: return((double)(e*n*n));
    case 10: return((double)(n*n*n));
    }
    return((double)0.0);
}

/***************************************************************************/
/*
    SOLVE FOR THE 'E' AND 'N' COEFFICIENTS BY USING A SOMEWHAT MODIFIED
    GAUSSIAN ELIMINATION METHOD.

    | M11 M12 ... M1n | | E0   |   | a0   |
    | M21 M22 ... M2n | | E1   | = | a1   |
    |  .   .   .   .  | | .    |   | .    |
    | Mn1 Mn2 ... Mnn | | En-1 |   | an-1 |

    and

    | M11 M12 ... M1n | | N0   |   | b0   |
    | M21 M22 ... M2n | | N1   | = | b1   |
    |  .   .   .   .  | | .    |   | .    |
    | Mn1 Mn2 ... Mnn | | Nn-1 |   | bn-1 |
*/
/***************************************************************************/

static int solvemat(struct MATRIX *m,
    double a[], double b[], double E[], double N[])
{
    int i = 0;
    int j = 0;
    int i2 = 0;
    int j2 = 0;
    int imark = 0;
    double factor = 0.0;
    double temp = 0.0;
    double pivot = 0.0;  /* ACTUAL VALUE OF THE LARGEST PIVOT CANDIDATE */

    for (i = 1; i <= m->n; i++)
    {
        j = i;

        /* find row with largest magnitude value for pivot value */

        pivot = M(i, j);
        imark = i;
        for (i2 = i + 1; i2 <= m->n; i2++)
        {
            temp = fabs(M(i2, j));
            if (temp > fabs(pivot))
            {
                pivot = M(i2, j);
                imark = i2;
            }
        }

        /* if the pivot is very small then the points are nearly co-linear */
        /* co-linear points result in an undefined matrix, and nearly */
        /* co-linear points results in a solution with rounding error */

        if (pivot == 0.0)
            return(MUNSOLVABLE);

        /* if row with highest pivot is not the current row, switch them */

        if (imark != i)
        {
            for (j2 = 1; j2 <= m->n; j2++)
            {
                temp = M(imark, j2);
                M(imark, j2) = M(i, j2);
                M(i, j2) = temp;
            }

            temp = a[imark - 1];
            a[imark - 1] = a[i - 1];
            a[i - 1] = temp;

            temp = b[imark - 1];
            b[imark - 1] = b[i - 1];
            b[i - 1] = temp;
        }

        /* compute zeros above and below the pivot, and compute
           values for the rest of the row as well */

        for (i2 = 1; i2 <= m->n; i2++)
        {
            if (i2 != i)
            {
                factor = M(i2, j) / pivot;
                for (j2 = j; j2 <= m->n; j2++)
                    M(i2, j2) -= factor * M(i, j2);
                a[i2 - 1] -= factor * a[i - 1];
                b[i2 - 1] -= factor * b[i - 1];
            }
        }
    }

    /* SINCE ALL OTHER VALUES IN THE MATRIX ARE ZERO NOW, CALCULATE THE
       COEFFICIENTS BY DIVIDING THE COLUMN VECTORS BY THE DIAGONAL VALUES. */

    for (i = 1; i <= m->n; i++)
    {
        E[i - 1] = a[i - 1] / M(i, i);
        N[i - 1] = b[i - 1] / M(i, i);
    }

    return(MSUCCESS);
}

void GDALApplyGeoTransform2(double *padfGeoTransform,
    double dfPixel, double dfLine,
    double *pdfGeoX, double *pdfGeoY)
{
    *pdfGeoX = padfGeoTransform[0] + dfPixel * padfGeoTransform[1]
                                   + dfLine * padfGeoTransform[2]
                                   + dfPixel * dfPixel * padfGeoTransform[6]
                                   + dfLine * dfLine * padfGeoTransform[7]
                                   + dfPixel * dfLine *  padfGeoTransform[8];
    *pdfGeoY = padfGeoTransform[3] + dfPixel * padfGeoTransform[4]
                                   + dfLine * padfGeoTransform[5]
                                   + dfPixel * dfPixel * padfGeoTransform[9]
                                   + dfLine * dfLine * padfGeoTransform[10]
                                   + dfPixel * dfLine * padfGeoTransform[11];
}

int GDALInvGeoTransform2(double *padfIn, double *padfOut)
{
    //构造格网点再重新求解 100*100
    int nGCPCount = 10000;
    double dfSrcX[10000];
    double dfSrcY[10000];
    double dfDstX[10000];
    double dfDstY[10000];
    int panStatus[10000];

    //格网范围-500~500
    for (int x = 0; x < 100; x++)
    {
        double dfx = -500 + x * 10;
        for (int y = 0; y < 100; y++)
        {
            double dfy = -500 + y * 10;

            int idx = x * 100 + y;
            dfSrcX[idx] = dfx;
            dfSrcY[idx] = dfy;

            GDALApplyGeoTransform2(padfIn, dfx, dfy, dfDstX + idx, dfDstY + idx);
            panStatus[idx] = 1;
        }
    }

    Control_Points1 sPoints;
    sPoints.count = nGCPCount;
    sPoints.e1 = dfDstX;
    sPoints.n1 = dfDstY;
    sPoints.e2 = dfSrcX;
    sPoints.n2 = dfSrcY;
    sPoints.status = panStatus;

    //最小二乘求解新的系数
    double adfToGeoX[20] = { 0 };
    double adfToGeoY[20] = { 0 };
    calccoef(&sPoints, 0, 0, adfToGeoX, adfToGeoY, 2);

    padfOut[0] = adfToGeoX[0];  //1
    padfOut[1] = adfToGeoX[1];  //x
    padfOut[2] = adfToGeoX[2];  //y
    padfOut[6] = adfToGeoX[3];  //xx
    padfOut[7] = adfToGeoX[5];  //yy
    padfOut[8] = adfToGeoX[4];  //xy

    padfOut[3] = adfToGeoY[0];  //1
    padfOut[4] = adfToGeoY[1];  //x
    padfOut[5] = adfToGeoY[2];  //y
    padfOut[9] = adfToGeoY[3];  //xx
    padfOut[10] = adfToGeoY[5]; //yy
    padfOut[11] = adfToGeoY[4]; //xy

    return TRUE;
}

