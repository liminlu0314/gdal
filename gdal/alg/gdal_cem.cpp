/******************************************************************************
 * $Id$
 *
 * Project:  Image Warper
 * Purpose:  Implements a collinearity equation model (CEM) based transformer.
 * Author:   Minlu Li, liminlu0314@163.com
 *
 ******************************************************************************
 * Copyright (c) 2014, Minlu Li <liminlu0314@163.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#include "gdal_priv.h"
#include "gdal_alg_ext.h"
#include "ogr_spatialref.h"
#include "cpl_minixml.h"

CPL_C_START
CPLXMLNode *GDALSerializeCEMTransformer(void *pTransformArg);
void *GDALDeserializeCMETransformer(CPLXMLNode *psTree);
CPL_C_END

/************************************************************************/
/*                          _FetchDblFromMD()                           */
/************************************************************************/

static bool FetchDblFromMD(char **papszMD, const char *pszKey,
double *padfTarget, int nCount, double dfDefault)

{
	char szFullKey[200];

	snprintf(szFullKey, sizeof(szFullKey), "%s", pszKey);

	const char *pszValue = CSLFetchNameValue(papszMD, szFullKey);

	for (int i = 0; i < nCount; i++)
		padfTarget[i] = dfDefault;

	if (pszValue == NULL)
		return false;

	if (nCount == 1)
	{
		*padfTarget = CPLAtofM(pszValue);
		return true;
	}

	char **papszTokens = CSLTokenizeStringComplex(pszValue, " ,",
		FALSE, FALSE);

	if (CSLCount(papszTokens) != nCount)
	{
		CSLDestroy(papszTokens);
		return false;
	}

	for (int i = 0; i < nCount; i++)
		padfTarget[i] = CPLAtofM(papszTokens[i]);

	CSLDestroy(papszTokens);

	return true;
}

/************************************************************************/
/*                         GDALExtractCEMInfo()                         */
/************************************************************************/

/** Extract CEM info from metadata, and apply to an CEMInfo structure.
*
* The inverse of this function is CEMInfoToMD() in alg/gdal_cem.cpp
*
* @param papszMD Dictionary of metadata representing CEM
* @param psCEM (output) Pointer to structure to hold the CEM values.
* @return TRUE in case of success. FALSE in case of failure.
*/
int CPL_STDCALL GDALExtractCEMInfo(char **papszMD, GDALCEMInfo *psCEM)
{
	if (CSLFetchNameValue(papszMD, "CEM_FOCAL_LENGTH") == NULL)
		return FALSE;

	if (CSLFetchNameValue(papszMD, "CEM_FOCAL_LENGTH") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_PRINCIPAL_X0") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_PRINCIPAL_Y0") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_PXIEL_XSIZE") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_PXIEL_YSIZE") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_DISTORTION_K1") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_DISTORTION_K2") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_DISTORTION_K3") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_DISTORTION_P1") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_DISTORTION_P2") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_DISTORTION_ALPHA") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_DISTORTION_BETA") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_EXTERIOR_XS") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_EXTERIOR_YS") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_EXTERIOR_ZS") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_EXTERIOR_OMEGA") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_EXTERIOR_PHI") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_EXTERIOR_KAPPA") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_ANGLE_TYPE") == NULL
		|| CSLFetchNameValue(papszMD, "CEM_ANGLE_ORDER") == NULL)
	{
		CPLError(CE_Failure, CPLE_AppDefined,
			"Some required CEM metadata missing in GDALExtractCEMInfo()");
		return FALSE;
	}

	FetchDblFromMD(papszMD, "CEM_DISTORTION_K1", &(psCEM->dfK1), 1, 0.0);
	FetchDblFromMD(papszMD, "CEM_DISTORTION_K2", &(psCEM->dfK2), 1, 0.0);
	FetchDblFromMD(papszMD, "CEM_DISTORTION_K3", &(psCEM->dfK3), 1, 0.0);
	FetchDblFromMD(papszMD, "CEM_DISTORTION_P1", &(psCEM->dfP1), 1, 0.0);
	FetchDblFromMD(papszMD, "CEM_DISTORTION_P2", &(psCEM->dfP2), 1, 0.0);
	FetchDblFromMD(papszMD, "CEM_DISTORTION_ALPHA", &(psCEM->dfAlpha), 1, 0.0);
	FetchDblFromMD(papszMD, "CEM_DISTORTION_BETA", &(psCEM->dfBeta), 1, 0.0);

	FetchDblFromMD(papszMD, "CEM_FOCAL_LENGTH", &(psCEM->dfFocalLength), 1, 0.0);
	FetchDblFromMD(papszMD, "CEM_PRINCIPAL_X0", &(psCEM->dfX0), 1, 0.0);
	FetchDblFromMD(papszMD, "CEM_PRINCIPAL_Y0", &(psCEM->dfY0), 1, 0.0);
	FetchDblFromMD(papszMD, "CEM_PXIEL_XSIZE", &(psCEM->dfXPS), 1, 0.0);
	FetchDblFromMD(papszMD, "CEM_PXIEL_YSIZE", &(psCEM->dfYPS), 1, 0.0);

	FetchDblFromMD(papszMD, "CEM_EXTERIOR_XS", &(psCEM->dfXS), 1, 0.0);
	FetchDblFromMD(papszMD, "CEM_EXTERIOR_YS", &(psCEM->dfYS), 1, 0.0);
	FetchDblFromMD(papszMD, "CEM_EXTERIOR_ZS", &(psCEM->dfZS), 1, 0.0);

	FetchDblFromMD(papszMD, "CEM_EXTERIOR_OMEGA", psCEM->adfOmega, 3, 0.0);
	FetchDblFromMD(papszMD, "CEM_EXTERIOR_PHI", psCEM->adfPhi, 3, 0.0);
	FetchDblFromMD(papszMD, "CEM_EXTERIOR_KAPPA", psCEM->adfKappa, 3, 0.0);

	const char* pszAngleType = CSLFetchNameValue(papszMD, "CEM_ANGLE_TYPE");
	const char* pszAngleOrder = CSLFetchNameValue(papszMD, "CEM_ANGLE_ORDER");
	psCEM->nAngleType = atoi(pszAngleType);
	psCEM->nAngleOrder = atoi(pszAngleOrder);

	return TRUE;
}

