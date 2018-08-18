/******************************************************************************
* $Id$
*
* Project:  Landsat 7/8 _MTL.TXT Driver
* Purpose:  Implementation of the LandsatDataset class.
* Author:   Minlu Li, liminlu0314@163.com
*
******************************************************************************
* Copyright (c) 2015, Minlu Li
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

#include "gdal_pam.h"
#include "gdal_proxy.h"
#include "ogr_spatialref.h"
#include "cpl_string.h"
#include "vrtdataset.h"
#include "cpl_multiproc.h"
#include "cplkeywordparser.h"

enum Satellite  // Satellites:
{
	LANDSAT7,	// Landsat 7
	LANDSAT8	// Landsat 8
};

/************************************************************************/
/* ==================================================================== */
/*			                   	LandsatDataset		             		*/
/* ==================================================================== */
/************************************************************************/

class CPL_DLL LandsatDataset : public GDALPamDataset
{
	VRTDataset *poVRTDS;
	std::vector<GDALDataset *> apoTifDS;

protected:
	virtual int         CloseDependentDatasets();

public:
	LandsatDataset();
	~LandsatDataset();

	virtual char **GetFileList(void);

	static GDALDataset *Open( GDALOpenInfo * );
	static int Identify( GDALOpenInfo *poOpenInfo );
	static int ParserLandsat7( const char* pszMtlFile, char **papszMetaInfo, int nSubdataset, LandsatDataset **ppoDS );
	static int ParserLandsat8( const char* pszMtlFile, char **papszMetaInfo, int nSubdataset, LandsatDataset **ppoDS );
};

/************************************************************************/
/* ==================================================================== */
/*                          LandSatRasterBand                           */
/* ==================================================================== */
/************************************************************************/

class LandSatRasterBand : public GDALPamRasterBand
{
	friend class LandsatDataset;

	GDALRasterBand *poVRTBand;

public:
	LandSatRasterBand( LandsatDataset *, int, GDALRasterBand * );
	virtual       ~LandSatRasterBand() {};

	virtual CPLErr IReadBlock( int, int, void * );
	virtual CPLErr IRasterIO( GDALRWFlag, int, int, int, int,
		void *, int, int, GDALDataType,
		GSpacing, GSpacing, GDALRasterIOExtraArg* psExtraArg );
};

/************************************************************************/
/*                         LandSatRasterBand()                          */
/************************************************************************/

LandSatRasterBand::LandSatRasterBand( LandsatDataset *poTILDS, int nBand, 
									 GDALRasterBand *poVRTBand )

{
	this->poDS = poTILDS;
	this->poVRTBand = poVRTBand;
	this->nBand = nBand;
	this->eDataType = poVRTBand->GetRasterDataType();

	poVRTBand->GetBlockSize( &nBlockXSize, &nBlockYSize );
}

/************************************************************************/
/*                             IReadBlock()                             */
/************************************************************************/

CPLErr LandSatRasterBand::IReadBlock( int iBlockX, int iBlockY, void *pBuffer )

{
	return poVRTBand->ReadBlock( iBlockX, iBlockY, pBuffer );
}

/************************************************************************/
/*                             IRasterIO()                              */
/************************************************************************/

CPLErr LandSatRasterBand::IRasterIO( GDALRWFlag eRWFlag,
									int nXOff, int nYOff, int nXSize, int nYSize,
									void * pData, int nBufXSize, int nBufYSize,
									GDALDataType eBufType,
									GSpacing nPixelSpace, GSpacing nLineSpace,
									GDALRasterIOExtraArg* psExtraArg )

{
	if(GetOverviewCount() > 0)
	{
		return GDALPamRasterBand::IRasterIO( eRWFlag, nXOff, nYOff, nXSize, nYSize,
			pData, nBufXSize, nBufYSize, eBufType,
			nPixelSpace, nLineSpace, NULL );
	}
	else //if not exist TIL overviews, try to use band source overviews
	{
		return poVRTBand->RasterIO( eRWFlag, nXOff, nYOff, nXSize, nYSize,
			pData, nBufXSize, nBufYSize, eBufType,
			nPixelSpace, nLineSpace, NULL );
	}
}

