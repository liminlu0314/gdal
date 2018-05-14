/******************************************************************************
 *
 * Project:  High Performance Image Reprojector
 * Purpose:  Triangular Irregular Network transformer (GDAL wrapper portion)
 * Author:   Minlu Li, liminlu0314@163.com
 *
 ******************************************************************************
 * Copyright (c) 2018, Minlu Li <liminlu0314@163.com>
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

#include "cpl_port.h"

#include <stdlib.h>
#include <string.h>
#include <map>
#include <utility>

#include "cpl_atomic_ops.h"
#include "cpl_conv.h"
#include "cpl_error.h"
#include "cpl_minixml.h"
#include "cpl_multiproc.h"
#include "cpl_string.h"
#include "gdal.h"
#include "gdal_alg_ext.h"
#include "gdal_alg_priv.h"
#include "gdal_priv.h"

CPL_C_START
CPLXMLNode *GDALSerializeTINTransformer(void *pTransformArg);
void *GDALDeserializeTINTransformer(CPLXMLNode *psTree);
CPL_C_END

//三角网内插类
class GeorefTin2D
{
public:
	GeorefTin2D();
	~GeorefTin2D();

	bool SetGCPs(int nGCPCount, const GDAL_GCP *pasGCPList);
	bool SolveTin(void);
	bool GetPoint(const double Px, const double Py, double *Pvars, int bDstToSrc, int nLastId = 0);

private:

	int GetTriangleId(double dfX, double dfY, int bDstToSrc, int nLastId = 0);

	struct TinGeoTransform
	{
		double adfGeoTransform[6];
		double adfInvGeoTransform[6];

		TinGeoTransform()
		{
			memset(adfGeoTransform, 0, sizeof(double) * 6);
			memset(adfInvGeoTransform, 0, sizeof(double) * 6);
		}
	};

	double *padfP;
	double *padfL;
	double *padfX;
	double *padfY;
	int nCount;

	GDALTriangulation *pTriangulation;
	GDALTriangulation *pTriangulationInv;

	TinGeoTransform *pTinGeoTransform;
	double adfGeoTransform[6];
	double adfInvGeoTransform[6];
};

GeorefTin2D::GeorefTin2D()
{
	padfX = nullptr;
	padfY = nullptr;
	padfP = nullptr;
	padfL = nullptr;
	nCount = 0;
	pTriangulation = nullptr;
	pTriangulationInv = nullptr;
	pTinGeoTransform = nullptr;
	memset(adfGeoTransform, 0, sizeof(double) * 6);
	memset(adfInvGeoTransform, 0, sizeof(double) * 6);
}

GeorefTin2D::~GeorefTin2D()
{
	if (padfP != nullptr) { delete[]padfP; padfP = nullptr; }
	if (padfL != nullptr) { delete[]padfL; padfL = nullptr; }
	if (padfX != nullptr) { delete[]padfX; padfX = nullptr; }
	if (padfY != nullptr) { delete[]padfY; padfY = nullptr; }
	if (pTriangulation != nullptr)
		GDALTriangulationFree(pTriangulation);
	if (pTriangulationInv != nullptr)
		GDALTriangulationFree(pTriangulationInv);
	if (pTinGeoTransform != nullptr)
		CPLFree(pTinGeoTransform);
}

bool GeorefTin2D::SetGCPs(int nGCPCount, const GDAL_GCP *pasGCPList)
{
	if (nGCPCount < 3 || pasGCPList == nullptr)
		return false;

	//先计算一个整体的仿射变换模型
	GDALGCPsToGeoTransform(nGCPCount, pasGCPList, adfGeoTransform, FALSE);
	GDALInvGeoTransform(adfGeoTransform, adfInvGeoTransform);

	nCount = nGCPCount;
	padfP = new double[nGCPCount];
	padfL = new double[nGCPCount];
	padfX = new double[nGCPCount];
	padfY = new double[nGCPCount];

	for (int i = 0; i < nGCPCount; i++)
	{
		padfP[i] = pasGCPList[i].dfGCPPixel;
		padfL[i] = pasGCPList[i].dfGCPLine;
		padfX[i] = pasGCPList[i].dfGCPX;
		padfY[i] = pasGCPList[i].dfGCPY;
	}

	return true;
}

bool GeorefTin2D::SolveTin(void)
{
	if (GDALHasTriangulation() != TRUE)	//GDAL库中不支持三角网构建算法
		return false;

	//构造三角网，使用目标影像上的坐标
	pTriangulation = GDALTriangulationCreateDelaunay(nCount, padfX, padfY);
	if (pTriangulation == nullptr)
		return false;

	pTriangulationInv = (GDALTriangulation*)CPLCalloc(1, sizeof(GDALTriangulation));
	pTriangulationInv->nFacets = pTriangulation->nFacets;
	pTriangulationInv->pasFacets = (GDALTriFacet*)VSI_MALLOC2_VERBOSE(pTriangulationInv->nFacets, sizeof(GDALTriFacet));
	for (int n = 0; n < pTriangulationInv->nFacets; n++)
		memcpy(&pTriangulationInv->pasFacets[n], &pTriangulation->pasFacets[n], sizeof(GDALTriFacet));

	//计算三角网重心
	GDALTriangulationComputeBarycentricCoefficients(pTriangulation, padfX, padfY);
	GDALTriangulationComputeBarycentricCoefficients(pTriangulationInv, padfP, padfL);

	int nTinNum = pTriangulation->nFacets;
	pTinGeoTransform = (TinGeoTransform *)CPLMalloc(sizeof(TinGeoTransform) * nTinNum);

	//通过点的id序号，对应到PL坐标系中
	for (int i = 0; i < nTinNum; i++)
	{
		int nGCPCount = 3;
		GDAL_GCP *pasGCPs = (GDAL_GCP *)CPLMalloc(sizeof(GDAL_GCP) * nGCPCount);
		GDALInitGCPs(nGCPCount, pasGCPs);

		//获取当前三角形的顶点ID，并取出坐标
		int *panVertexIdx = pTriangulation->pasFacets[i].anVertexIdx;
		for (int n = 0; n < 3; n++)
		{
			pasGCPs[n].dfGCPLine = padfL[panVertexIdx[n]];
			pasGCPs[n].dfGCPPixel = padfP[panVertexIdx[n]];
			pasGCPs[n].dfGCPX = padfX[panVertexIdx[n]];
			pasGCPs[n].dfGCPY = padfY[panVertexIdx[n]];
		}

		// 计算仿射变换参数
		if (GDALGCPsToGeoTransform(nGCPCount, pasGCPs, pTinGeoTransform[i].adfGeoTransform, FALSE))
		{
			// 计算逆变换参数
			GDALInvGeoTransform(pTinGeoTransform[i].adfGeoTransform, pTinGeoTransform[i].adfInvGeoTransform);
		}
		else
		{
			memset(pTinGeoTransform[i].adfGeoTransform, 0, sizeof(double) * 6);
			memset(pTinGeoTransform[i].adfInvGeoTransform, 0, sizeof(double) * 6);
		}

		GDALDeinitGCPs(nGCPCount, pasGCPs);
		CPLFree(pasGCPs);
	}

	return true;
}

int GeorefTin2D::GetTriangleId(double dfX, double dfY, int bDstToSrc, int nLastId)
{
	GDALTriangulation *pTri = bDstToSrc ? pTriangulation : pTriangulationInv;
	int nFacetID = -1;
	GDALTriangulationFindFacetDirected(pTri, nLastId, dfX, dfY, &nFacetID);
	return nFacetID;
}

bool GeorefTin2D::GetPoint(const double Px, const double Py, double * Pvars, int bDstToSrc, int nLastId)
{
	double *padfGeoTransform = NULL;

	int nTriangleId = GetTriangleId(Px, Py, bDstToSrc, nLastId);
	if (nTriangleId < 0)
	{
		if (bDstToSrc)//georef to src(GCP XYZ to GCP PL)
			padfGeoTransform = adfInvGeoTransform;
		else //src to georef(GCP PL to GCP XYZ)
			padfGeoTransform = adfGeoTransform;
	}
	else
	{
		if (bDstToSrc)//georef to src(GCP XYZ to GCP PL)
			padfGeoTransform = pTinGeoTransform[nTriangleId].adfInvGeoTransform;
		else //src to georef(GCP PL to GCP XYZ)
			padfGeoTransform = pTinGeoTransform[nTriangleId].adfGeoTransform;
	}

	double dfX = Px;
	double dfY = Py;

	GDALApplyGeoTransform(padfGeoTransform, Px, Py, &dfX, &dfY);

	Pvars[0] = dfX;
	Pvars[1] = dfY;

	return true;
}

typedef struct
{
	GDALTransformerInfo  sTI;

	GeorefTin2D   *poTin;
	bool           bTinSolved;

	bool      bReversed;

	int       nGCPCount;
	GDAL_GCP *pasGCPList;

	volatile int nRefCount;

} TINTransformInfo;

/************************************************************************/
/*                   GDALCreateSimilarTINTransformer()                  */
/************************************************************************/