/************************************************************************/
/*                         CEMTransformPoint()                          */
/************************************************************************/

static void CEMTransformPoint(GDALCEMInfo *psCEM,
	double dfMapX, double dfMapY, double dfHeight,
	double *pdfPixel, double *pdfLine)
{
	double adfPhi[3], adfOmega[3], adfKappa[3]; //外方位元素
	memcpy(adfOmega, psCEM->adfOmega, sizeof(double) * 3);
	memcpy(adfPhi, psCEM->adfPhi, sizeof(double) * 3);
	memcpy(adfKappa, psCEM->adfKappa, sizeof(double) * 3);

	double Omega = adfOmega[0];
	double Phi = adfPhi[0];
	double Kappa = adfKappa[0];

	double a11 = cos(Phi)*cos(Kappa) - sin(Phi)*sin(Omega)*sin(Kappa);
	double a12 = -(cos(Phi)*sin(Kappa)) - (sin(Phi)*sin(Omega)*cos(Kappa));
	double a13 = -(sin(Phi)*cos(Omega));
	double a21 = cos(Omega)*sin(Kappa);
	double a22 = cos(Omega)*cos(Kappa);
	double a23 = -sin(Omega);
	double a31 = sin(Phi)*cos(Kappa) + cos(Phi)*sin(Omega)*sin(Kappa);
	double a32 = -(sin(Phi)*sin(Kappa)) + cos(Phi)*sin(Omega)*cos(Kappa);
	double a33 = cos(Phi)*cos(Omega);

	double dfXS = psCEM->dfXS;
	double dfYS = psCEM->dfYS;
	double dfZS = psCEM->dfZS;

	double dfFL = psCEM->dfFocalLength;

	double dTemp = a31*dfMapX + a32*dfMapY - a33*dfFL;

	*pdfPixel = (-dfFL)*((a11*(dfMapX - dfXS) + a21*(dfMapY - dfYS) + a31*(dfHeight - dfZS)) / dTemp);
	*pdfLine = (-dfFL)*((a12*(dfMapX - dfXS) + a22*(dfMapY - dfYS) + a32*(dfHeight - dfZS)) / dTemp);
}

/************************************************************************/
/* ==================================================================== */
/*			               GDALCEMTransformer                           */
/* ==================================================================== */
/************************************************************************/

/*! DEM Resampling Algorithm */
typedef enum {
	/*! Nearest neighbour (select on one input pixel) */ DRA_NearestNeighbour = 0,
	/*! Bilinear (2x2 kernel) */                         DRA_Bilinear = 1,
	/*! Cubic Convolution Approximation (4x4 kernel) */  DRA_Cubic = 2
} DEMResampleAlg;

typedef struct {

	GDALTransformerInfo sTI;

	GDALCEMInfo sCEM;

	double      adfPLToLatLongGeoTransform[6];

	int         bReversed;

	double      dfPixErrThreshold;

	double      dfHeightOffset;

	double      dfHeightScale;

	char        *pszDEMPath;

	DEMResampleAlg eResampleAlg;

	int         bHasTriedOpeningDS;
	GDALDataset *poDS;

	OGRCoordinateTransformation *poCT;

	double      adfGeoTransform[6];
	double      adfReverseGeoTransform[6];
} GDALCEMTransformInfo;

/************************************************************************/
/*                      GDALCreateCEMTransformer()                      */
/************************************************************************/