/************************************************************************/
/*                           LandsatDataset()                           */
/************************************************************************/

LandsatDataset::LandsatDataset()

{
	poVRTDS = NULL;
}

/************************************************************************/
/*                          ~LandsatDataset()                           */
/************************************************************************/

LandsatDataset::~LandsatDataset()

{
	CloseDependentDatasets();
}

/************************************************************************/
/*                        CloseDependentDatasets()                      */
/************************************************************************/

int LandsatDataset::CloseDependentDatasets()
{
	int bHasDroppedRef = GDALPamDataset::CloseDependentDatasets();

	if( poVRTDS )
	{
		bHasDroppedRef = TRUE;
		delete poVRTDS;
		poVRTDS = NULL;
	}

	while( !apoTifDS.empty() )
	{
		GDALClose( (GDALDatasetH) apoTifDS.back() );
		apoTifDS.pop_back();
	}

	return bHasDroppedRef;
}

/************************************************************************/
/*                              Identify()                              */
/************************************************************************/

int LandsatDataset::Identify( GDALOpenInfo *poOpenInfo )

{
	if( EQUALN(poOpenInfo->pszFilename,"LANDSAT:",8) )	//subdataset
		return TRUE;

	const char* pszExt = CPLGetExtension(poOpenInfo->pszFilename);
	if( !EQUAL(pszExt,"TXT") && !EQUAL(pszExt,"MET"))
		return FALSE;

	if( strstr((const char *) poOpenInfo->pabyHeader,"GROUP = L1_METADATA_FILE") == NULL )
		return FALSE;
	else
		return TRUE;
}

