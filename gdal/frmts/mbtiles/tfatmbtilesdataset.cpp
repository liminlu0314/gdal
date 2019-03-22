/******************************************************************************
 * $Id$
 *
 * Project:  21At MBTiles driver
 * Purpose:  Implement 21At MBTiles support using OGR SQLite driver
 * Author:   Li Minlu, <liminlu0314 at 21stc dot com dot cn>
 *
 **********************************************************************
 * Copyright (c) 2014, Li Minlu <liminlu0314 at 21stc dot com dot cn>
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

#include "gdal_frmts.h"
#include "gdal_pam.h"
#include "ogr_api.h"
#include "ogr_srs_api.h"
#include "cpl_vsil_curl_priv.h"

//#include "zlib.h"
//
#include <math.h>

extern "C" void GDALRegister_TFATMBTiles();

CPL_CVSID("$Id$");

static const char * const apszAllowedDrivers[] = {"JPEG", "PNG", NULL};

struct TileLevelInfo
{
	int nLevelName;
	int nMinRow;
	int nMaxRow;
	int nMinColumn;
	int nMaxColumn;
	int nTileCount;
};

class TfatMBTilesBand;

/************************************************************************/
/* ==================================================================== */
/*                            TfatMBTilesDataset                        */
/* ==================================================================== */
/************************************************************************/

class TfatMBTilesDataset : public GDALPamDataset
{
    friend class TfatMBTilesBand;

  public:
                 TfatMBTilesDataset();
                 TfatMBTilesDataset(TfatMBTilesDataset* poMainDS, int nLevel);

    virtual     ~TfatMBTilesDataset();

    virtual CPLErr GetGeoTransform(double* padfGeoTransform);
    virtual const char* GetProjectionRef();
    
    virtual char      **GetMetadataDomainList();
    virtual char      **GetMetadata( const char * pszDomain = "" );

    static GDALDataset *Open( GDALOpenInfo * );
    static int          Identify( GDALOpenInfo * );

    void                ComputeTileColTileRowZoomLevel( int nBlockXOff,
                                                        int nBlockYOff,
                                                        int &nTileColumn,
                                                        int &nTileRow,
                                                        int &nZoomLevel,
														int &nTileIndex);
  protected:
    virtual int         CloseDependentDatasets();

  private:

    int bMustFree;
    TfatMBTilesDataset* poMainDS;
    int nLevel;
    int nMinTileCol, nMinTileRow;
    int nMinLevel;

    char** papszMetadata;
    char** papszImageStructure;

    int nResolutions;
    TfatMBTilesDataset** papoOverviews;

	int nTileMaxCount;
	int nMBTilesCount;
	OGRDataSourceH *pHDS;
	int nTileInfoCount;
	TileLevelInfo *pTileLevelInfo;

    int bFetchedMetadata;
    CPLStringList aosList;
};

/************************************************************************/
/* ==================================================================== */
/*                            TfatMBTilesBand                           */
/* ==================================================================== */
/************************************************************************/

class TfatMBTilesBand: public GDALPamRasterBand
{
    friend class TfatMBTilesDataset;

    CPLString               osLocationInfo;

  public:
                            TfatMBTilesBand( TfatMBTilesDataset* poDS, int nBand,
                                            GDALDataType eDataType,
                                            int nBlockXSize, int nBlockYSize);

    virtual GDALColorInterp GetColorInterpretation();

    virtual int             GetOverviewCount();
    virtual GDALRasterBand* GetOverview(int nLevel);

    virtual CPLErr          IReadBlock( int, int, void * );

    virtual char      **GetMetadataDomainList();
    virtual const char *GetMetadataItem( const char * pszName,
                                         const char * pszDomain = "" );
};

/************************************************************************/
/*                          TfatMBTilesBand()                           */
/************************************************************************/

TfatMBTilesBand::TfatMBTilesBand(TfatMBTilesDataset* poDS, int nBand,
                                GDALDataType eDataType,
                                int nBlockXSize, int nBlockYSize)
{
    this->poDS = poDS;
    this->nBand = nBand;
    this->eDataType = eDataType;
    this->nBlockXSize = nBlockXSize;
    this->nBlockYSize = nBlockYSize;
}

/************************************************************************/
/*                            IReadBlock()                              */
/************************************************************************/

