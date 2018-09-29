#ifndef GDAL_CEM_H_INCLUDED
#define GDAL_CEM_H_INCLUDED

/**
* \file gdal_alg_ext.h
*
* Public (C callable) GDAL CEM & TIN algorithm entry points, and definitions.
*/

#ifndef DOXYGEN_SKIP
#include "gdal_alg.h"
#endif

CPL_C_START

//Collinearity Equation Model
typedef struct {
	// 径向畸变参数
	double		dfK1;
	double		dfK2;
	double		dfK3;
	// 切向畸变参数
	double		dfP1;
	double		dfP2;
	// 像素的非正方形比例因子
	double		dfAlpha;
	// CCD阵列排列非正交性误差系数
	double		dfBeta;

	// 内方位元素(单位 mm)
	double      dfFocalLength;	// 焦距
	double      dfX0;			// 像主点 x0
	double      dfY0;			// 像主点 y0
	double		dfXPS;			// x pixel size
	double		dfYPS;			// y pixel size

	// 外方位元素(单位 m & rad)
	double      dfXS;		   // 摄影中心坐标X
	double      dfYS;		   // 摄影中心坐标Y
	double      dfZS;		   // 摄影中心坐标Z

	double      adfOmega[3];   // 旋转角度Omega
	double      adfPhi[3];	   // 旋转角度Phi
	double      adfKappa[3];   // 旋转角度Kappa

	int			nAngleType;	   // 转角类型,0为Omega、Phi、Kappa
	int			nAngleOrder;   // 转角次数
} GDALCEMInfo;

int CPL_DLL CPL_STDCALL GDALExtractCEMInfo(char **, GDALCEMInfo *);

/* CEM based transformer ... src is pixel/line/elev, dst is mapx/mapy/elev */
void CPL_DLL *GDALCreateCEMTransformer(
	GDALCEMInfo *psCEM, int bReversed,
	double dfPixErrThreshold, char **papszOptions);

void CPL_DLL GDALDestroyCEMTransformer(void *pTransformArg);

int CPL_DLL GDALCEMTransform(
	void *pTransformArg, int bDstToSrc, int nPointCount,
	double *x, double *y, double *z, int *panSuccess);


/* Triangular Irregular Network transformer ... forward is to georef coordinates */

void* GDALCreateTINTransformerInt(
	int nGCPCount, const GDAL_GCP *pasGCPList,
	int bReversed, char** papszOptions);

void CPL_DLL *GDALCreateTINTransformer(
	int nGCPCount, const GDAL_GCP *pasGCPList, int bReversed);

void CPL_DLL GDALDestroyTINTransformer(void *pTransformArg);

int CPL_DLL GDALTINTransform(
	void *pTransformArg, int bDstToSrc, int nPointCount,
	double *x, double *y, double *z, int *panSuccess);


void GDALApplyGeoTransform2(double *padfGeoTransform,
    double dfPixel, double dfLine,
    double *pdfGeoX, double *pdfGeoY);

int GDALInvGeoTransform2(double *padfIn, double *padfOut);


CPL_C_END

#endif /* endif GDAL_CEM_H_INCLUDED */