/**
 * Create an Collinearity Equation Model (CEM) based transformer.
 *
 * The geometric sensor model describing the physical relationship between
 * image coordinates and ground coordinate is known as a Rigorous Projection
 * Model. A Rigorous Projection Model expresses the mapping of the image space
 * coordinates of rows and columns (r,c) onto the object space reference
 * surface geodetic coordinates (mapx, mapy, height).
 *
 * This function creates a GDALTransformFunc compatible transformer
 * for going between image pixel/line and mapx/mapy/height coordinates
 * using CEMs.  The CEMs are provided in a GDALCEMInfo structure which is
 * normally read from metadata using GDALExtractCEMInfo().
 *
 * GDAL CEM Metadata has the following entries
 *
 * <ul>
 * <li>DISTORTION_K1: 径向畸变参数K1
 * <li>DISTORTION_K2: 径向畸变参数K2
 * <li>DISTORTION_K3: 径向畸变参数K3
 * <li>DISTORTION_P1: 切向畸变参数P1
 * <li>DISTORTION_P2: 切向畸变参数P2
 * <li>DISTORTION_ALPHA: 像素的非正方形比例因子
 * <li>DISTORTION_BETA: CCD阵列排列非正交性误差系数
 * <li>FOCAL_LENGTH: 焦距
 * <li>PRINCIPAL_X0: 像主点 x0
 * <li>PRINCIPAL_Y0: 像主点 y0
 * <li>PXIEL_XSIZE: x pixel size
 * <li>PXIEL_YSIZE: y pixel size
 * <li>EXTERIOR_XS: 摄影中心坐标X
 * <li>EXTERIOR_YS: 摄影中心坐标Y
 * <li>EXTERIOR_ZS: 摄影中心坐标Z
 * <li>EXTERIOR_OMEGA: 旋转角度Omega(space separated)
 * <li>EXTERIOR_PHI:   旋转角度Phi(space separated)
 * <li>EXTERIOR_KAPPA: 旋转角度Kappa(space separated)
 * <li>ANGLE_TYPE: 转角类型
 * <li>ANGLE_ORDER: 转角次数
 * </ul>
 *
 * The transformer normally maps from pixel/line/height to mapx/mapy/height space
 * as a forward transformation though in CEM terms that would be considered
 * an inverse transformation (and is solved by iterative approximation using
 * mapx/mapy/height to pixel/line transformations).  The default direction can
 * be reversed by passing bReversed=TRUE.
 *
 * The iterative solution of pixel/line
 * to mapx/mapy/height is currently run for up to 10 iterations or until
 * the apparent error is less than dfPixErrThreshold pixels.  Passing zero
 * will not avoid all error, but will cause the operation to run for the maximum
 * number of iterations.
 *
 * Additional options to the transformer can be supplied in papszOptions.
 *
 * Options:
 *
 * <ul>
 * <li> CEM_HEIGHT: a fixed height offset to be applied to all points passed
 * in.  In this situation the Z passed into the transformation function is
 * assumed to be height above ground, and the CEM_HEIGHT is assumed to be
 * an average height above sea level for ground in the target scene.
 *
 * <li> CEM_HEIGHT_SCALE: a factor used to multiply heights above ground.
 * Usefull when elevation offsets of the DEM are not expressed in meters. (GDAL >= 1.8.0)
 *
 * <li> CEM_DEM: the name of a GDAL dataset (a DEM file typically) used to
 * extract elevation offsets from. In this situation the Z passed into the
 * transformation function is assumed to be height above ground. This option
 * should be used in replacement of CEM_HEIGHT to provide a way of defining
 * a non uniform ground for the target scene (GDAL >= 1.8.0)
 *
 * <li> CEM_DEMINTERPOLATION: the DEM interpolation (near, bilinear or cubic)
 * </ul>
 *
 * @param psCEMInfo Definition of the CEM parameters.
 *
 * @param bReversed If true "forward" transformation will be mapx/mapy to pixel/line instead of the normal pixel/line to lat/long.
 *
 * @param dfPixErrThreshold the error (measured in pixels) allowed in the
 * iterative solution of pixel/line to mapx/mapy computations (the other way
 * is always exact given the equations).
 *
 * @param papszOptions Other transformer options (ie. CEM_HEIGHT=<z>).
 *
 * @return transformer callback data (deallocate with GDALDestroyTransformer()).
 */

void *GDALCreateCEMTransformer(GDALCEMInfo *psCEMInfo, int bReversed,
	double dfPixErrThreshold, char **papszOptions)

{
	GDALCEMTransformInfo *psTransform;

	/* -------------------------------------------------------------------- */
	/*      Initialize core info.                                           */
	/* -------------------------------------------------------------------- */
	psTransform = (GDALCEMTransformInfo *)
		CPLCalloc(sizeof(GDALCEMTransformInfo), 1);

	memcpy(&(psTransform->sCEM), psCEMInfo, sizeof(GDALCEMInfo));
	psTransform->bReversed = bReversed;
	psTransform->dfPixErrThreshold = dfPixErrThreshold;
	psTransform->dfHeightOffset = 0.0;
	psTransform->dfHeightScale = 1.0;

	memcpy(psTransform->sTI.abySignature, GDAL_GTI2_SIGNATURE, strlen(GDAL_GTI2_SIGNATURE));
	psTransform->sTI.pszClassName = "GDALCEMTransformer";
	psTransform->sTI.pfnTransform = GDALCEMTransform;
	psTransform->sTI.pfnCleanup = GDALDestroyCEMTransformer;
	psTransform->sTI.pfnSerialize = GDALSerializeCEMTransformer;

	/* -------------------------------------------------------------------- */
	/*      Do we have a "average height" that we want to consider all      */
	/*      elevations to be relative to?                                   */
	/* -------------------------------------------------------------------- */
	const char *pszHeight = CSLFetchNameValue(papszOptions, "CEM_HEIGHT");
	if (pszHeight != NULL)
		psTransform->dfHeightOffset = CPLAtof(pszHeight);

	/* -------------------------------------------------------------------- */
	/*                       The "height scale"                             */
	/* -------------------------------------------------------------------- */
	const char *pszHeightScale = CSLFetchNameValue(papszOptions, "CEM_HEIGHT_SCALE");
	if (pszHeightScale != NULL)
		psTransform->dfHeightScale = CPLAtof(pszHeightScale);

	/* -------------------------------------------------------------------- */
	/*                       The DEM file name                              */
	/* -------------------------------------------------------------------- */
	const char *pszDEMPath = CSLFetchNameValue(papszOptions, "CEM_DEM");
	if (pszDEMPath != NULL)
		psTransform->pszDEMPath = CPLStrdup(pszDEMPath);

	/* -------------------------------------------------------------------- */
	/*                      The DEM interpolation                           */
	/* -------------------------------------------------------------------- */
	const char *pszDEMInterpolation = CSLFetchNameValueDef(papszOptions, "CEM_DEMINTERPOLATION", "bilinear");
	if (EQUAL(pszDEMInterpolation, "near"))
		psTransform->eResampleAlg = DRA_NearestNeighbour;
	else if (EQUAL(pszDEMInterpolation, "bilinear"))
		psTransform->eResampleAlg = DRA_Bilinear;
	else if (EQUAL(pszDEMInterpolation, "cubic"))
		psTransform->eResampleAlg = DRA_Cubic;
	else
		psTransform->eResampleAlg = DRA_Bilinear;

	/* -------------------------------------------------------------------- */
	/*      Establish a reference point for calcualating an affine          */
	/*      geotransform approximate transformation.                        */
	/* -------------------------------------------------------------------- */
	double adfGTFromLL[6], dfRefPixel = -1.0, dfRefLine = -1.0;
	double dfRefLong = 0.0, dfRefLat = 0.0;

	// Try with scale and offset if we don't can't use bounds or
	// the results seem daft. 
	if (dfRefPixel < 0.0 || dfRefLine < 0.0
		|| dfRefPixel > 100000 || dfRefLine > 100000)
	{
		dfRefLong = psCEMInfo->dfXS;
		dfRefLat = psCEMInfo->dfYS;

		CEMTransformPoint(psCEMInfo, dfRefLong, dfRefLat, 0.0,
			&dfRefPixel, &dfRefLine);
	}

	/* -------------------------------------------------------------------- */
	/*      Transform nearby locations to establish affine direction        */
	/*      vectors.                                                        */
	/* -------------------------------------------------------------------- */
	double dfRefPixelDelta, dfRefLineDelta, dfLLDelta = 0.0001;

	CEMTransformPoint(psCEMInfo, dfRefLong + dfLLDelta, dfRefLat, 0.0,
		&dfRefPixelDelta, &dfRefLineDelta);
	adfGTFromLL[1] = (dfRefPixelDelta - dfRefPixel) / dfLLDelta;
	adfGTFromLL[4] = (dfRefLineDelta - dfRefLine) / dfLLDelta;

	CEMTransformPoint(psCEMInfo, dfRefLong, dfRefLat + dfLLDelta, 0.0,
		&dfRefPixelDelta, &dfRefLineDelta);
	adfGTFromLL[2] = (dfRefPixelDelta - dfRefPixel) / dfLLDelta;
	adfGTFromLL[5] = (dfRefLineDelta - dfRefLine) / dfLLDelta;

	adfGTFromLL[0] = dfRefPixel
		- adfGTFromLL[1] * dfRefLong - adfGTFromLL[2] * dfRefLat;
	adfGTFromLL[3] = dfRefLine
		- adfGTFromLL[4] * dfRefLong - adfGTFromLL[5] * dfRefLat;

	if (!GDALInvGeoTransform(adfGTFromLL, psTransform->adfPLToLatLongGeoTransform))
	{
		CPLError(CE_Failure, CPLE_AppDefined, "Cannot invert geotransform");
		GDALDestroyRPCTransformer(psTransform);
		return NULL;
	}
	return psTransform;
}