CPLErr TfatMBTilesBand::IReadBlock( int nBlockXOff, int nBlockYOff, void * pImage)
{
    TfatMBTilesDataset* poGDS = (TfatMBTilesDataset*) poDS;

    int bGotTile = FALSE;
    CPLAssert(eDataType == GDT_Byte);

    int nTileColumn, nTileRow, nZoomLevel, nTilesIndex;
    poGDS->ComputeTileColTileRowZoomLevel(nBlockXOff, nBlockYOff,
                                          nTileColumn, nTileRow, nZoomLevel, nTilesIndex);

    const char* pszSQL = CPLSPrintf("SELECT tile_data FROM tiles WHERE "
                                    "tile_column = %d AND tile_row = %d AND zoom_level=%d",
                                    nTileColumn, nTileRow, nZoomLevel);

    CPLDebug("MBTILES", "nBand=%d, nBlockXOff=%d, nBlockYOff=%d, %s",
             nBand, nBlockXOff, nBlockYOff, pszSQL);

	OGRDataSourceH hDS = poGDS->pHDS[nTilesIndex];
    OGRLayerH hSQLLyr = OGR_DS_ExecuteSQL(hDS, pszSQL, NULL, NULL);

    OGRFeatureH hFeat = hSQLLyr ? OGR_L_GetNextFeature(hSQLLyr) : NULL;
    if (hFeat != NULL)
    {
        CPLString osMemFileName;
        osMemFileName.Printf("/vsimem/%p", this);

        int nDataSize = 0;
        GByte* pabyData = OGR_F_GetFieldAsBinary(hFeat, 0, &nDataSize);

        VSILFILE * fp = VSIFileFromMemBuffer( osMemFileName.c_str(), pabyData,
                                            nDataSize, FALSE);
        VSIFCloseL(fp);

		GDALDatasetH hDSTile = GDALOpenEx(osMemFileName.c_str(),
			GDAL_OF_RASTER | GDAL_OF_INTERNAL,
			apszAllowedDrivers, NULL, NULL);
        if (hDSTile != NULL)
        {
            int nTileBands = GDALGetRasterCount(hDSTile);
            if (nTileBands == 4 && poGDS->nBands == 3)
                nTileBands = 3;

            if (GDALGetRasterXSize(hDSTile) == nBlockXSize &&
                GDALGetRasterYSize(hDSTile) == nBlockYSize &&
                (nTileBands == poGDS->nBands ||
                 (nTileBands == 1 && (poGDS->nBands == 3 || poGDS->nBands == 4)) ||
                 (nTileBands == 3 && poGDS->nBands == 4)))
            {
                int iBand;
                void* pSrcImage = NULL;
                GByte abyTranslation[256][4];

                bGotTile = TRUE;

                GDALColorTableH hCT = GDALGetRasterColorTable(GDALGetRasterBand(hDSTile, 1));
                if (nTileBands == 1 && (poGDS->nBands == 3 || poGDS->nBands == 4))
                {
                    if (hCT != NULL)
                        pSrcImage = CPLMalloc(nBlockXSize * nBlockYSize);
                    iBand = 1;
                }
                else
                    iBand = nBand;

                if (nTileBands == 3 && poGDS->nBands == 4 && iBand == 4)
                    memset(pImage, 255, nBlockXSize * nBlockYSize);
                else
                {
                    GDALRasterIO(GDALGetRasterBand(hDSTile, iBand), GF_Read,
                                0, 0, nBlockXSize, nBlockYSize,
                                pImage, nBlockXSize, nBlockYSize, eDataType, 0, 0);
                }

                if (pSrcImage != NULL && hCT != NULL)
                {
                    int i;
                    memcpy(pSrcImage, pImage, nBlockXSize * nBlockYSize);

                    int nEntryCount = GDALGetColorEntryCount( hCT );
                    if (nEntryCount > 256)
                        nEntryCount = 256;
                    for(i = 0; i < nEntryCount; i++)
                    {
                        const GDALColorEntry* psEntry = GDALGetColorEntry( hCT, i );
                        abyTranslation[i][0] = (GByte) psEntry->c1;
                        abyTranslation[i][1] = (GByte) psEntry->c2;
                        abyTranslation[i][2] = (GByte) psEntry->c3;
                        abyTranslation[i][3] = (GByte) psEntry->c4;
                    }
                    for(; i < 256; i++)
                    {
                        abyTranslation[i][0] = 0;
                        abyTranslation[i][1] = 0;
                        abyTranslation[i][2] = 0;
                        abyTranslation[i][3] = 0;
                    }

                    for(i = 0; i < nBlockXSize * nBlockYSize; i++)
                    {
                        ((GByte*)pImage)[i] = abyTranslation[((GByte*)pSrcImage)[i]][nBand-1];
                    }
                }

                for(int iOtherBand=1;iOtherBand<=poGDS->nBands;iOtherBand++)
                {
                    GDALRasterBlock *poBlock;

                    if (iOtherBand == nBand)
                        continue;

                    poBlock = ((TfatMBTilesBand*)poGDS->GetRasterBand(iOtherBand))->
                        TryGetLockedBlockRef(nBlockXOff,nBlockYOff);

                    if (poBlock != NULL)
                    {
                        poBlock->DropLock();
                        continue;
                    }

                    poBlock = poGDS->GetRasterBand(iOtherBand)->
                        GetLockedBlockRef(nBlockXOff,nBlockYOff, TRUE);
                    if (poBlock == NULL)
                        break;

                    GByte* pabySrcBlock = (GByte *) poBlock->GetDataRef();
                    if( pabySrcBlock == NULL )
                    {
                        poBlock->DropLock();
                        break;
                    }

                    if (nTileBands == 3 && poGDS->nBands == 4 && iOtherBand == 4)
                        memset(pabySrcBlock, 255, nBlockXSize * nBlockYSize);
                    else if (nTileBands == 1 && (poGDS->nBands == 3 || poGDS->nBands == 4))
                    {
                        int i;
                        if (pSrcImage)
                        {
                            for(i = 0; i < nBlockXSize * nBlockYSize; i++)
                            {
                                ((GByte*)pabySrcBlock)[i] =
                                    abyTranslation[((GByte*)pSrcImage)[i]][iOtherBand-1];
                            }
                        }
                        else
                            memcpy(pabySrcBlock, pImage, nBlockXSize * nBlockYSize);
                    }
                    else
                    {
                        GDALRasterIO(GDALGetRasterBand(hDSTile, iOtherBand), GF_Read,
                            0, 0, nBlockXSize, nBlockYSize,
                            pabySrcBlock, nBlockXSize, nBlockYSize, eDataType, 0, 0);
                    }

                    poBlock->DropLock();
                }

                CPLFree(pSrcImage);
            }
            else if (GDALGetRasterXSize(hDSTile) == nBlockXSize &&
                     GDALGetRasterYSize(hDSTile) == nBlockYSize &&
                     (nTileBands == 3 && poGDS->nBands == 1))
            {
                bGotTile = TRUE;

                GByte* pabyRGBImage = (GByte*)CPLMalloc(3 * nBlockXSize * nBlockYSize);
                GDALDatasetRasterIO(hDSTile, GF_Read,
                                    0, 0, nBlockXSize, nBlockYSize,
                                    pabyRGBImage, nBlockXSize, nBlockYSize, eDataType,
                                    3, NULL, 3, 3 * nBlockXSize, 1);
                for(int i=0;i<nBlockXSize*nBlockYSize;i++)
                {
                    int R = pabyRGBImage[3*i];
                    int G = pabyRGBImage[3*i+1];
                    int B = pabyRGBImage[3*i+2];
                    GByte Y = (GByte)((213 * R + 715 * G + 72 * B) / 1000);
                    ((GByte*)pImage)[i] = Y;
                }
                CPLFree(pabyRGBImage);
            }
            else
            {
                CPLDebug("MBTILES", "tile size = %d, tile height = %d, tile bands = %d",
                         GDALGetRasterXSize(hDSTile), GDALGetRasterYSize(hDSTile),
                         GDALGetRasterCount(hDSTile));
            }
            GDALClose(hDSTile);
        }

        VSIUnlink( osMemFileName.c_str() );

        OGR_F_Destroy(hFeat);
    }

    OGR_DS_ReleaseResultSet(hDS, hSQLLyr);

    if (!bGotTile)
    {
        memset(pImage, 0, nBlockXSize * nBlockYSize);

        for(int iOtherBand=1;iOtherBand<=poGDS->nBands;iOtherBand++)
        {
            GDALRasterBlock *poBlock;

            if (iOtherBand == nBand)
                continue;

            poBlock = poGDS->GetRasterBand(iOtherBand)->
                GetLockedBlockRef(nBlockXOff,nBlockYOff, TRUE);
            if (poBlock == NULL)
                break;

            GByte* pabySrcBlock = (GByte *) poBlock->GetDataRef();
            if( pabySrcBlock == NULL )
            {
                poBlock->DropLock();
                break;
            }

            memset(pabySrcBlock, 0, nBlockXSize * nBlockYSize);

            poBlock->DropLock();
        }
    }

    return CE_None;
}