static
void* GDALCreateSimilarTINTransformer(void *hTransformArg,
	double dfRatioX, double dfRatioY)
{
	VALIDATE_POINTER1(hTransformArg, "GDALCreateSimilarTINTransformer", nullptr);

	TINTransformInfo *psInfo = static_cast<TINTransformInfo *>(hTransformArg);

	if (dfRatioX == 1.0 && dfRatioY == 1.0)
	{
		// We can just use a ref count, since using the source transformation
		// is thread-safe.
		CPLAtomicInc(&(psInfo->nRefCount));
	}
	else
	{
		GDAL_GCP *pasGCPList = GDALDuplicateGCPs(psInfo->nGCPCount, psInfo->pasGCPList);
		for (int i = 0; i < psInfo->nGCPCount; i++)
		{
			pasGCPList[i].dfGCPPixel /= dfRatioX;
			pasGCPList[i].dfGCPLine /= dfRatioY;
		}
		psInfo = static_cast<TINTransformInfo *>(
			GDALCreateTINTransformer(psInfo->nGCPCount, pasGCPList,
				psInfo->bReversed));
		GDALDeinitGCPs(psInfo->nGCPCount, pasGCPList);
		CPLFree(pasGCPList);
	}

	return psInfo;
}

/************************************************************************/
/*                      GDALCreateTINTransformer()                      */
/************************************************************************/