/************************************************************************/
/*                 GDALDestroyReprojectionTransformer()                 */
/************************************************************************/

void GDALDestroyCEMTransformer(void *pTransformAlg)

{
	GDALCEMTransformInfo *psTransform = (GDALCEMTransformInfo *)pTransformAlg;

	CPLFree(psTransform->pszDEMPath);

	if (psTransform->poDS)
		GDALClose(psTransform->poDS);

	if (psTransform->poCT)
		OCTDestroyCoordinateTransformation((OGRCoordinateTransformationH)psTransform->poCT);

	CPLFree(pTransformAlg);
}

/************************************************************************/
/*                      RPCInverseTransformPoint()                      */
/************************************************************************/

static void
CEMInverseTransformPoint(GDALCEMTransformInfo *psTransform,
double dfPixel, double dfLine, double dfHeight,
double *pdfLong, double *pdfLat)

{
	double dfResultX, dfResultY;
	int    iIter;
	GDALCEMInfo *psCEM = &(psTransform->sCEM);

	/* -------------------------------------------------------------------- */
	/*      Compute an initial approximation based on linear                */
	/*      interpolation from our reference point.                         */
	/* -------------------------------------------------------------------- */
	dfResultX = psTransform->adfPLToLatLongGeoTransform[0]
		+ psTransform->adfPLToLatLongGeoTransform[1] * dfPixel
		+ psTransform->adfPLToLatLongGeoTransform[2] * dfLine;

	dfResultY = psTransform->adfPLToLatLongGeoTransform[3]
		+ psTransform->adfPLToLatLongGeoTransform[4] * dfPixel
		+ psTransform->adfPLToLatLongGeoTransform[5] * dfLine;

	/* -------------------------------------------------------------------- */
	/*      Now iterate, trying to find a closer LL location that will      */
	/*      back transform to the indicated pixel and line.                 */
	/* -------------------------------------------------------------------- */
	double dfPixelDeltaX = 0.0, dfPixelDeltaY = 0.0;

	for (iIter = 0; iIter < 10; iIter++)
	{
		double dfBackPixel, dfBackLine;

		CEMTransformPoint(psCEM, dfResultX, dfResultY, dfHeight,
			&dfBackPixel, &dfBackLine);

		dfPixelDeltaX = dfBackPixel - dfPixel;
		dfPixelDeltaY = dfBackLine - dfLine;

		dfResultX = dfResultX
			- dfPixelDeltaX * psTransform->adfPLToLatLongGeoTransform[1]
			- dfPixelDeltaY * psTransform->adfPLToLatLongGeoTransform[2];
		dfResultY = dfResultY
			- dfPixelDeltaX * psTransform->adfPLToLatLongGeoTransform[4]
			- dfPixelDeltaY * psTransform->adfPLToLatLongGeoTransform[5];

		if (ABS(dfPixelDeltaX) < psTransform->dfPixErrThreshold
			&& ABS(dfPixelDeltaY) < psTransform->dfPixErrThreshold)
		{
			iIter = -1;
			//CPLDebug( "CEM", "Converged!" );
			break;
		}
	}

	if (iIter != -1)
	{
#ifdef notdef
		CPLDebug( "CEM", "Failed Iterations %d: Got: %g,%g  Offset=%g,%g", 
			iIter, 
			dfResultX, dfResultY,
			dfPixelDeltaX, dfPixelDeltaY );
#endif
	}

	*pdfLong = dfResultX;
	*pdfLat = dfResultY;
}