/************************************************************************/
/*                      GetMetadataDomainList()                         */
/************************************************************************/

char **TfatMBTilesBand::GetMetadataDomainList()
{
    return CSLAddString(GDALPamRasterBand::GetMetadataDomainList(), "LocationInfo");
}

/************************************************************************/
/*                         GetMetadataItem()                            */
/************************************************************************/

const char *TfatMBTilesBand::GetMetadataItem( const char * pszName,
                                          const char * pszDomain )
{
	return GDALPamRasterBand::GetMetadataItem(pszName, pszDomain);
}

/************************************************************************/
/*                         GetOverviewCount()                           */
/************************************************************************/

int TfatMBTilesBand::GetOverviewCount()
{
    TfatMBTilesDataset* poGDS = (TfatMBTilesDataset*) poDS;

    if (poGDS->nResolutions >= 1)
        return poGDS->nResolutions;
    else
        return GDALPamRasterBand::GetOverviewCount();
}

/************************************************************************/
/*                              GetOverview()                           */
/************************************************************************/

GDALRasterBand* TfatMBTilesBand::GetOverview(int nLevel)
{
    TfatMBTilesDataset* poGDS = (TfatMBTilesDataset*) poDS;

    if (poGDS->nResolutions == 0)
        return GDALPamRasterBand::GetOverview(nLevel);

    if (nLevel < 0 || nLevel >= poGDS->nResolutions)
        return NULL;

    GDALDataset* poOvrDS = poGDS->papoOverviews[nLevel];
    if (poOvrDS)
        return poOvrDS->GetRasterBand(nBand);
    else
        return NULL;
}

/************************************************************************/
/*                   GetColorInterpretation()                           */
/************************************************************************/

GDALColorInterp TfatMBTilesBand::GetColorInterpretation()
{
    TfatMBTilesDataset* poGDS = (TfatMBTilesDataset*) poDS;
    if (poGDS->nBands == 1)
    {
        return GCI_GrayIndex;
    }
    else if (poGDS->nBands == 3 || poGDS->nBands == 4)
    {
        if (nBand == 1)
            return GCI_RedBand;
        else if (nBand == 2)
            return GCI_GreenBand;
        else if (nBand == 3)
            return GCI_BlueBand;
        else if (nBand == 4)
            return GCI_AlphaBand;
    }

    return GCI_Undefined;
}

/************************************************************************/
/*                         MBTilesDataset()                          */
/************************************************************************/

TfatMBTilesDataset::TfatMBTilesDataset()
{
    bMustFree = FALSE;
    nLevel = 0;
    poMainDS = NULL;
    nResolutions = 0;
	nMBTilesCount = 0;
	pHDS = NULL;
	nTileInfoCount = 0;
	pTileLevelInfo = NULL;
	nTileMaxCount = 20000;
    papoOverviews = NULL;
    papszMetadata = NULL;
    papszImageStructure = CSLAddString(NULL, "INTERLEAVE=PIXEL");
    nMinTileCol = nMinTileRow = 0;
    nMinLevel = 0;
    bFetchedMetadata = FALSE;
}

/************************************************************************/
/*                          MBTilesDataset()                            */
/************************************************************************/

TfatMBTilesDataset::TfatMBTilesDataset(TfatMBTilesDataset* poMainDS, int nLevel)
{
    bMustFree = FALSE;
    this->nLevel = nLevel;
    this->poMainDS = poMainDS;
    nResolutions = poMainDS->nResolutions - nLevel;
	nTileInfoCount = poMainDS->nTileInfoCount;
	pTileLevelInfo = poMainDS->pTileLevelInfo;
	nMBTilesCount = poMainDS->nMBTilesCount;
	pHDS = poMainDS->pHDS;
	nTileMaxCount = poMainDS->nTileMaxCount;
    papoOverviews = poMainDS->papoOverviews + nLevel;
    papszMetadata = poMainDS->papszMetadata;
    papszImageStructure =  poMainDS->papszImageStructure;

    nRasterXSize = poMainDS->nRasterXSize / (1 << nLevel);
    nRasterYSize = poMainDS->nRasterYSize / (1 << nLevel);
    nMinTileCol = nMinTileRow = 0;
    nMinLevel = 0;
    bFetchedMetadata = FALSE;
}

/************************************************************************/
/*                        ~MBTilesDataset()                             */
/************************************************************************/

TfatMBTilesDataset::~TfatMBTilesDataset()
{
    CloseDependentDatasets();
}

/************************************************************************/
/*                      CloseDependentDatasets()                        */
/************************************************************************/

int TfatMBTilesDataset::CloseDependentDatasets()
{
    int bRet = GDALPamDataset::CloseDependentDatasets();

    if (poMainDS == NULL && !bMustFree)
    {
        CSLDestroy(papszMetadata);
        papszMetadata = NULL;
        CSLDestroy(papszImageStructure);
        papszImageStructure = NULL;

        int i;

        if (papoOverviews)
        {
            for(i=0;i<nResolutions;i++)
            {
                if (papoOverviews[i] != NULL && papoOverviews[i]->bMustFree)
                    papoOverviews[i]->poMainDS = NULL;

				delete papoOverviews[i];
            }

            CPLFree(papoOverviews);
            papoOverviews = NULL;
            nResolutions = 0;
            bRet = TRUE;
        }

		if (pHDS != NULL)
		{
			for (i=0; i<nMBTilesCount; i++)
			{
				if(pHDS[i] != NULL)
					OGRReleaseDataSource(pHDS[i]);
			}

			delete []pHDS;
			pHDS = NULL;
		}

		if (pTileLevelInfo != NULL)
		{
			CPLFree(pTileLevelInfo);
			pTileLevelInfo = NULL;
		}
    }
    else if (poMainDS != NULL && bMustFree)
    {
        poMainDS->papoOverviews[nLevel-1] = NULL;
        delete poMainDS;
        poMainDS = NULL;
        bRet = TRUE;
    }

    return bRet;
}