/**
 * Create Triangular Irregular Network transformer from GCPs.
 *
 * The Triangular Irregular Network transformer produces exact transformation
 * at all control points and smoothly varying transformations between
 * control points with greatest influence from local control points.
 * It is suitable for for many applications not well modeled by polynomial
 * transformations.
 *
 * Creating the TIN transformer involves solving systems of linear equations
 * related to the number of control points involved.  This solution is
 * computed within this function call.  It can be quite an expensive operation
 * for large numbers of GCPs.  For instance, for reference, it takes on the
 * order of 10s for 400 GCPs on a 2GHz Athlon processor.
 *
 * TIN Transformers are serializable.
 *
 * @param nGCPCount the number of GCPs in pasGCPList.
 * @param pasGCPList an array of GCPs to be used as input.
 * @param bReversed set it to TRUE to compute the reversed transformation.
 *
 * @return the transform argument or NULL if creation fails.
 */

void *GDALCreateTINTransformer(int nGCPCount, const GDAL_GCP *pasGCPList,
	int bReversed)
{
	return GDALCreateTINTransformerInt(nGCPCount, pasGCPList, bReversed, nullptr);
}

void *GDALCreateTINTransformerInt(int nGCPCount, const GDAL_GCP *pasGCPList,
	int bReversed, char** papszOptions)
{
	/* -------------------------------------------------------------------- */
	/*      Allocate transform info.                                        */
	/* -------------------------------------------------------------------- */
	TINTransformInfo *psInfo = static_cast<TINTransformInfo *>(
		CPLCalloc(sizeof(TINTransformInfo), 1));

	psInfo->pasGCPList = GDALDuplicateGCPs(nGCPCount, pasGCPList);
	psInfo->nGCPCount = nGCPCount;

	psInfo->bReversed = CPL_TO_BOOL(bReversed);
	psInfo->poTin = new GeorefTin2D();

	memcpy(psInfo->sTI.abySignature, GDAL_GTI2_SIGNATURE, strlen(GDAL_GTI2_SIGNATURE));
	psInfo->sTI.pszClassName = "GDALTINTransformer";
	psInfo->sTI.pfnTransform = GDALTINTransform;
	psInfo->sTI.pfnCleanup = GDALDestroyTINTransformer;
	psInfo->sTI.pfnSerialize = GDALSerializeTINTransformer;
	psInfo->sTI.pfnCreateSimilar = GDALCreateSimilarTINTransformer;

	bool bOK = psInfo->poTin->SetGCPs(nGCPCount, pasGCPList);
	if (!bOK)
	{
		GDALDestroyTINTransformer(psInfo);
		return nullptr;
	}

	psInfo->nRefCount = 1;
	psInfo->bTinSolved = psInfo->poTin->SolveTin();

	if (!psInfo->bTinSolved)
	{
		GDALDestroyTINTransformer(psInfo);
		return nullptr;
	}

	return psInfo;
}

/************************************************************************/
/*                     GDALDestroyTINTransformer()                      */
/************************************************************************/

/**
 * Destroy TIN transformer.
 *
 * This function is used to destroy information about a GCP based
 * polynomial transformation created with GDALCreateTINTransformer().
 *
 * @param pTransformArg the transform arg previously returned by
 * GDALCreateTINTransformer().
 */

void GDALDestroyTINTransformer(void *pTransformArg)

{
	if (pTransformArg == nullptr)
		return;

	TINTransformInfo *psInfo = static_cast<TINTransformInfo *>(pTransformArg);

	if (CPLAtomicDec(&(psInfo->nRefCount)) == 0)
	{
		delete psInfo->poTin;

		GDALDeinitGCPs(psInfo->nGCPCount, psInfo->pasGCPList);
		CPLFree(psInfo->pasGCPList);

		CPLFree(pTransformArg);
	}
}