static
double BiCubicKernel(double dfVal)
{
	if (dfVal > 2.0)
		return 0.0;

	double a, b, c, d;
	double xm1 = dfVal - 1.0;
	double xp1 = dfVal + 1.0;
	double xp2 = dfVal + 2.0;

	a = (xp2 <= 0.0) ? 0.0 : xp2 * xp2 * xp2;
	b = (xp1 <= 0.0) ? 0.0 : xp1 * xp1 * xp1;
	c = (dfVal <= 0.0) ? 0.0 : dfVal * dfVal * dfVal;
	d = (xm1 <= 0.0) ? 0.0 : xm1 * xm1 * xm1;

	return (0.16666666666666666667 * (a - (4.0 * b) + (6.0 * c) - (4.0 * d)));
}

/************************************************************************/
/*                          GDALRPCTransform()                          */
/************************************************************************/

int GDALCEMTransform(void *pTransformArg, int bDstToSrc,
	int nPointCount,
	double *padfX, double *padfY, double *padfZ,
	int *panSuccess)

{
	VALIDATE_POINTER1(pTransformArg, "GDALCEMTransform", 0);

	GDALCEMTransformInfo *psTransform = (GDALCEMTransformInfo *)pTransformArg;
	GDALCEMInfo *psCEM = &(psTransform->sCEM);
	int i;

	if (psTransform->bReversed)
		bDstToSrc = !bDstToSrc;

	int bands[1] = { 1 };
	int nRasterXSize = 0, nRasterYSize = 0;

	/* -------------------------------------------------------------------- */
	/*      Lazy opening of the optionnal DEM file.                         */
	/* -------------------------------------------------------------------- */
	if (psTransform->pszDEMPath != NULL &&
		psTransform->bHasTriedOpeningDS == FALSE)
	{
		int bIsValid = FALSE;
		psTransform->bHasTriedOpeningDS = TRUE;
		psTransform->poDS = (GDALDataset *)
			GDALOpen(psTransform->pszDEMPath, GA_ReadOnly);
		if (psTransform->poDS != NULL && psTransform->poDS->GetRasterCount() >= 1)
		{
			const char* pszSpatialRef = psTransform->poDS->GetProjectionRef();
			if (pszSpatialRef != NULL && pszSpatialRef[0] != '\0')
			{
				OGRSpatialReference* poWGSSpaRef =
					new OGRSpatialReference(SRS_WKT_WGS84_LAT_LONG);
				OGRSpatialReference* poDSSpaRef =
					new OGRSpatialReference(pszSpatialRef);
				if (!poWGSSpaRef->IsSame(poDSSpaRef))
					psTransform->poCT = OGRCreateCoordinateTransformation(
					poWGSSpaRef, poDSSpaRef);
				delete poWGSSpaRef;
				delete poDSSpaRef;
			}

			if (psTransform->poDS->GetGeoTransform(
				psTransform->adfGeoTransform) == CE_None &&
				GDALInvGeoTransform(psTransform->adfGeoTransform,
				psTransform->adfReverseGeoTransform))
			{
				bIsValid = TRUE;
			}
		}

		if (!bIsValid && psTransform->poDS != NULL)
		{
			GDALClose(psTransform->poDS);
			psTransform->poDS = NULL;
		}
	}
	if (psTransform->poDS)
	{
		nRasterXSize = psTransform->poDS->GetRasterXSize();
		nRasterYSize = psTransform->poDS->GetRasterYSize();
	}

	/* -------------------------------------------------------------------- */
	/*      The simple case is transforming from lat/long to pixel/line.    */
	/*      Just apply the equations directly.                              */
	/* -------------------------------------------------------------------- */
	if (bDstToSrc)
	{
		for (i = 0; i < nPointCount; i++)
		{
			if (psTransform->poDS)
			{
				double dfX, dfY;
				//check if dem is not in WGS84 and transform points padfX[i], padfY[i]
				if (psTransform->poCT)
				{
					double dfXOrig = padfX[i];
					double dfYOrig = padfY[i];
					double dfZOrig = padfZ[i];
					if (!psTransform->poCT->Transform(
						1, &dfXOrig, &dfYOrig, &dfZOrig))
					{
						panSuccess[i] = FALSE;
						continue;
					}
					GDALApplyGeoTransform(psTransform->adfReverseGeoTransform,
						dfXOrig, dfYOrig, &dfX, &dfY);
				}
				else
					GDALApplyGeoTransform(psTransform->adfReverseGeoTransform,
					padfX[i], padfY[i], &dfX, &dfY);
				int dX = int(dfX);
				int dY = int(dfY);

				if (!(dX >= 0 && dY >= 0 &&
					dX + 2 <= nRasterXSize && dY + 2 <= nRasterYSize))
				{
					panSuccess[i] = FALSE;
					continue;
				}

				double dfDEMH(0);
				double dfDeltaX = dfX - dX;
				double dfDeltaY = dfY - dY;

				if (psTransform->eResampleAlg == DRA_Cubic)
				{
					int dXNew = dX - 1;
					int dYNew = dY - 1;
					if (!(dXNew >= 0 && dYNew >= 0 && dXNew + 4 <= nRasterXSize && dYNew + 4 <= nRasterYSize))
					{
						panSuccess[i] = FALSE;
						continue;
					}
					//cubic interpolation
					int anElevData[16] = { 0 };
					CPLErr eErr = psTransform->poDS->RasterIO(GF_Read, dXNew, dYNew, 4, 4,
						&anElevData, 4, 4,
						GDT_Int32, 1, bands, 0, 0, 0, NULL);
					if (eErr != CE_None)
					{
						panSuccess[i] = FALSE;
						continue;
					}

					double dfSumH(0);
					for (int i = 0; i < 4; i++)
					{
						// Loop across the X axis
						for (int j = 0; j < 4; j++)
						{
							// Calculate the weight for the specified pixel according
							// to the bicubic b-spline kernel we're using for
							// interpolation
							int dKernIndX = j - 1;
							int dKernIndY = i - 1;
							double dfPixelWeight = BiCubicKernel(dKernIndX - dfDeltaX) * BiCubicKernel(dKernIndY - dfDeltaY);

							// Create a sum of all values
							// adjusted for the pixel's calculated weight
							dfSumH += anElevData[j + i * 4] * dfPixelWeight;
						}
					}
					dfDEMH = dfSumH;
				}
				else if (psTransform->eResampleAlg == DRA_Bilinear)
				{
					if (!(dX >= 0 && dY >= 0 && dX + 2 <= nRasterXSize && dY + 2 <= nRasterYSize))
					{
						panSuccess[i] = FALSE;
						continue;
					}
					//bilinear interpolation
					int anElevData[4] = { 0, 0, 0, 0 };
					CPLErr eErr = psTransform->poDS->RasterIO(GF_Read, dX, dY, 2, 2,
						&anElevData, 2, 2,
						GDT_Int32, 1, bands, 0, 0, 0, NULL);
					if (eErr != CE_None)
					{
						panSuccess[i] = FALSE;
						continue;
					}
					double dfDeltaX1 = 1.0 - dfDeltaX;
					double dfDeltaY1 = 1.0 - dfDeltaY;

					double dfXZ1 = anElevData[0] * dfDeltaX1 + anElevData[1] * dfDeltaX;
					double dfXZ2 = anElevData[2] * dfDeltaX1 + anElevData[3] * dfDeltaX;
					double dfYZ = dfXZ1 * dfDeltaY1 + dfXZ2 * dfDeltaY;
					dfDEMH = dfYZ;
				}
				else
				{
					if (!(dX >= 0 && dY >= 0 && dX < nRasterXSize && dY < nRasterYSize))
					{
						panSuccess[i] = FALSE;
						continue;
					}
					CPLErr eErr = psTransform->poDS->RasterIO(GF_Read, dX, dY, 1, 1,
						&dfDEMH, 1, 1,
						GDT_Float64, 1, bands, 0, 0, 0, NULL);
					if (eErr != CE_None)
					{
						panSuccess[i] = FALSE;
						continue;
					}
				}

				CEMTransformPoint(psCEM, padfX[i], padfY[i],
					padfZ[i] + (psTransform->dfHeightOffset + dfDEMH) *
					psTransform->dfHeightScale,
					padfX + i, padfY + i);
			}
			else
				CEMTransformPoint(psCEM, padfX[i], padfY[i],
				padfZ[i] + psTransform->dfHeightOffset *
				psTransform->dfHeightScale,
				padfX + i, padfY + i);
			panSuccess[i] = TRUE;
		}

		return TRUE;
	}

	/* -------------------------------------------------------------------- */
	/*      Compute the inverse (pixel/line/height to lat/long).  This      */
	/*      function uses an iterative method from an initial linear        */
	/*      approximation.                                                  */
	/* -------------------------------------------------------------------- */
	for (i = 0; i < nPointCount; i++)
	{
		double dfResultX, dfResultY;

		if (psTransform->poDS)
		{
			CEMInverseTransformPoint(psTransform, padfX[i], padfY[i],
				padfZ[i] + psTransform->dfHeightOffset *
				psTransform->dfHeightScale,
				&dfResultX, &dfResultY);

			double dfX, dfY;
			//check if dem is not in WGS84 and transform points padfX[i], padfY[i]
			if (psTransform->poCT)
			{
				double dfZ = 0;
				if (!psTransform->poCT->Transform(1, &dfResultX, &dfResultY, &dfZ))
				{
					panSuccess[i] = FALSE;
					continue;
				}
			}

			GDALApplyGeoTransform(psTransform->adfReverseGeoTransform,
				dfResultX, dfResultY, &dfX, &dfY);
			int dX = int(dfX);
			int dY = int(dfY);

			double dfDEMH(0);
			double dfDeltaX = dfX - dX;
			double dfDeltaY = dfY - dY;

			if (psTransform->eResampleAlg == DRA_Cubic)
			{
				int dXNew = dX - 1;
				int dYNew = dY - 1;
				if (!(dXNew >= 0 && dYNew >= 0 && dXNew + 4 <= nRasterXSize && dYNew + 4 <= nRasterYSize))
				{
					panSuccess[i] = FALSE;
					continue;
				}
				//cubic interpolation
				int anElevData[16] = { 0 };
				CPLErr eErr = psTransform->poDS->RasterIO(GF_Read, dXNew, dYNew, 4, 4,
					&anElevData, 4, 4,
					GDT_Int32, 1, bands, 0, 0, 0, NULL);
				if (eErr != CE_None)
				{
					panSuccess[i] = FALSE;
					continue;
				}

				double dfSumH(0);
				for (int r = 0; r < 4; r++)
				{
					// Loop across the X axis
					for (int c = 0; c < 4; c++)
					{
						// Calculate the weight for the specified pixel according
						// to the bicubic b-spline kernel we're using for
						// interpolation
						int dKernIndX = c - 1;
						int dKernIndY = r - 1;
						double dfPixelWeight = BiCubicKernel(dKernIndX - dfDeltaX) * BiCubicKernel(dKernIndY - dfDeltaY);

						// Create a sum of all values
						// adjusted for the pixel's calculated weight
						dfSumH += anElevData[c + r * 4] * dfPixelWeight;
					}
				}
				dfDEMH = dfSumH;
			}
			else if (psTransform->eResampleAlg == DRA_Bilinear)
			{
				if (!(dX >= 0 && dY >= 0 && dX + 2 <= nRasterXSize && dY + 2 <= nRasterYSize))
				{
					panSuccess[i] = FALSE;
					continue;
				}
				//bilinear interpolation
				int anElevData[4] = { 0, 0, 0, 0 };
				CPLErr eErr = psTransform->poDS->RasterIO(GF_Read, dX, dY, 2, 2,
					&anElevData, 2, 2,
					GDT_Int32, 1, bands, 0, 0, 0, NULL);
				if (eErr != CE_None)
				{
					panSuccess[i] = FALSE;
					continue;
				}
				double dfDeltaX1 = 1.0 - dfDeltaX;
				double dfDeltaY1 = 1.0 - dfDeltaY;

				double dfXZ1 = anElevData[0] * dfDeltaX1 + anElevData[1] * dfDeltaX;
				double dfXZ2 = anElevData[2] * dfDeltaX1 + anElevData[3] * dfDeltaX;
				double dfYZ = dfXZ1 * dfDeltaY1 + dfXZ2 * dfDeltaY;
				dfDEMH = dfYZ;
			}
			else
			{
				if (!(dX >= 0 && dY >= 0 && dX < nRasterXSize && dY < nRasterYSize))
				{
					panSuccess[i] = FALSE;
					continue;
				}
				CPLErr eErr = psTransform->poDS->RasterIO(GF_Read, dX, dY, 1, 1,
					&dfDEMH, 1, 1,
					GDT_Float64, 1, bands, 0, 0, 0, NULL);
				if (eErr != CE_None)
				{
					panSuccess[i] = FALSE;
					continue;
				}
			}

			CEMInverseTransformPoint(psTransform, padfX[i], padfY[i],
				padfZ[i] + (psTransform->dfHeightOffset + dfDEMH) *
				psTransform->dfHeightScale,
				&dfResultX, &dfResultY);
		}
		else
		{
			CEMInverseTransformPoint(psTransform, padfX[i], padfY[i],
				padfZ[i] + psTransform->dfHeightOffset *
				psTransform->dfHeightScale,
				&dfResultX, &dfResultY);

		}
		padfX[i] = dfResultX;
		padfY[i] = dfResultY;

		panSuccess[i] = TRUE;
	}

	return TRUE;
}