int LandsatDataset::ParserLandsat7( const char* pszMtlFile, char **papszMetaInfo, int nSubdataset, LandsatDataset **ppoDS )
{
	LandsatDataset *poDS = (LandsatDataset*)(*ppoDS);

	double dfCellSize = 0.0;
	char **papszRasterName = NULL;
	if(nSubdataset == 0)
	{
		dfCellSize = CPLAtof(CSLFetchNameValueDef(papszMetaInfo, "PROJECTION_PARAMETERS.GRID_CELL_SIZE_PAN", "0"));
		poDS->nRasterXSize = atoi(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_SAMPLES_PAN","0"));
		poDS->nRasterYSize = atoi(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_LINES_PAN","0"));
		papszRasterName = CSLAddString(papszRasterName, "BAND8");
	}
	else if(nSubdataset ==1)
	{
		dfCellSize = CPLAtof(CSLFetchNameValueDef(papszMetaInfo, "PROJECTION_PARAMETERS.GRID_CELL_SIZE_REF", "0"));
		poDS->nRasterXSize = atoi(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_SAMPLES_REF","0"));
		poDS->nRasterYSize = atoi(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_LINES_REF","0"));
		papszRasterName = CSLAddString(papszRasterName, "BAND1");
		papszRasterName = CSLAddString(papszRasterName, "BAND2");
		papszRasterName = CSLAddString(papszRasterName, "BAND3");
		papszRasterName = CSLAddString(papszRasterName, "BAND4");
		papszRasterName = CSLAddString(papszRasterName, "BAND5");
		papszRasterName = CSLAddString(papszRasterName, "BAND7");
	}
	else
	{
		dfCellSize = CPLAtof(CSLFetchNameValueDef(papszMetaInfo, "PROJECTION_PARAMETERS.GRID_CELL_SIZE_THM", "0"));
		poDS->nRasterXSize = atoi(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_SAMPLES_THM","0"));
		poDS->nRasterYSize = atoi(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_LINES_THM","0"));
		papszRasterName = CSLAddString(papszRasterName, "BAND61");
		papszRasterName = CSLAddString(papszRasterName, "BAND62");
	}

	if (!GDALCheckDatasetDimensions(poDS->nRasterXSize, poDS->nRasterYSize))
	{
		CSLDestroy(papszRasterName);
		return FALSE;
	}

	OGRSpatialReference oSRS;
	const char* pszMapProjection = CSLFetchNameValue(papszMetaInfo, "PROJECTION_PARAMETERS.MAP_PROJECTION");
	const char* pszDatum = NULL;
	const char* pszEllipsoid = NULL;
	const char* pszZone = NULL;

	if(strstr(pszMapProjection, "UTM") != NULL)
	{
		pszDatum = CSLFetchNameValue(papszMetaInfo, "PROJECTION_PARAMETERS.REFERENCE_DATUM");
		pszEllipsoid = CSLFetchNameValue(papszMetaInfo, "PROJECTION_PARAMETERS.REFERENCE_ELLIPSOID");
		pszZone = CSLFetchNameValue(papszMetaInfo, "UTM_PARAMETERS.ZONE_NUMBER");

		// trim double quotes. 
		if( pszDatum[0] == '"' )
			pszDatum++;
		if( pszDatum[strlen(pszDatum)-1] == '"' )
			((char *) pszDatum)[strlen(pszDatum)-1] = '\0';

		oSRS.SetWellKnownGeogCS(pszDatum);
		oSRS.SetUTM(atoi(pszZone));
	}

	char *pszWkt = NULL;
	if(oSRS.exportToWkt(&pszWkt) == OGRERR_NONE)
		poDS->SetProjection(pszWkt);

	double dfUL_X = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_UL_CORNER_MAPX","0"));
	double dfUL_Y = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_UL_CORNER_MAPY","0"));
	double dfUR_X = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_UR_CORNER_MAPX","0"));
	double dfUR_Y = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_UR_CORNER_MAPY","0"));
	double dfLL_X = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_LL_CORNER_MAPX","0"));
	double dfLL_Y = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_LL_CORNER_MAPY","0"));
	double dfLR_X = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_LR_CORNER_MAPX","0"));
	double dfLR_Y = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_LR_CORNER_MAPY","0"));

	double adfGeoTransform[6] = {dfUL_X, dfCellSize, 0, dfUL_Y, 0, -1*dfCellSize};
	poDS->SetGeoTransform(adfGeoTransform);

	/* -------------------------------------------------------------------- */
	/*      We need to open one of the images in order to establish         */
	/*      details like the band count and types.                          */
	/* -------------------------------------------------------------------- */
	GDALDataset *poTemplateDS = NULL;

	CPLString ostrFileName;
	ostrFileName.Printf( "L1_METADATA_FILE.%s_FILE_NAME", papszRasterName[0] );
	const char *pszFilename = CSLFetchNameValue( papszMetaInfo, ostrFileName.c_str() );
	if( pszFilename == NULL )
	{
		CPLError( CE_Failure, CPLE_AppDefined, "Missing %s in .TXT file.", ostrFileName.c_str() );
		CSLDestroy(papszRasterName);
		return FALSE;
	}

	CPLString osDirname = CPLGetDirname(pszMtlFile);

	// trim double quotes. 
	if( pszFilename[0] == '"' )
		pszFilename++;
	if( pszFilename[strlen(pszFilename)-1] == '"' )
		((char *) pszFilename)[strlen(pszFilename)-1] = '\0';

	const char* pszSubFile = CPLFormFilename(osDirname, pszFilename, NULL);
	poTemplateDS = (GDALDataset *) GDALOpen( pszSubFile, GA_ReadOnly );
	if( poTemplateDS == NULL || poTemplateDS->GetRasterCount() == 0)
	{
		if (poTemplateDS != NULL)
			GDALClose( poTemplateDS );

		CSLDestroy(papszRasterName);
		return FALSE;
	}

	poDS->SetProjection(poTemplateDS->GetProjectionRef());
	poTemplateDS->GetGeoTransform(adfGeoTransform);
	poDS->SetGeoTransform(adfGeoTransform);

	GDALRasterBand *poTemplateBand = poTemplateDS->GetRasterBand(1);
	GDALDataType eDT = poTemplateBand->GetRasterDataType();
	int          nBandCount = CSLCount(papszRasterName);

	poTemplateBand = NULL;
	GDALClose( poTemplateDS );

	/* -------------------------------------------------------------------- */
	/*      Create and initialize the corresponding VRT dataset used to     */
	/*      manage the tiled data access.                                   */
	/* -------------------------------------------------------------------- */
	poDS->poVRTDS = new VRTDataset(poDS->nRasterXSize, poDS->nRasterYSize);
	int iBand = 0;
	for( iBand = 0; iBand < nBandCount; iBand++ )
		poDS->poVRTDS->AddBand( eDT, NULL );

	/* Don't try to write a VRT file */
	poDS->poVRTDS->SetWritable(FALSE);

	/* -------------------------------------------------------------------- */
	/*      Create band information objects.                                */
	/* -------------------------------------------------------------------- */
	for( iBand = 0; iBand < nBandCount; iBand++ )
	{
		ostrFileName.Printf( "L1_METADATA_FILE.%s_FILE_NAME", papszRasterName[iBand] );
		pszFilename = CSLFetchNameValue( papszMetaInfo, ostrFileName.c_str() );
		if( pszFilename == NULL )
		{
			CPLError( CE_Failure, CPLE_AppDefined, "Missing %s in .TXT file.", ostrFileName.c_str() );
			CSLDestroy(papszRasterName);
			return FALSE;
		}

		// trim double quotes. 
		if( pszFilename[0] == '"' )
			pszFilename++;
		if( pszFilename[strlen(pszFilename)-1] == '"' )
			((char *) pszFilename)[strlen(pszFilename)-1] = '\0';

		pszSubFile = CPLFormFilename(osDirname, pszFilename, NULL);
		GDALDataset *poBandDS = (GDALDataset *) GDALOpen( pszSubFile, GA_ReadOnly );
		if( poBandDS == NULL || poBandDS->GetRasterCount() == 0)
		{
			if (poBandDS != NULL)
				GDALClose( poBandDS );

			CSLDestroy(papszRasterName);
			return FALSE;
		}

		poDS->apoTifDS.push_back( poBandDS );

		GDALRasterBand *pBand = poBandDS->GetRasterBand(1);
		LandSatRasterBand *pLandsatBand = new LandSatRasterBand( poDS, iBand+1, pBand);
		char** papszBandMeta = NULL;
		papszBandMeta = CSLAddString(papszBandMeta, pszFilename);
		pLandsatBand->SetMetadata(papszBandMeta);
		poDS->SetBand( iBand+1, pLandsatBand);
	}

	CSLDestroy(papszRasterName);
	return TRUE;
}

int LandsatDataset::ParserLandsat8( const char* pszMtlFile, char **papszMetaInfo, int nSubdataset, LandsatDataset **ppoDS )
{
	LandsatDataset *poDS = (LandsatDataset*)(*ppoDS);

	double dfCellSize = 0.0;
	char **papszRasterName = NULL;
	if(nSubdataset == 0)
	{
		dfCellSize = CPLAtof(CSLFetchNameValueDef(papszMetaInfo, "L1_METADATA_FILE.PROJECTION_PARAMETERS.GRID_CELL_SIZE_PANCHROMATIC", "0"));
		poDS->nRasterXSize = atoi(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.PANCHROMATIC_SAMPLES","0"));
		poDS->nRasterYSize = atoi(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.PANCHROMATIC_LINES","0"));
		papszRasterName = CSLAddString(papszRasterName, "BAND_8");
	}
	else if(nSubdataset ==1)
	{
		dfCellSize = CPLAtof(CSLFetchNameValueDef(papszMetaInfo, "L1_METADATA_FILE.PROJECTION_PARAMETERS.GRID_CELL_SIZE_REFLECTIVE", "0"));
		poDS->nRasterXSize = atoi(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.REFLECTIVE_SAMPLES","0"));
		poDS->nRasterYSize = atoi(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.REFLECTIVE_LINES","0"));
		papszRasterName = CSLAddString(papszRasterName, "BAND_1");
		papszRasterName = CSLAddString(papszRasterName, "BAND_2");
		papszRasterName = CSLAddString(papszRasterName, "BAND_3");
		papszRasterName = CSLAddString(papszRasterName, "BAND_4");
		papszRasterName = CSLAddString(papszRasterName, "BAND_5");
		papszRasterName = CSLAddString(papszRasterName, "BAND_6");
		papszRasterName = CSLAddString(papszRasterName, "BAND_7");
		papszRasterName = CSLAddString(papszRasterName, "BAND_9");
	}
	else if(nSubdataset ==2)
	{
		dfCellSize = CPLAtof(CSLFetchNameValueDef(papszMetaInfo, "L1_METADATA_FILE.PROJECTION_PARAMETERS.GRID_CELL_SIZE_THERMAL", "0"));
		poDS->nRasterXSize = atoi(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.THERMAL_SAMPLES","0"));
		poDS->nRasterYSize = atoi(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.THERMAL_LINES","0"));
		papszRasterName = CSLAddString(papszRasterName, "BAND_10");
		papszRasterName = CSLAddString(papszRasterName, "BAND_11");
	}
	else
	{
		dfCellSize = CPLAtof(CSLFetchNameValueDef(papszMetaInfo, "L1_METADATA_FILE.PROJECTION_PARAMETERS.GRID_CELL_SIZE_THERMAL", "0"));
		poDS->nRasterXSize = atoi(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.THERMAL_SAMPLES","0"));
		poDS->nRasterYSize = atoi(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.THERMAL_LINES","0"));
		papszRasterName = CSLAddString(papszRasterName, "BAND_QUALITY");
	}

	if (!GDALCheckDatasetDimensions(poDS->nRasterXSize, poDS->nRasterYSize))
	{
		CSLDestroy(papszRasterName);
		return FALSE;
	}

	OGRSpatialReference oSRS;
	const char* pszMapProjection = CSLFetchNameValue(papszMetaInfo, "L1_METADATA_FILE.PROJECTION_PARAMETERS.MAP_PROJECTION");
	const char* pszDatum = NULL;
	const char* pszEllipsoid = NULL;
	const char* pszZone = NULL;

	if(strstr(pszMapProjection, "UTM") != NULL)
	{
		pszDatum = CSLFetchNameValue(papszMetaInfo, "L1_METADATA_FILE.PROJECTION_PARAMETERS.DATUM");
		pszEllipsoid = CSLFetchNameValue(papszMetaInfo, "L1_METADATA_FILE.PROJECTION_PARAMETERS.ELLIPSOID");
		pszZone = CSLFetchNameValue(papszMetaInfo, "L1_METADATA_FILE.PROJECTION_PARAMETERS.UTM_ZONE");

		// trim double quotes. 
		if( pszDatum[0] == '"' )
			pszDatum++;
		if( pszDatum[strlen(pszDatum)-1] == '"' )
			((char *) pszDatum)[strlen(pszDatum)-1] = '\0';

		oSRS.SetWellKnownGeogCS(pszDatum);
		oSRS.SetUTM(atoi(pszZone));
	}

	char *pszWkt = NULL;
	if(oSRS.exportToWkt(&pszWkt) == OGRERR_NONE)
		poDS->SetProjection(pszWkt);

	double dfUL_X = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.CORNER_UL_PROJECTION_X_PRODUCT","0"));
	double dfUL_Y = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.CORNER_UL_PROJECTION_Y_PRODUCT","0"));
	double dfUR_X = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.CORNER_UR_PROJECTION_X_PRODUCT","0"));
	double dfUR_Y = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.CORNER_UR_PROJECTION_Y_PRODUCT","0"));
	double dfLL_X = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.CORNER_LL_PROJECTION_X_PRODUCT","0"));
	double dfLL_Y = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.CORNER_LL_PROJECTION_Y_PRODUCT","0"));
	double dfLR_X = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.CORNER_LR_PROJECTION_X_PRODUCT","0"));
	double dfLR_Y = CPLAtof(CSLFetchNameValueDef(papszMetaInfo,"L1_METADATA_FILE.PRODUCT_METADATA.CORNER_LR_PROJECTION_Y_PRODUCT","0"));

	double adfGeoTransform[6] = {dfUL_X, dfCellSize, 0, dfUL_Y, 0, -1*dfCellSize};
	poDS->SetGeoTransform(adfGeoTransform);

	/* -------------------------------------------------------------------- */
	/*      We need to open one of the images in order to establish         */
	/*      details like the band count and types.                          */
	/* -------------------------------------------------------------------- */
	GDALDataset *poTemplateDS = NULL;
	
	CPLString ostrFileName;
	ostrFileName.Printf( "L1_METADATA_FILE.PRODUCT_METADATA.FILE_NAME_%s", papszRasterName[0] );
	const char *pszFilename = CSLFetchNameValue( papszMetaInfo, ostrFileName.c_str() );
	if( pszFilename == NULL )
	{
		CPLError( CE_Failure, CPLE_AppDefined, "Missing %s in .TXT file.", ostrFileName.c_str() );
		CSLDestroy(papszRasterName);
		return FALSE;
	}

	CPLString osDirname = CPLGetDirname(pszMtlFile);

	// trim double quotes. 
	if( pszFilename[0] == '"' )
		pszFilename++;
	if( pszFilename[strlen(pszFilename)-1] == '"' )
		((char *) pszFilename)[strlen(pszFilename)-1] = '\0';

	const char* pszSubFile = CPLFormFilename(osDirname, pszFilename, NULL);
	poTemplateDS = (GDALDataset *) GDALOpen( pszSubFile, GA_ReadOnly );
	if( poTemplateDS == NULL || poTemplateDS->GetRasterCount() == 0)
	{
		if (poTemplateDS != NULL)
			GDALClose( poTemplateDS );

		CSLDestroy(papszRasterName);
		return FALSE;
	}

	poDS->SetProjection(poTemplateDS->GetProjectionRef());
	poTemplateDS->GetGeoTransform(adfGeoTransform);
	poDS->SetGeoTransform(adfGeoTransform);

	GDALRasterBand *poTemplateBand = poTemplateDS->GetRasterBand(1);
	GDALDataType eDT = poTemplateBand->GetRasterDataType();
	int          nBandCount = CSLCount(papszRasterName);

	poTemplateBand = NULL;
	GDALClose( poTemplateDS );

	/* -------------------------------------------------------------------- */
	/*      Create and initialize the corresponding VRT dataset used to     */
	/*      manage the tiled data access.                                   */
	/* -------------------------------------------------------------------- */
	poDS->poVRTDS = new VRTDataset(poDS->nRasterXSize, poDS->nRasterYSize);
	int iBand = 0;
	for( iBand = 0; iBand < nBandCount; iBand++ )
		poDS->poVRTDS->AddBand( eDT, NULL );

	/* Don't try to write a VRT file */
	poDS->poVRTDS->SetWritable(FALSE);

	/* -------------------------------------------------------------------- */
	/*      Create band information objects.                                */
	/* -------------------------------------------------------------------- */
	for( iBand = 0; iBand < nBandCount; iBand++ )
	{
		ostrFileName.Printf( "L1_METADATA_FILE.PRODUCT_METADATA.FILE_NAME_%s", papszRasterName[iBand] );
		pszFilename = CSLFetchNameValue( papszMetaInfo, ostrFileName.c_str() );
		if( pszFilename == NULL )
		{
			CPLError( CE_Failure, CPLE_AppDefined, "Missing %s in .TXT file.", ostrFileName.c_str() );
			CSLDestroy(papszRasterName);
			return FALSE;
		}

		// trim double quotes. 
		if( pszFilename[0] == '"' )
			pszFilename++;
		if( pszFilename[strlen(pszFilename)-1] == '"' )
			((char *) pszFilename)[strlen(pszFilename)-1] = '\0';

		pszSubFile = CPLFormFilename(osDirname, pszFilename, NULL);
		GDALDataset *poBandDS = (GDALDataset *) GDALOpen( pszSubFile, GA_ReadOnly );
		if( poBandDS == NULL || poBandDS->GetRasterCount() == 0)
		{
			if (poBandDS != NULL)
				GDALClose( poBandDS );

			CSLDestroy(papszRasterName);
			return FALSE;
		}

		poDS->apoTifDS.push_back( poBandDS );

		GDALRasterBand *pBand = poBandDS->GetRasterBand(1);
		LandSatRasterBand *pLandsatBand = new LandSatRasterBand( poDS, iBand+1, pBand);
		char** papszBandMeta = NULL;
		papszBandMeta = CSLAddString(papszBandMeta, pszFilename);
		pLandsatBand->SetMetadata(papszBandMeta);
		poDS->SetBand( iBand+1, pLandsatBand);
	}

	CSLDestroy(papszRasterName);
	return TRUE;
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

GDALDataset *LandsatDataset::Open( GDALOpenInfo * poOpenInfo )

{
	if( !Identify( poOpenInfo ) )
		return NULL;

	/* -------------------------------------------------------------------- */
	/*      Confirm the requested access is supported.                      */
	/* -------------------------------------------------------------------- */
	if( poOpenInfo->eAccess == GA_Update )
	{
		CPLError( CE_Failure, CPLE_NotSupported, 
			"The LANDSAT driver does not support update access to existing"
			" datasets.\n" );
		return NULL;
	}

	CPLString osFilename;
	int iSubdatasetIndex = -1;//0:PAN 1:REF 2:THM

	if( EQUALN(poOpenInfo->pszFilename, "LANDSAT:",8) )
	{
		const char *pszRest = poOpenInfo->pszFilename+8;

		iSubdatasetIndex = atoi(pszRest);
		while( *pszRest != '\0' && *pszRest != ':' )
			pszRest++;

		if( *pszRest == ':' )
			pszRest++;

		osFilename = pszRest;
	}
	else
		osFilename = poOpenInfo->pszFilename;

	if(iSubdatasetIndex <-1 || iSubdatasetIndex > 3)
	{
		CPLError( CE_Failure, CPLE_IllegalArg, "The LANDSAT driver does not support %d subdatasets.\n", iSubdatasetIndex );
		return NULL;
	}

	/* -------------------------------------------------------------------- */
	/*      Try to load and parse the .MTL .TXT file.                       */
	/* -------------------------------------------------------------------- */
	VSILFILE *fp = VSIFOpenL( osFilename, "r" );
	if( fp == NULL )
	{
		return NULL;
	}

	CPLKeywordParser oParser;
	if( !oParser.Ingest( fp ) )
	{
		VSIFCloseL( fp );
		return NULL;
	}

	VSIFCloseL( fp );

	char **papszMTL = oParser.GetAllKeywords();
	Satellite sat = LANDSAT7;

	const char* pszSatID = CSLFetchNameValue(papszMTL, "L1_METADATA_FILE.PRODUCT_METADATA.SPACECRAFT_ID");
	if(pszSatID == NULL)
	{
		return NULL;
	}

	if(strstr(pszSatID,"Landsat7") != NULL)
		sat = LANDSAT7;
	else if(strstr(pszSatID,"LANDSAT_8") != NULL)
		sat = LANDSAT8;
	else
	{
		return NULL;
	}

	if(sat == LANDSAT7 && iSubdatasetIndex ==3)	//landsat7 not quality
	{
		return NULL;
	}

	/* -------------------------------------------------------------------- */
	/*      Create a corresponding GDALDataset.                             */
	/* -------------------------------------------------------------------- */
	LandsatDataset 	*poDS;
	poDS = new LandsatDataset();
	poDS->SetMetadata(papszMTL);

	char *papszName[4] = {"PAN", "REF", "THE", "QUA"};
	char *papszDesc[4] = {"Panchromatic", "Reflective", "Thermal", "Quality"};
	if( iSubdatasetIndex == -1 )	//not a subdataset
	{
		int nSubdataset = (sat==LANDSAT7) ? 3 : 4;
		for (int i=0; i<nSubdataset; i++)
		{
			CPLString osKey, osValue;
			osKey.Printf( "SUBDATASET_%d_NAME", i+1 );
			osValue.Printf( "LANDSAT:%d:%s", i, osFilename.c_str() );
			poDS->SetMetadataItem( osKey, osValue, "SUBDATASETS" );

			osKey.Printf( "SUBDATASET_%d_DESC", i+1 );
			osValue.Printf( "LANDSAT:%s", papszDesc[i] );
			poDS->SetMetadataItem( osKey, osValue, "SUBDATASETS" );
		}

		return( poDS );
	}

	int nSuccess = FALSE;
	if(sat == LANDSAT7)
		nSuccess = ParserLandsat7(osFilename.c_str(), papszMTL, iSubdatasetIndex, &poDS);
	else
		nSuccess = ParserLandsat8(osFilename.c_str(), papszMTL, iSubdatasetIndex, &poDS);

	if(!nSuccess)
	{
		delete poDS;
		return NULL;
	}

	const char* pszDirname = CPLGetDirname(osFilename);
	const char* pszBasename = CPLGetBasename(osFilename);
	CPLString osTemp;
	osTemp.Printf("%s_%s", pszBasename, papszName[iSubdatasetIndex]);
	const char* pszSubFile = CPLFormFilename(pszDirname, osTemp, "");

	/* -------------------------------------------------------------------- */
	/*      Initialize any PAM information.                                 */
	/* -------------------------------------------------------------------- */
	poDS->SetDescription( pszSubFile );
	poDS->TryLoadXML();

	/* -------------------------------------------------------------------- */
	/*      Check for overviews.                                            */
	/* -------------------------------------------------------------------- */
	poDS->oOvManager.Initialize( poDS, pszSubFile );

	return( poDS );
}

/************************************************************************/
/*                            GetFileList()                             */
/************************************************************************/

char **LandsatDataset::GetFileList()

{
	unsigned int  i;
	char **papszFileList = GDALPamDataset::GetFileList();

	for( i = 0; i < apoTifDS.size(); i++ )
		papszFileList = CSLAddString( papszFileList, apoTifDS[i]->GetDescription() );

	return papszFileList;
}

/************************************************************************/
/*                         GDALRegister_LANDSAT()                       */
/************************************************************************/

void GDALRegister_LANDSAT()

{
	if ( !GDAL_CHECK_VERSION("LANDSAT driver") )
		return;

	if( GDALGetDriverByName( "LANDSAT" ) != NULL )
		return;

	GDALDriver  *poDriver = new GDALDriver();

	poDriver->SetDescription( "LANDSAT" );
	poDriver->SetMetadataItem( GDAL_DCAP_RASTER, "YES" );
	poDriver->SetMetadataItem( GDAL_DMD_LONGNAME, "LANDSAT 7/8 GeoTiff with Metadata" );
	poDriver->SetMetadataItem( GDAL_DMD_HELPTOPIC, "frmt_landsat.html" );

	poDriver->SetMetadataItem( GDAL_DCAP_VIRTUALIO, "YES" );

	poDriver->pfnOpen = LandsatDataset::Open;
	poDriver->pfnIdentify = LandsatDataset::Identify;

	GetGDALDriverManager()->RegisterDriver( poDriver );
}