/************************************************************************/
/*                          GDALTINTransform()                          */
/************************************************************************/

/**
 * Transforms point based on GCP derived TIN model.
 *
 * This function matches the GDALTransformerFunc signature, and can be
 * used to transform one or more points from pixel/line coordinates to
 * georeferenced coordinates (SrcToDst) or vice versa (DstToSrc).
 *
 * @param pTransformArg return value from GDALCreateTINTransformer().
 * @param bDstToSrc TRUE if transformation is from the destination
 * (georeferenced) coordinates to pixel/line or FALSE when transforming
 * from pixel/line to georeferenced coordinates.
 * @param nPointCount the number of values in the x, y and z arrays.
 * @param x array containing the X values to be transformed.
 * @param y array containing the Y values to be transformed.
 * @param z array containing the Z values to be transformed.
 * @param panSuccess array in which a flag indicating success (TRUE) or
 * failure (FALSE) of the transformation are placed.
 *
 * @return TRUE.
 */

int GDALTINTransform(void *pTransformArg, int bDstToSrc,
	int nPointCount,
	double *x, double *y,
	CPL_UNUSED double *z,
	int *panSuccess)
{
	VALIDATE_POINTER1(pTransformArg, "GDALTINTransform", 0);

	TINTransformInfo *psInfo = static_cast<TINTransformInfo *>(pTransformArg);
	int nLastId = 0;

	//(bDstToSrc==TRUE) GCP XYZ to GCP PL
	//(bDstToSrc!=TRUE) GCP PL to GCP XYZ
	for (int i = 0; i < nPointCount; i++)
	{
		double xy_out[2] = { 0.0, 0.0 };
		panSuccess[i] = psInfo->poTin->GetPoint(x[i], y[i], xy_out, bDstToSrc, nLastId);
		x[i] = xy_out[0];
		y[i] = xy_out[1];
	}

	return TRUE;
}

/************************************************************************/
/*                    GDALSerializeTINTransformer()                     */
/************************************************************************/

CPLXMLNode *GDALSerializeTINTransformer(void *pTransformArg)

{
	VALIDATE_POINTER1(pTransformArg, "GDALSerializeTINTransformer", nullptr);

	TINTransformInfo *psInfo = static_cast<TINTransformInfo *>(pTransformArg);

	CPLXMLNode *psTree = CPLCreateXMLNode(nullptr, CXT_Element, "TINTransformer");

	/* -------------------------------------------------------------------- */
	/*      Serialize bReversed.                                            */
	/* -------------------------------------------------------------------- */
	CPLCreateXMLElementAndValue(
		psTree, "Reversed",
		CPLString().Printf("%d", static_cast<int>(psInfo->bReversed)));

	/* -------------------------------------------------------------------- */
	/*      Attach GCP List.                                                */
	/* -------------------------------------------------------------------- */
	if (psInfo->nGCPCount > 0)
	{
		GDALSerializeGCPListToXML(psTree,
			psInfo->pasGCPList,
			psInfo->nGCPCount,
			nullptr);
	}

	return psTree;
}

/************************************************************************/
/*                   GDALDeserializeTINTransformer()                    */
/************************************************************************/

void *GDALDeserializeTINTransformer(CPLXMLNode *psTree)

{
	/* -------------------------------------------------------------------- */
	/*      Check for GCPs.                                                 */
	/* -------------------------------------------------------------------- */
	CPLXMLNode *psGCPList = CPLGetXMLNode(psTree, "GCPList");
	GDAL_GCP *pasGCPList = nullptr;
	int nGCPCount = 0;

	if (psGCPList != nullptr)
	{
		GDALDeserializeGCPListFromXML(psGCPList,
			&pasGCPList,
			&nGCPCount,
			nullptr);
	}

	/* -------------------------------------------------------------------- */
	/*      Get other flags.                                                */
	/* -------------------------------------------------------------------- */
	const int bReversed = atoi(CPLGetXMLValue(psTree, "Reversed", "0"));

	/* -------------------------------------------------------------------- */
	/*      Generate transformation.                                        */
	/* -------------------------------------------------------------------- */
	void *pResult =
		GDALCreateTINTransformer(nGCPCount, pasGCPList, bReversed);

	/* -------------------------------------------------------------------- */
	/*      Cleanup GCP copy.                                               */
	/* -------------------------------------------------------------------- */
	GDALDeinitGCPs(nGCPCount, pasGCPList);
	CPLFree(pasGCPList);

	return pResult;
}