/************************************************************************/
/*                    GDALSerializeCEMTransformer()                     */
/************************************************************************/

CPLXMLNode *GDALSerializeCEMTransformer(void *pTransformArg)

{
	VALIDATE_POINTER1(pTransformArg, "GDALSerializeCEMTransformer", NULL);

	CPLXMLNode *psTree;
	GDALCEMTransformInfo *psInfo = (GDALCEMTransformInfo *)(pTransformArg);

	psTree = CPLCreateXMLNode(NULL, CXT_Element, "CEMTransformer");

	/* -------------------------------------------------------------------- */
	/*      Serialize bReversed.                                            */
	/* -------------------------------------------------------------------- */
	CPLCreateXMLElementAndValue(
		psTree, "Reversed",
		CPLString().Printf("%d", psInfo->bReversed));

	/* -------------------------------------------------------------------- */
	/*      Serialize Height Offset.                                        */
	/* -------------------------------------------------------------------- */
	CPLCreateXMLElementAndValue(
		psTree, "HeightOffset",
		CPLString().Printf("%.15g", psInfo->dfHeightOffset));

	/* -------------------------------------------------------------------- */
	/*      Serialize Height Scale.                                         */
	/* -------------------------------------------------------------------- */
	if (psInfo->dfHeightScale != 1.0)
		CPLCreateXMLElementAndValue(
		psTree, "HeightScale",
		CPLString().Printf("%.15g", psInfo->dfHeightScale));

	/* -------------------------------------------------------------------- */
	/*      Serialize DEM path.                                             */
	/* -------------------------------------------------------------------- */
	if (psInfo->pszDEMPath != NULL)
		CPLCreateXMLElementAndValue(
		psTree, "DEMPath",
		CPLString().Printf("%s", psInfo->pszDEMPath));

	/* -------------------------------------------------------------------- */
	/*      Serialize DEM interpolation                                     */
	/* -------------------------------------------------------------------- */
	CPLString soDEMInterpolation;
	switch (psInfo->eResampleAlg)
	{
	case  DRA_NearestNeighbour:
		soDEMInterpolation = "near";
		break;
	case DRA_Cubic:
		soDEMInterpolation = "cubic";
		break;
	default:
	case DRA_Bilinear:
		soDEMInterpolation = "bilinear";
	}
	CPLCreateXMLElementAndValue(
		psTree, "DEMInterpolation", soDEMInterpolation);

	/* -------------------------------------------------------------------- */
	/*      Serialize pixel error threshold.                                */
	/* -------------------------------------------------------------------- */
	CPLCreateXMLElementAndValue(
		psTree, "PixErrThreshold",
		CPLString().Printf("%.15g", psInfo->dfPixErrThreshold));

	/* -------------------------------------------------------------------- */
	/*      CEM metadata.                                                   */
	/* -------------------------------------------------------------------- */
	CPLXMLNode *psMD = CPLCreateXMLNode(psTree, CXT_Element, "CEM");
	CPLCreateXMLElementAndValue(psMD, "FocalLength", CPLString().Printf("%.15g", psInfo->sCEM.dfFocalLength));
	CPLCreateXMLElementAndValue(psMD, "PrincipalPointX0", CPLString().Printf("%.15g", psInfo->sCEM.dfX0));
	CPLCreateXMLElementAndValue(psMD, "PrincipalPointY0", CPLString().Printf("%.15g", psInfo->sCEM.dfY0));
	CPLCreateXMLElementAndValue(psMD, "XPixelSize", CPLString().Printf("%.15g", psInfo->sCEM.dfXPS));
	CPLCreateXMLElementAndValue(psMD, "YPixelSize", CPLString().Printf("%.15g", psInfo->sCEM.dfYPS));

	CPLCreateXMLElementAndValue(psMD, "XS", CPLString().Printf("%.15g", psInfo->sCEM.dfXS));
	CPLCreateXMLElementAndValue(psMD, "YS", CPLString().Printf("%.15g", psInfo->sCEM.dfYS));
	CPLCreateXMLElementAndValue(psMD, "ZS", CPLString().Printf("%.15g", psInfo->sCEM.dfZS));
	CPLCreateXMLElementAndValue(psMD, "Omega", CPLString().Printf("%.15g", psInfo->sCEM.adfOmega[0]));
	CPLCreateXMLElementAndValue(psMD, "Phi", CPLString().Printf("%.15g", psInfo->sCEM.adfPhi[0]));
	CPLCreateXMLElementAndValue(psMD, "Kappa", CPLString().Printf("%.15g", psInfo->sCEM.adfKappa[0]));
	CPLCreateXMLElementAndValue(psMD, "AngleType", CPLString().Printf("%.15g", psInfo->sCEM.nAngleType));

	return psTree;
}