/************************************************************************/
/*                  ComputeTileColTileRowZoomLevel()                    */
/************************************************************************/

void TfatMBTilesDataset::ComputeTileColTileRowZoomLevel(int nBlockXOff,
														int nBlockYOff,
														int &nTileColumn,
														int &nTileRow,
														int &nZoomLevel,
														int &nTileIndex)
{
	const int nBlockYSize = 256;

	int _nMinLevel = (poMainDS) ? poMainDS->nMinLevel : nMinLevel;
	int _nMinTileCol = (poMainDS) ? poMainDS->nMinTileCol : nMinTileCol;
	int _nMinTileRow = (poMainDS) ? poMainDS->nMinTileRow : nMinTileRow;
	_nMinTileCol >>= nLevel;

	nTileColumn = nBlockXOff + _nMinTileCol;
	nTileRow = (((nRasterYSize / nBlockYSize - 1 - nBlockYOff) << nLevel) + _nMinTileRow) >> nLevel;
	nZoomLevel = ((poMainDS) ? poMainDS->nResolutions : nResolutions) - nLevel + _nMinLevel;

	int nTileCount = 0, nIndex = 0;
	for (int i=0; i<nTileInfoCount; i++)
	{
		if(nZoomLevel > pTileLevelInfo[i].nLevelName )
		{
			nTileCount += pTileLevelInfo[i].nTileCount;
			nIndex ++;
		}
		else
			break;
	}
	
	int nMinRow = pTileLevelInfo[nIndex].nMinRow;
	int nMaxRow = pTileLevelInfo[nIndex].nMaxRow;
	int nMinColumn = pTileLevelInfo[nIndex].nMinColumn;
	int nMaxColumn = pTileLevelInfo[nIndex].nMaxColumn;

	if(nTileColumn < nMinColumn || nTileColumn > nMaxColumn ||
	   nTileRow < nMinRow || nTileRow > nMaxRow)
	{
		nTileIndex = 0;
		return;
	}

	int nColumnCount = nTileColumn - nMinColumn;

	nTileCount += (nColumnCount*(nMaxRow-nMinRow+1));

	nTileCount += (nTileRow - nMinRow +1);

	int nCount = nTileCount / nTileMaxCount;
	int nRemainder = nTileCount % nTileMaxCount;

	nTileIndex = (nRemainder==0) ? nCount-1 : nCount;
}

/************************************************************************/
/*                          GetGeoTransform()                           */
/************************************************************************/

#define MAX_GM 20037508.3427892	//半个赤道的长度

CPLErr TfatMBTilesDataset::GetGeoTransform(double* padfGeoTransform)
{
    int nMaxLevel = nMinLevel + nResolutions;
    if (nMaxLevel == 0)
    {
        padfGeoTransform[0] = -180;
        padfGeoTransform[1] = 360.0 / nRasterXSize;
        padfGeoTransform[2] = 0;
        padfGeoTransform[3] = 90;
        padfGeoTransform[4] = 0;
        padfGeoTransform[5] = -180.0 / nRasterYSize;
    }
    else
    {
        int nMaxTileRow = nMinTileRow + static_cast<int>(ceil(nRasterYSize / 256.0));

		double dStep = 360.0 / (1 << nMaxLevel);
		padfGeoTransform[0] = -180.0 + dStep * nMinTileCol;
		padfGeoTransform[1] = dStep / 256.0;
		padfGeoTransform[2] = 0;
		padfGeoTransform[3] = -90 + dStep * nMaxTileRow;
		padfGeoTransform[4] = 0;
		padfGeoTransform[5] = -1 * dStep / 256.0;
    }
	//if (nMaxLevel == 0)
	//{
	//	padfGeoTransform[0] = -MAX_GM;
	//	padfGeoTransform[1] = 2 * MAX_GM / nRasterXSize;
	//	padfGeoTransform[2] = 0;
	//	padfGeoTransform[3] = MAX_GM / 2;
	//	padfGeoTransform[4] = 0;
	//	padfGeoTransform[5] = -MAX_GM / nRasterYSize;
	//}
	//else
	//{
	//	int nMaxTileCol = nMinTileCol + nRasterXSize / 256;
	//	int nMaxTileRow = nMinTileRow + nRasterYSize / 256;
	//	int nMiddleTile = (1 << nMaxLevel) / 2;
	//	padfGeoTransform[0] = 2 * MAX_GM * (nMinTileCol - nMiddleTile) / (1 << nMaxLevel);
	//	padfGeoTransform[1] = 2 * MAX_GM * (nMaxTileCol - nMinTileCol) / (1 << nMaxLevel) / nRasterXSize;
	//	padfGeoTransform[2] = 0;
	//	padfGeoTransform[3] = MAX_GM * (nMaxTileRow - nMiddleTile) / (1 << nMaxLevel);
	//	padfGeoTransform[4] = 0;
	//	padfGeoTransform[5] = -MAX_GM * (nMaxTileRow - nMinTileRow) / (1 << nMaxLevel) / nRasterYSize;
	//}
    return CE_None;
}

/************************************************************************/
/*                         GetProjectionRef()                           */
/************************************************************************/

const char* TfatMBTilesDataset::GetProjectionRef()
{
	// Global Geodetic tiles are using geodetic coordinates (latitude, longitude)
	// directly as planar coordinates XY (it is also called Unprojected or Plate Carre). 
	return SRS_WKT_WGS84_LAT_LONG;
}

/************************************************************************/
/*                      GetMetadataDomainList()                         */
/************************************************************************/

char **TfatMBTilesDataset::GetMetadataDomainList()
{
    return BuildMetadataDomainList(GDALDataset::GetMetadataDomainList(),
                                   TRUE,
                                   "", NULL);
}

/************************************************************************/
/*                            GetMetadata()                             */
/************************************************************************/

char** TfatMBTilesDataset::GetMetadata( const char * pszDomain )
{
    if (pszDomain != NULL && !EQUAL(pszDomain, ""))
        return GDALPamDataset::GetMetadata(pszDomain);

    if (bFetchedMetadata)
        return aosList.List();
	else
       return NULL;
}

/************************************************************************/
/*                             Identify()                               */
/************************************************************************/

int TfatMBTilesDataset::Identify(GDALOpenInfo* poOpenInfo)
{
	if (EQUAL(CPLGetExtension(poOpenInfo->pszFilename), "XML") &&
		poOpenInfo->nHeaderBytes >= 1024 &&
		EQUALN((const char*)poOpenInfo->pabyHeader, "<?xml version=\"1.0\" encoding=\"utf-8\"?>", 38))
	{
		CPLXMLNode *psTree = CPLParseXMLString( poOpenInfo->pszFilename );
		if( psTree != NULL )
		{
			CPLDestroyXMLNode(psTree);
			return TRUE;
		}
		else
			return FALSE;
	}

	return FALSE;
}

/************************************************************************/
/*                        MBTilesCurlReadCbk()                          */
/************************************************************************/

/* We spy the data received by CURL for the initial request where we try */
/* to get a first tile to see its characteristics. We just need the header */
/* to determine that, so let's make VSICurl stop reading after we have found it */

static int MBTilesCurlReadCbk(CPL_UNUSED VSILFILE* fp,
                              void *pabyBuffer, size_t nBufferSize,
                              void* pfnUserData)
{
    const GByte abyPNGSig[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, /* PNG signature */
                                0x00, 0x00, 0x00, 0x0D, /* IHDR length */
                                0x49, 0x48, 0x44, 0x52  /* IHDR chunk */ };

    /* JPEG SOF0 (Start Of Frame 0) marker */
    const GByte abyJPEG1CompSig[] = { 0xFF, 0xC0, /* marker */
                                      0x00, 0x0B, /* data length = 8 + 1 * 3 */
                                      0x08,       /* depth : 8 bit */
                                      0x01, 0x00, /* width : 256 */
                                      0x01, 0x00, /* height : 256 */
                                      0x01        /* components : 1 */
                                    };
    const GByte abyJPEG3CompSig[] = { 0xFF, 0xC0, /* marker */
                                      0x00, 0x11, /* data length = 8 + 3 * 3 */
                                      0x08,       /* depth : 8 bit */
                                      0x01, 0x00, /* width : 256 */
                                      0x01, 0x00, /* width : 256 */
                                      0x03        /* components : 3 */
                                     };

    int i;
    for(i = 0; i < (int)nBufferSize - (int)sizeof(abyPNGSig); i++)
    {
        if (memcmp(((GByte*)pabyBuffer) + i, abyPNGSig, sizeof(abyPNGSig)) == 0 &&
            i + sizeof(abyPNGSig) + 4 + 4 + 1 + 1 < nBufferSize)
        {
            GByte* ptr = ((GByte*)(pabyBuffer)) + i + (int)sizeof(abyPNGSig);

            int nWidth;
            memcpy(&nWidth, ptr, 4);
            CPL_MSBPTR32(&nWidth);
            ptr += 4;

            int nHeight;
            memcpy(&nHeight, ptr, 4);
            CPL_MSBPTR32(&nHeight);
            ptr += 4;

            GByte nDepth = *ptr;
            ptr += 1;

            GByte nColorType = *ptr;
            CPLDebug("MBTILES", "PNG: nWidth=%d nHeight=%d depth=%d nColorType=%d",
                        nWidth, nHeight, nDepth, nColorType);

            int* pnBands = (int*) pfnUserData;
            *pnBands = -2;
            if (nWidth == 256 && nHeight == 256 && nDepth == 8)
            {
                if (nColorType == 0)
                    *pnBands = 1; /* Gray */
                else if (nColorType == 2)
                    *pnBands = 3; /* RGB */
                else if (nColorType == 3)
                {
                    /* This might also be a color table with transparency */
                    /* but we cannot tell ! */
                    *pnBands = -1;
                    return TRUE;
                }
                else if (nColorType == 4)
                    *pnBands = 2; /* Gray + alpha */
                else if (nColorType == 6)
                    *pnBands = 4; /* RGB + alpha */
            }

            return FALSE;
        }
    }

    for(i = 0; i < (int)nBufferSize - (int)sizeof(abyJPEG1CompSig); i++)
    {
        if (memcmp(((GByte*)pabyBuffer) + i, abyJPEG1CompSig, sizeof(abyJPEG1CompSig)) == 0)
        {
            CPLDebug("MBTILES", "JPEG: nWidth=%d nHeight=%d depth=%d nBands=%d",
                        256, 256, 8, 1);

            int* pnBands = (int*) pfnUserData;
            *pnBands = 1;

            return FALSE;
        }
        else if (memcmp(((GByte*)pabyBuffer) + i, abyJPEG3CompSig, sizeof(abyJPEG3CompSig)) == 0)
        {
            CPLDebug("MBTILES", "JPEG: nWidth=%d nHeight=%d depth=%d nBands=%d",
                        256, 256, 8, 3);

            int* pnBands = (int*) pfnUserData;
            *pnBands = 3;

            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/
/*                        MBTilesGetBandCount()                         */
/************************************************************************/

static
int MBTilesGetBandCount(OGRDataSourceH &hDS, CPL_UNUSED int nMinLevel, int nMaxLevel,
                        int nMinTileRow, int nMaxTileRow,
                        int nMinTileCol, int nMaxTileCol)
{
    OGRLayerH hSQLLyr;
    OGRFeatureH hFeat;
    const char* pszSQL;
    VSILFILE* fpCURLOGR = NULL;
    int bFirstSelect = TRUE;

    int nBands = -1;

    /* Small trick to get the VSILFILE associated with the OGR SQLite */
    /* DB */
    CPLString osDSName(OGR_DS_GetName(hDS));
    if (strncmp(osDSName.c_str(), "/vsicurl/", 9) == 0)
    {
        CPLErrorReset();
        CPLPushErrorHandler(CPLQuietErrorHandler);
        hSQLLyr = OGR_DS_ExecuteSQL(hDS, "GetVSILFILE()", NULL, NULL);
        CPLPopErrorHandler();
        CPLErrorReset();
        if (hSQLLyr != NULL)
        {
            hFeat = OGR_L_GetNextFeature(hSQLLyr);
            if (hFeat)
            {
                if (OGR_F_IsFieldSet(hFeat, 0))
                {
                    const char* pszPointer = OGR_F_GetFieldAsString(hFeat, 0);
                    fpCURLOGR = (VSILFILE* )CPLScanPointer( pszPointer, strlen(pszPointer) );
                }
                OGR_F_Destroy(hFeat);
            }
            OGR_DS_ReleaseResultSet(hDS, hSQLLyr);
        }
    }

    pszSQL = CPLSPrintf("SELECT tile_data FROM tiles WHERE "
                            "tile_column = %d AND tile_row = %d AND zoom_level = %d",
                            (nMinTileCol  + nMaxTileCol) / 2,
                            (nMinTileRow  + nMaxTileRow) / 2,
                            nMaxLevel);
    CPLDebug("MBTILES", "%s", pszSQL);

    if (fpCURLOGR)
    {
        /* Install a spy on the file connexion that will intercept */
        /* PNG or JPEG headers, to interrupt their downloading */
        /* once the header is found. Speeds up dataset opening. */
        CPLErrorReset();
        VSICurlInstallReadCbk(fpCURLOGR, MBTilesCurlReadCbk, &nBands, TRUE);

        CPLErrorReset();
        CPLPushErrorHandler(CPLQuietErrorHandler);
        hSQLLyr = OGR_DS_ExecuteSQL(hDS, pszSQL, NULL, NULL);
        CPLPopErrorHandler();

        VSICurlUninstallReadCbk(fpCURLOGR);

        /* Did the spy intercept something interesting ? */
        if (nBands != -1)
        {
            CPLErrorReset();

            OGR_DS_ReleaseResultSet(hDS, hSQLLyr);
            hSQLLyr = NULL;

            /* Re-open OGR SQLite DB, because with our spy we have simulated an I/O error */
            /* that SQLite will have difficulies to recover within the existing connection */
            /* No worry ! This will be fast because the /vsicurl/ cache has cached the already */
            /* read blocks */
            OGRReleaseDataSource(hDS);
            hDS = OGROpen(osDSName.c_str(), FALSE, NULL);
            if (hDS == NULL)
                return -1;

            /* Unrecognized form of PNG. Error out */
            if (nBands <= 0)
                return -1;

            return nBands;
        }
        else if (CPLGetLastErrorType() == CE_Failure)
        {
            CPLError(CE_Failure, CPLGetLastErrorNo(), "%s", CPLGetLastErrorMsg());
        }
    }
    else
    {
        hSQLLyr = OGR_DS_ExecuteSQL(hDS, pszSQL, NULL, NULL);
    }

    while( TRUE )
    {
        if (hSQLLyr == NULL && bFirstSelect)
        {
            bFirstSelect = FALSE;
            pszSQL = CPLSPrintf("SELECT tile_data FROM tiles WHERE "
                                "zoom_level = %d LIMIT 1", nMaxLevel);
            CPLDebug("MBTILES", "%s", pszSQL);
            hSQLLyr = OGR_DS_ExecuteSQL(hDS, pszSQL, NULL, NULL);
            if (hSQLLyr == NULL)
                return -1;
        }

        hFeat = OGR_L_GetNextFeature(hSQLLyr);
        if (hFeat == NULL)
        {
            OGR_DS_ReleaseResultSet(hDS, hSQLLyr);
            hSQLLyr = NULL;
            if( !bFirstSelect )
                return -1;
        }
        else
            break;
    }

    CPLString osMemFileName;
    osMemFileName.Printf("/vsimem/%p", hSQLLyr);

    int nDataSize = 0;
    GByte* pabyData = OGR_F_GetFieldAsBinary(hFeat, 0, &nDataSize);

    VSIFCloseL(VSIFileFromMemBuffer( osMemFileName.c_str(), pabyData,
                                    nDataSize, FALSE));

	GDALDatasetH hDSTile = GDALOpenEx(osMemFileName.c_str(), GDAL_OF_RASTER,
		apszAllowedDrivers, NULL, NULL);
    if (hDSTile == NULL)
    {
        VSIUnlink(osMemFileName.c_str());
        OGR_F_Destroy(hFeat);
        OGR_DS_ReleaseResultSet(hDS, hSQLLyr);
        return -1;
    }

    nBands = GDALGetRasterCount(hDSTile);

    if ((nBands != 1 && nBands != 3 && nBands != 4) ||
        GDALGetRasterXSize(hDSTile) != 256 ||
        GDALGetRasterYSize(hDSTile) != 256 ||
        GDALGetRasterDataType(GDALGetRasterBand(hDSTile, 1)) != GDT_Byte)
    {
        CPLError(CE_Failure, CPLE_NotSupported, "Unsupported tile characteristics");
        GDALClose(hDSTile);
        VSIUnlink(osMemFileName.c_str());
        OGR_F_Destroy(hFeat);
        OGR_DS_ReleaseResultSet(hDS, hSQLLyr);
        return -1;
    }

    GDALColorTableH hCT = GDALGetRasterColorTable(GDALGetRasterBand(hDSTile, 1));
    if (nBands == 1 && hCT != NULL)
    {
        nBands = 3;
        if( GDALGetColorEntryCount(hCT) > 0 )
        {
            /* Typical of paletted PNG with transparency */
            const GDALColorEntry* psEntry = GDALGetColorEntry( hCT, 0 );
            if( psEntry->c4 == 0 )
                nBands = 4;
        }
    }

    GDALClose(hDSTile);
    VSIUnlink(osMemFileName.c_str());
    OGR_F_Destroy(hFeat);
    OGR_DS_ReleaseResultSet(hDS, hSQLLyr);

    return nBands;
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

GDALDataset* TfatMBTilesDataset::Open(GDALOpenInfo* poOpenInfo)
{
    CPLString osFileName;
    CPLString osTableName;

    if (!Identify(poOpenInfo))
        return NULL;

/* -------------------------------------------------------------------- */
/*	Try to read the whole file into memory.				                */
/* -------------------------------------------------------------------- */
	char *pszXML = NULL;

	VSILFILE *fp = VSIFOpenL(poOpenInfo->pszFilename, "rb");
	if( fp == NULL )
	{
		CPLError( CE_Failure, CPLE_AppDefined, "can't open TileMetadata file." );
		return NULL;
	}

	unsigned int nLength;

	VSIFSeekL( fp, 0, SEEK_END );
	nLength = (int) VSIFTellL( fp );
	VSIFSeekL( fp, 0, SEEK_SET );

	nLength = MAX(0,nLength);
	pszXML = (char *) VSIMalloc(nLength+1);

	if( pszXML == NULL )
	{
		VSIFCloseL(fp);
		CPLError( CE_Failure, CPLE_OutOfMemory, "Failed to allocate %d byte "
			"buffer to hold TileMetadata xml file.", nLength );
		return NULL;
	}

	if( VSIFReadL( pszXML, 1, nLength, fp ) != nLength )
	{
		VSIFCloseL(fp);
		CPLFree( pszXML );
		CPLError( CE_Failure, CPLE_FileIO, "Failed to read %d bytes from "
			"TileMetadata xml file.", nLength );
		return NULL;
	}

	pszXML[nLength] = '\0';
	VSIFCloseL(fp);

	CPLXMLNode *psTree = CPLParseXMLString( pszXML );
	CPLFree( pszXML );

	CPLXMLNode *psRoot = CPLGetXMLNode( psTree, "=TileMetadata" );
	if (psRoot == NULL)
	{
		CPLError( CE_Failure, CPLE_AppDefined,
			"Missing TileMetadata element." );
		CPLDestroyXMLNode( psTree );
		return NULL;
	}

	if( CPLGetXMLNode( psRoot, "TileFileCount" ) == NULL
		|| CPLGetXMLNode( psRoot, "MinZoom" ) == NULL
		|| CPLGetXMLNode( psRoot, "MaxZoom" ) == NULL )
	{
		CPLError( CE_Failure, CPLE_AppDefined, 
			"Missing one of TileFileCount, MinZoom or MaxZoom on TileMetadata." );
		CPLDestroyXMLNode( psTree );
		return NULL;
	}

	int nMBTilesCount = atoi(CPLGetXMLValue(psRoot,"TileFileCount","0"));
	if (nMBTilesCount <=0)
	{
		CPLError( CE_Failure, CPLE_AppDefined, 
			"TileFileCount less than or equal to 0 on TileMetadata." );
		CPLDestroyXMLNode( psTree );
		return NULL;
	}
	
/* -------------------------------------------------------------------- */
/*      Open underlying OGR DB                                          */
/* -------------------------------------------------------------------- */
	TfatMBTilesDataset* poDS = NULL;

	if (OGRGetDriverCount() == 0)
		OGRRegisterAll();

	OGRDataSourceH *pHDS = NULL;
	pHDS = new OGRDataSourceH[nMBTilesCount];
	memset(pHDS, 0, sizeof(OGRDataSourceH)*nMBTilesCount);

/* -------------------------------------------------------------------- */
/*      constructing MBTiles filenames                                  */
/* -------------------------------------------------------------------- */
	char **papszMBTilesName = NULL;
	const char* pszBaseName = CPLGetXMLValue(psRoot, "LayerName", "");
	if(EQUAL(pszBaseName, ""))
		pszBaseName = CPLGetBasename(poOpenInfo->pszFilename);

	for (int i=0; i<nMBTilesCount; i++)
	{
		const char* pszDirName = CPLGetDirname(poOpenInfo->pszFilename);
		const char* pszFileName = CPLSPrintf("%s\\%s_%d.mbtiles", pszDirName, pszBaseName, i);
		papszMBTilesName = CSLAddString(papszMBTilesName, pszFileName);

		pHDS[i] = OGROpen(pszFileName, FALSE, NULL);
		if(pHDS[i] == NULL)
		{
			CPLError(CE_Failure, CPLE_AppDefined, "Cannot open file '%s'", pszFileName);
			goto end;
		}

		OGRLayerH hRasterLyr = OGR_DS_GetLayerByName(pHDS[i], "tiles");
		if (hRasterLyr == NULL)
		{
			CPLError(CE_Failure, CPLE_AppDefined, "Cannot find tiles table in file '%s'", pszFileName);
			goto end;
		}
	}
	
/* -------------------------------------------------------------------- */
/*      Build dataset                                                   */
/* -------------------------------------------------------------------- */
	
	TileLevelInfo *pTileInfo = NULL;
	int nTileInfoCount = 0;

	{
/* -------------------------------------------------------------------- */
/*      Get minimum and maximum zoom levels                             */
/* -------------------------------------------------------------------- */

		int nMinLevel = -1, nMaxLevel = -1;
		int nTileMaxCount = 20000;
		nMinLevel = atoi(CPLGetXMLValue(psRoot, "MinZoom", "-1"));
		nMaxLevel = atoi(CPLGetXMLValue(psRoot, "MaxZoom", "-1"));
		nTileMaxCount = atoi(CPLGetXMLValue(psRoot, "TileMaxCount", "-1"));

/* -------------------------------------------------------------------- */
/*      Get bounds                                                      */
/* -------------------------------------------------------------------- */

		CPLXMLNode *pTileLevels = CPLGetXMLNode(psRoot, "TileLevels");
		CPLXMLNode *pLevelChild = pTileLevels->psChild;

		while(pLevelChild != NULL)
		{
			nTileInfoCount++;
			pTileInfo = (TileLevelInfo *) CPLRealloc( pTileInfo, sizeof(TileLevelInfo) * nTileInfoCount );
			//memset(pTileInfo[nTileInfoCount-1], 0, sizeof(TileLevelInfo));

			pTileInfo[nTileInfoCount-1].nLevelName = atoi(CPLGetXMLValue(pLevelChild, "LevelName", "-1"));
			pTileInfo[nTileInfoCount-1].nMinRow = atoi(CPLGetXMLValue(pLevelChild, "MinRow", "-1"));
			pTileInfo[nTileInfoCount-1].nMaxRow = atoi(CPLGetXMLValue(pLevelChild, "MaxRow", "-1"));
			pTileInfo[nTileInfoCount-1].nMinColumn = atoi(CPLGetXMLValue(pLevelChild, "MinColumn", "-1"));
			pTileInfo[nTileInfoCount-1].nMaxColumn = atoi(CPLGetXMLValue(pLevelChild, "MaxColumn", "-1"));
			pTileInfo[nTileInfoCount-1].nTileCount = atoi(CPLGetXMLValue(pLevelChild, "TileCount", "-1"));

			pLevelChild = pLevelChild->psNext;
		}

		int nMinTileRow = 0, nMaxTileRow = 0, nMinTileCol = 0, nMaxTileCol = 0;

		TileLevelInfo oMaxInfo = pTileInfo[nTileInfoCount-1];
		nMinTileRow = oMaxInfo.nMinRow;
		nMaxTileRow = oMaxInfo.nMaxRow;
		nMinTileCol = oMaxInfo.nMinColumn;
		nMaxTileCol = oMaxInfo.nMaxColumn;

        if (nMinTileRow == -1 || nMaxTileRow == -1 ||
			nMinTileCol == -1 || nMaxTileCol == -1 ||
			nTileMaxCount == -1)
        {
            CPLError(CE_Failure, CPLE_AppDefined, "Cannot find min and max tile numbers");
            goto end;
        }

/* -------------------------------------------------------------------- */
/*      Get number of bands                                             */
/* -------------------------------------------------------------------- */

		int nBands = 0;
		nBands = MBTilesGetBandCount(pHDS[nMBTilesCount-1], nMinLevel, nMaxLevel,
			nMinTileRow, nMaxTileRow, nMinTileCol, nMaxTileCol);

		if (nBands <= 0)
			goto end;

/* -------------------------------------------------------------------- */
/*      Set dataset attributes                                          */
/* -------------------------------------------------------------------- */

        poDS = new TfatMBTilesDataset();
        poDS->eAccess = poOpenInfo->eAccess;
		poDS->nTileInfoCount = nTileInfoCount;
		poDS->pTileLevelInfo = pTileInfo;
		poDS->nMBTilesCount = nMBTilesCount;
		poDS->pHDS = pHDS;
		poDS->nTileMaxCount = nTileMaxCount;
		
		/* poDS will release it from now */
		pTileInfo = NULL;
		pHDS = NULL;

/* -------------------------------------------------------------------- */
/*      Store resolutions                                               */
/* -------------------------------------------------------------------- */
		poDS->nMinLevel = nMinLevel;
		int nResolutions;
        poDS->nResolutions = nResolutions = nMaxLevel - nMinLevel;

/* -------------------------------------------------------------------- */
/*      Round bounds to the lowest zoom level                           */
/* -------------------------------------------------------------------- */

        CPLDebug("MBTILES", "%d %d %d %d", nMinTileCol, nMinTileRow, nMaxTileCol, nMaxTileRow);
        nMinTileCol = (int)(1.0 * nMinTileCol / (1 << nResolutions)) * (1 << nResolutions);
        nMinTileRow = (int)(1.0 * nMinTileRow / (1 << nResolutions)) * (1 << nResolutions);
        nMaxTileCol = (int)ceil(1.0 * nMaxTileCol / (1 << nResolutions)) * (1 << nResolutions);
        nMaxTileRow = (int)ceil(1.0 * nMaxTileRow / (1 << nResolutions)) * (1 << nResolutions);

/* -------------------------------------------------------------------- */
/*      Compute raster size, geotransform and projection                */
/* -------------------------------------------------------------------- */
        poDS->nMinTileCol = nMinTileCol;
        poDS->nMinTileRow = nMinTileRow;
        poDS->nRasterXSize = (nMaxTileCol-nMinTileCol) * 256;
        poDS->nRasterYSize = (nMaxTileRow-nMinTileRow) * 256;

		int iBand, nBlockXSize, nBlockYSize;
		GDALDataType eDataType = GDT_Byte;
        nBlockXSize = nBlockYSize = 256;

/* -------------------------------------------------------------------- */
/*      Add bands                                                       */
/* -------------------------------------------------------------------- */

        for(iBand=0;iBand<nBands;iBand++)
            poDS->SetBand(iBand+1, new TfatMBTilesBand(poDS, iBand+1, eDataType,
                                                  nBlockXSize, nBlockYSize));

/* -------------------------------------------------------------------- */
/*      Add overview levels as internal datasets                        */
/* -------------------------------------------------------------------- */
        if (nResolutions >= 1)
        {
            poDS->papoOverviews = (TfatMBTilesDataset**)
                CPLCalloc(nResolutions, sizeof(TfatMBTilesDataset*));
            int nLev;
            for(nLev=1;nLev<=nResolutions;nLev++)
            {
                poDS->papoOverviews[nLev-1] = new TfatMBTilesDataset(poDS, nLev);

                for(iBand=0;iBand<nBands;iBand++)
                {
                    poDS->papoOverviews[nLev-1]->SetBand(iBand+1,
                        new TfatMBTilesBand(poDS->papoOverviews[nLev-1], iBand+1, eDataType,
                                           nBlockXSize, nBlockYSize));
                }
            }
        }

        poDS->SetMetadata(poDS->papszImageStructure, "IMAGE_STRUCTURE");

/* -------------------------------------------------------------------- */
/*      Initialize any PAM information.                                 */
/* -------------------------------------------------------------------- */
        poDS->SetDescription( poOpenInfo->pszFilename );

        if ( !EQUALN(poOpenInfo->pszFilename, "/vsicurl/", 9) )
            poDS->TryLoadXML();
        else
        {
            poDS->SetPamFlags(poDS->GetPamFlags() & ~GPF_DIRTY);
        }
    }

end:
	CSLDestroy(papszMBTilesName);

	if (pHDS != NULL)
	{
		for (int i=0; i<nMBTilesCount; i++)
		{
			if(pHDS[i] != NULL)
				OGRReleaseDataSource(pHDS[i]);
		}

		delete []pHDS;
		pHDS = NULL;
	}

	if (pTileInfo != NULL)
	{
		CPLFree(pTileInfo);
		pTileInfo = NULL;
	}

    return poDS;
}

/************************************************************************/
/*                     GDALRegister_TFATMBTiles()                       */
/************************************************************************/

void GDALRegister_TFATMBTiles()

{
	if ( !GDAL_CHECK_VERSION("21At MBTiles driver") )
		return;

	if( GDALGetDriverByName( "21At MBTiles" ) != NULL )
		return;

	GDALDriver  *poDriver = new GDALDriver();

	poDriver->SetDescription( "21At MBTiles" );
	poDriver->SetMetadataItem( GDAL_DCAP_RASTER, "YES" );
	poDriver->SetMetadataItem( GDAL_DMD_LONGNAME, "21At MBTiles" );
	poDriver->SetMetadataItem( GDAL_DMD_HELPTOPIC, "frmt_21atmbtiles.html" );
	poDriver->SetMetadataItem( GDAL_DMD_EXTENSION, "mbtiles" );

	poDriver->SetMetadataItem( GDAL_DCAP_VIRTUALIO, "YES" );

	poDriver->pfnOpen = TfatMBTilesDataset::Open;
	poDriver->pfnIdentify = TfatMBTilesDataset::Identify;

	GetGDALDriverManager()->RegisterDriver( poDriver );
}