/************************************************************************/
/*                   GDALDeserializeCEMTransformer()                    */
/************************************************************************/

void *GDALDeserializeCEMTransformer(CPLXMLNode *psTree)

{
	void *pResult;
	char **papszOptions = NULL;

	/* -------------------------------------------------------------------- */
	/*      Collect metadata.                                               */
	/* -------------------------------------------------------------------- */
	CPLXMLNode *psMetadata;
	GDALCEMInfo sCEM;

	psMetadata = CPLGetXMLNode(psTree, "CEM");

	if (psMetadata == NULL
		|| psMetadata->eType != CXT_Element
		|| !EQUAL(psMetadata->pszValue, "CEM"))
		return NULL;

	/* -------------------------------------------------------------------- */
	/*      Get CEM info.                                                   */
	/* -------------------------------------------------------------------- */
	const char* pszTemp = NULL;
	sCEM.dfFocalLength = CPLAtof(CPLGetXMLValue(psMetadata, "FocalLength", "0"));
	sCEM.dfX0 = CPLAtof(CPLGetXMLValue(psMetadata, "PrincipalPointX0", "0"));
	sCEM.dfY0 = CPLAtof(CPLGetXMLValue(psMetadata, "PrincipalPointY0", "0"));

	pszTemp = CPLGetXMLValue(psTree, "XPixelSize", NULL);
	if (pszTemp != NULL)
		sCEM.dfXPS = CPLAtof(pszTemp);

	pszTemp = CPLGetXMLValue(psTree, "YPixelSize", NULL);
	if (pszTemp != NULL)
		sCEM.dfYPS = CPLAtof(pszTemp);

	sCEM.dfXS = CPLAtof(CPLGetXMLValue(psMetadata, "XS", "0"));
	sCEM.dfYS = CPLAtof(CPLGetXMLValue(psMetadata, "YS", "0"));
	sCEM.dfZS = CPLAtof(CPLGetXMLValue(psMetadata, "ZS", "0"));
	sCEM.adfOmega[0] = CPLAtof(CPLGetXMLValue(psMetadata, "Omega", "0"));
	sCEM.adfPhi[0] = CPLAtof(CPLGetXMLValue(psMetadata, "Phi", "0"));
	sCEM.adfKappa[0] = CPLAtof(CPLGetXMLValue(psMetadata, "Kappa", "0"));
	sCEM.nAngleType = atoi(CPLGetXMLValue(psMetadata, "AngleType", "0"));

	/* -------------------------------------------------------------------- */
	/*      Get other flags.                                                */
	/* -------------------------------------------------------------------- */
	double dfPixErrThreshold;
	int bReversed;

	bReversed = atoi(CPLGetXMLValue(psTree, "Reversed", "0"));
	dfPixErrThreshold = CPLAtof(CPLGetXMLValue(psTree, "PixErrThreshold", "0.25"));

	papszOptions = CSLSetNameValue(papszOptions, "CEM_HEIGHT", CPLGetXMLValue(psTree, "HeightOffset", "0"));
	papszOptions = CSLSetNameValue(papszOptions, "CEM_HEIGHT_SCALE", CPLGetXMLValue(psTree, "HeightScale", "1"));

	const char* pszDEMPath = CPLGetXMLValue(psTree, "DEMPath", NULL);
	if (pszDEMPath != NULL)
		papszOptions = CSLSetNameValue(papszOptions, "CEM_DEM", pszDEMPath);

	const char* pszDEMInterpolation = CPLGetXMLValue(psTree, "DEMInterpolation", "bilinear");
	if (pszDEMInterpolation != NULL)
		papszOptions = CSLSetNameValue(papszOptions, "CEM_DEMINTERPOLATION", pszDEMInterpolation);

	/* -------------------------------------------------------------------- */
	/*      Generate transformation.                                        */
	/* -------------------------------------------------------------------- */
	pResult = GDALCreateCEMTransformer(&sCEM, bReversed, dfPixErrThreshold, papszOptions);

	CSLDestroy(papszOptions);

	return pResult;
}
