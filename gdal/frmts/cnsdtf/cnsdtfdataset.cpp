/******************************************************************************
*
* Project:  GDAL
* Purpose:  Implements China Geospatial Data Transfer Grid Format.
* Author:   Minlu Li, liminlu0314@gmail.com
* Describe: Chinese Spatial Data Transfer format. Reference:GB/T 17798-2007
*
******************************************************************************
* Copyright (c) 2013, Minlu Li (liminlu0314@gmail.com)
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

#include "cnsdtfsrs.h"
#include "gdal_pam.h"

static CPLString OSR_GDS( char **papszNV, const char * pszField, 
						 const char *pszDefaultValue );

/************************************************************************/
/* ==================================================================== */
/*                              CNSDTFDataset                           */
/* ==================================================================== */
/************************************************************************/

class CNSDTFRasterBand;

class CPL_DLL CNSDTFDataset : public GDALPamDataset
{
	friend class CNSDTFRasterBand;

	VSILFILE   *fp;

	char        **papszPrj;
	CPLString   osPrjFilename;
	char        *pszProjection;

	// DataMark Version Compress Alpha
	CPLString	osDataMark;
	CPLString	osDataVersion;
	GBool       bIsCompress;
	double      adfAlphaValue;
	CPLString   osValueType;
	int			nHZoom;
	CPLString   osUnitType;

	unsigned char achReadBuf[256];
	GUIntBig    nBufferOffset;
	int         nOffsetInBuffer;

	char        Getc();
	GUIntBig    Tell();
	int         Seek( GUIntBig nOffset );

protected:
	GDALDataType eDataType;
	double      adfGeoTransform[6];
	int         bNoDataSet;
	double      dfNoDataValue;
	double      dfMin;
	double      dfMax;

	int ParseHeader(const char* pszHeader);

public:
	CNSDTFDataset();
	~CNSDTFDataset();

	virtual char **GetFileList(void);

	static GDALDataset *Open( GDALOpenInfo * poOpenInfo);
	static int          Identify( GDALOpenInfo * poOpenInfo);
	static GDALDataset *CreateCopy( const char * pszFilename, 
		GDALDataset *poSrcDS, int bStrict, char ** papszOptions,
		GDALProgressFunc pfnProgress, void * pProgressData );

	virtual CPLErr GetGeoTransform( double * padfTransform);
	virtual const char *GetProjectionRef(void);
};

/************************************************************************/
/* ==================================================================== */
/*                            CNSDTFRasterBand                          */
/* ==================================================================== */
/************************************************************************/

class CNSDTFRasterBand : public GDALPamRasterBand
{
	friend class CNSDTFDataset;

	GUIntBig      *panLineOffset;
	char          *pszUnitType;
	double        dfScale;

public:

	CNSDTFRasterBand( CNSDTFDataset *poDS, int nDataStart);
	virtual       ~CNSDTFRasterBand();

	virtual double GetMinimum( int *pbSuccess );
	virtual double GetMaximum( int *pbSuccess );
	virtual double GetNoDataValue( int * pbSuccess);
	virtual CPLErr SetNoDataValue( double dfNoData);
	virtual const char *GetUnitType();
	CPLErr SetUnitType( const char * pszNewValue); 
	virtual double GetScale( int *pbSuccess = NULL );
	CPLErr SetScale( double dfNewScale);

	virtual CPLErr IReadBlock( int nBlockXOff, int nBlockYOff, void * pImage);
};

/************************************************************************/
/*                           CNSDTFRasterBand()                         */
/************************************************************************/

CNSDTFRasterBand::CNSDTFRasterBand( CNSDTFDataset *poDS, int nDataStart )

{
	this->poDS = poDS;

	nBand = 1;
	eDataType = poDS->eDataType;

	nBlockXSize = poDS->nRasterXSize;
	nBlockYSize = 1;

	panLineOffset = 
		(GUIntBig *) VSICalloc( poDS->nRasterYSize, sizeof(GUIntBig) );
	if (panLineOffset == NULL)
	{
		CPLError(CE_Failure, CPLE_OutOfMemory,
			"CNSDTFRasterBand::CNSDTFRasterBand : Out of memory (nRasterYSize = %d)",
			poDS->nRasterYSize);
		return;
	}

	panLineOffset[0] = nDataStart;
	dfScale = poDS->nHZoom;
	pszUnitType = CPLStrdup(poDS->osUnitType.c_str());
}

/************************************************************************/
/*                          ~CNSDTFRasterBand()                         */
/************************************************************************/

CNSDTFRasterBand::~CNSDTFRasterBand()

{
	CPLFree( panLineOffset );
	CPLFree( pszUnitType );
}

/************************************************************************/
/*                             IReadBlock()                             */
/************************************************************************/

CPLErr CNSDTFRasterBand::IReadBlock( int nBlockXOff, int nBlockYOff,
									void * pImage )

{
	CNSDTFDataset *poODS = (CNSDTFDataset *) poDS;
	int         iPixel;

	if( nBlockYOff < 0 || nBlockYOff > poODS->nRasterYSize - 1
		|| nBlockXOff != 0 || panLineOffset == NULL || poODS->fp == NULL )
		return CE_Failure;

	if( panLineOffset[nBlockYOff] == 0 )
	{
		int iPrevLine;

		for( iPrevLine = 1; iPrevLine <= nBlockYOff; iPrevLine++ )
			if( panLineOffset[iPrevLine] == 0 )
				IReadBlock( nBlockXOff, iPrevLine-1, NULL );
	}

	if( panLineOffset[nBlockYOff] == 0 )
		return CE_Failure;


	if( poODS->Seek( panLineOffset[nBlockYOff] ) != 0 )
	{
		CPLError( CE_Failure, CPLE_FileIO,
			"Can't seek to offset %lu in input file to read data.",
			(long unsigned int)panLineOffset[nBlockYOff] );
		return CE_Failure;
	}

	for( iPixel = 0; iPixel < poODS->nRasterXSize; )
	{
		char szToken[500];
		char chNext;
		int  iTokenChar = 0;

		/* suck up any pre-white space. */
		do {
			chNext = poODS->Getc();
		} while( isspace( (unsigned char)chNext ) );

		while( chNext != '\0' && !isspace((unsigned char)chNext)  )
		{
			if( iTokenChar == sizeof(szToken)-2 )
			{
				CPLError( CE_Failure, CPLE_FileIO, 
					"Token too long at scanline %d.", 
					nBlockYOff );
				return CE_Failure;
			}

			szToken[iTokenChar++] = chNext;
			chNext = poODS->Getc();
		}

		if( chNext == '\0' &&
			(iPixel != poODS->nRasterXSize - 1 ||
			nBlockYOff != poODS->nRasterYSize - 1) )
		{
			CPLError( CE_Failure, CPLE_FileIO, 
				"File short, can't read line %d.",
				nBlockYOff );
			return CE_Failure;
		}

		szToken[iTokenChar] = '\0';

		if( pImage != NULL )
		{
			if( eDataType == GDT_Float64 )
				((double *) pImage)[iPixel] = CPLAtofM(szToken);
			else if( eDataType == GDT_Float32 )
				((float *) pImage)[iPixel] = (float) CPLAtofM(szToken);
			else
				((GInt32 *) pImage)[iPixel] = (GInt32) atoi(szToken);
		}

		iPixel++;
	}

	if( nBlockYOff < poODS->nRasterYSize - 1 )
		panLineOffset[nBlockYOff + 1] = poODS->Tell();

	return CE_None;
}

/************************************************************************/
/*                             GetMinimum()                             */
/************************************************************************/

double CNSDTFRasterBand::GetMinimum( int *pbSuccess )

{
	CNSDTFDataset	*poODS = (CNSDTFDataset *) poDS;

	if( pbSuccess != NULL )
		*pbSuccess = TRUE;

	return poODS->dfMin;
}

/************************************************************************/
/*                             GetMaximum()                             */
/************************************************************************/

double CNSDTFRasterBand::GetMaximum( int *pbSuccess )

{
	CNSDTFDataset	*poODS = (CNSDTFDataset *) poDS;

	if( pbSuccess != NULL )
		*pbSuccess = TRUE;

	return poODS->dfMax;
}

/************************************************************************/
/*                           GetNoDataValue()                           */
/************************************************************************/

double CNSDTFRasterBand::GetNoDataValue( int * pbSuccess )

{
	CNSDTFDataset *poODS = (CNSDTFDataset *) poDS;

	if( pbSuccess )
		*pbSuccess = poODS->bNoDataSet;

	return poODS->dfNoDataValue;
}


/************************************************************************/
/*                           SetNoDataValue()                           */
/************************************************************************/

CPLErr CNSDTFRasterBand::SetNoDataValue( double dfNoData )

{
	CNSDTFDataset *poODS = (CNSDTFDataset *) poDS;

	poODS->bNoDataSet = TRUE;
	poODS->dfNoDataValue = dfNoData;

	return CE_None;
}

/************************************************************************/
/*                            GetUnitType()                             */
/************************************************************************/

const char *CNSDTFRasterBand::GetUnitType()

{
	if( pszUnitType == NULL )
		return "";
	else
		return pszUnitType;
}

/************************************************************************/
/*                            SetUnitType()                             */
/************************************************************************/

CPLErr CNSDTFRasterBand::SetUnitType( const char *pszNewValue )

{
	CPLFree( pszUnitType );

	if( pszNewValue == NULL )
		pszUnitType = NULL;
	else
		pszUnitType = CPLStrdup(pszNewValue);

	return CE_None;
}

/************************************************************************/
/*                              GetScale()                              */
/************************************************************************/

double CNSDTFRasterBand::GetScale( int *pbSuccess )

{
	if( pbSuccess != NULL )
		*pbSuccess = TRUE;

	return dfScale;
}

/************************************************************************/
/*                              SetScale()                              */
/************************************************************************/

CPLErr CNSDTFRasterBand::SetScale( double dfNewScale )

{
	dfScale = dfNewScale;
	return CE_None;
}

/************************************************************************/
/*                            CNSDTFDataset()                           */
/************************************************************************/

CNSDTFDataset::CNSDTFDataset()
{
	papszPrj = NULL;
	pszProjection = CPLStrdup("");
	fp = NULL;
	eDataType = GDT_Int32;
	bNoDataSet = FALSE;
	dfNoDataValue = -99999.0;

	adfGeoTransform[0] = 0.0;
	adfGeoTransform[1] = 1.0;
	adfGeoTransform[2] = 0.0;
	adfGeoTransform[3] = 0.0;
	adfGeoTransform[4] = 0.0;
	adfGeoTransform[5] = 1.0;

	nOffsetInBuffer = 256;
	nBufferOffset = 0;

	osDataMark = "CNSDTF-RAS";
	osDataVersion = "GB/T 17798-2007";
	bIsCompress = FALSE;
	adfAlphaValue = 0.0;
	osValueType = "Integer";
	nHZoom = 1;
	dfMin = 0.0;
    dfMax = 0.0;
}

/************************************************************************/
/*                           ~CNSDTFDataset()                           */
/************************************************************************/

CNSDTFDataset::~CNSDTFDataset()

{
	FlushCache();

	if( fp != NULL )
		VSIFCloseL( fp );

	CPLFree( pszProjection );
	CSLDestroy( papszPrj );
}

/************************************************************************/
/*                                Tell()                                */
/************************************************************************/

GUIntBig CNSDTFDataset::Tell()

{
	return nBufferOffset + nOffsetInBuffer;
}

/************************************************************************/
/*                                Seek()                                */
/************************************************************************/

int CNSDTFDataset::Seek( GUIntBig nNewOffset )

{
	nOffsetInBuffer = sizeof(achReadBuf);
	return VSIFSeekL( fp, nNewOffset, SEEK_SET );
}

/************************************************************************/
/*                                Getc()                                */
/*                                                                      */
/*      Read a single character from the input file (efficiently we     */
/*      hope).                                                          */
/************************************************************************/

char CNSDTFDataset::Getc()

{
	if( nOffsetInBuffer < (int) sizeof(achReadBuf) )
		return achReadBuf[nOffsetInBuffer++];

	nBufferOffset = VSIFTellL( fp );
	int nRead = VSIFReadL( achReadBuf, 1, sizeof(achReadBuf), fp );
	unsigned int i;
	for(i=nRead;i<sizeof(achReadBuf);i++)
		achReadBuf[i] = '\0';

	nOffsetInBuffer = 0;

	return achReadBuf[nOffsetInBuffer++];
}

/************************************************************************/
/*                            GetFileList()                             */
/************************************************************************/

char **CNSDTFDataset::GetFileList()

{
	char **papszFileList = GDALPamDataset::GetFileList();

	if( papszPrj != NULL )
		papszFileList = CSLAddString( papszFileList, osPrjFilename );

	return papszFileList;
}

/************************************************************************/
/*                            Identify()                                */
/************************************************************************/

int CNSDTFDataset::Identify( GDALOpenInfo * poOpenInfo )

{
	/* -------------------------------------------------------------------- */
	/*      Does this look like an CNSDTF grid file?                        */
	/* -------------------------------------------------------------------- */
	if( poOpenInfo->nHeaderBytes < 40
		|| !( EQUALN((const char *) poOpenInfo->pabyHeader,"DataMark",8) ||
		EQUALN((const char *) poOpenInfo->pabyHeader,"Version",7) ||
		EQUALN((const char *) poOpenInfo->pabyHeader,"Alpha",5)||
		EQUALN((const char *) poOpenInfo->pabyHeader,"Compress",7)||
		EQUALN((const char *) poOpenInfo->pabyHeader,"X0",2)||
		EQUALN((const char *) poOpenInfo->pabyHeader,"Y0",2)||
		EQUALN((const char *) poOpenInfo->pabyHeader,"DX",2)||
		EQUALN((const char *) poOpenInfo->pabyHeader,"DY",2)||
		EQUALN((const char *) poOpenInfo->pabyHeader,"Row",3)||
		EQUALN((const char *) poOpenInfo->pabyHeader,"Col",3)||
		EQUALN((const char *) poOpenInfo->pabyHeader,"ValueType",9)||
		EQUALN((const char *) poOpenInfo->pabyHeader,"HZoom",5)) )
		return FALSE;

	char** papszTokens = CSLTokenizeString2( (const char *) poOpenInfo->pabyHeader,  " \n\r\t" , 0 );
	int nTokens = CSLCount(papszTokens);
	if (nTokens <11)	//the header at (the) least 11 lines
	{
		CSLDestroy( papszTokens );
		return FALSE;
	}

	if( poOpenInfo->nHeaderBytes < 40 || 
		!( EQUALN(papszTokens[0], "DataMark:CNSDTF-DEM", 19) || 
		EQUALN(papszTokens[0], "DataMark:CNSDTF-RAS", 19) || 
		EQUALN(papszTokens[0], "DataMark:CSDTF-DEM", 18) || 
		EQUALN(papszTokens[0], "DataMark:CSDTF-RAS", 18))
		)
	{
		CSLDestroy( papszTokens );
		return FALSE;
	}

	CSLDestroy( papszTokens );
	return TRUE;
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

GDALDataset *CNSDTFDataset::Open( GDALOpenInfo * poOpenInfo )
{
	if (!Identify(poOpenInfo))
		return NULL;

	int i = 0;

	/* -------------------------------------------------------------------- */
	/*      Create a corresponding GDALDataset.                             */
	/* -------------------------------------------------------------------- */
	CNSDTFDataset         *poDS;
	poDS = new CNSDTFDataset();

	/* -------------------------------------------------------------------- */
	/*      Parse the header.                                               */
	/* -------------------------------------------------------------------- */
	if (!poDS->ParseHeader((const char *) poOpenInfo->pabyHeader))
	{
		delete poDS;
		return NULL;
	}

	/* -------------------------------------------------------------------- */
	/*      Open file with large file API.                                  */
	/* -------------------------------------------------------------------- */

	poDS->fp = VSIFOpenL( poOpenInfo->pszFilename, "r" );
	if( poDS->fp == NULL )
	{
		CPLError( CE_Failure, CPLE_OpenFailed, 
			"VSIFOpenL(%s) failed unexpectedly.", 
			poOpenInfo->pszFilename );
		delete poDS;
		return NULL;
	} 

	/* -------------------------------------------------------------------- */
	/*      Find the start of real data.                                    */
	/* -------------------------------------------------------------------- */
	int         nStartOfData;

	for (i=0; TRUE; i++)
	{
		char cChar = poOpenInfo->pabyHeader[i];
		if( cChar == '\0' )
		{
			CPLError( CE_Failure, CPLE_AppDefined, 
				"Couldn't find data values in CNSDTF Grid file.\n" );
			delete poDS;
			return NULL;
		}

		if (cChar == '\r' || cChar == '\n')
		{
			char cNext = poOpenInfo->pabyHeader[i+1];
			if(isalpha(cNext) || isalpha(poOpenInfo->pabyHeader[i+2]))
				continue;

			if ((cNext >= '0' && cNext <='9') 
				|| cNext=='-' 
				|| cNext=='.' )
			{
				nStartOfData = i+1;
				/* Beginning of real data found. */
				break;
			}
		}
	}

	/* -------------------------------------------------------------------- */
	/*      Recognize the type of data.										*/
	/* -------------------------------------------------------------------- */
	CPLAssert( NULL != poDS->fp );

	/* -------------------------------------------------------------------- */
	/*      Create band information objects.                                */
	/* -------------------------------------------------------------------- */
	CNSDTFRasterBand* band = new CNSDTFRasterBand( poDS, nStartOfData );
	poDS->SetBand( 1, band );
	if (band->panLineOffset == NULL)
	{
		delete poDS;
		return NULL;
	}

	/* -------------------------------------------------------------------- */
	/*      Parse the SRS.                                                  */
	/* -------------------------------------------------------------------- */
	CPLString strProjection = ParseSpatialReference((const char *) poOpenInfo->pabyHeader);
	if (strProjection != "")
	{
		CPLFree( poDS->pszProjection );
		poDS->pszProjection = CPLStrdup(strProjection.c_str());
	}
	
	if (EQUAL(poDS->pszProjection, ""))
	{
		/* -------------------------------------------------------------------- */
		/*      Try to read projection file.                                    */
		/* -------------------------------------------------------------------- */
		char        *pszDirname, *pszBasename;
		VSIStatBufL   sStatBuf;

		pszDirname = CPLStrdup(CPLGetPath(poOpenInfo->pszFilename));
		pszBasename = CPLStrdup(CPLGetBasename(poOpenInfo->pszFilename));

		poDS->osPrjFilename = CPLFormFilename( pszDirname, pszBasename, "prj" );
		int nRet = VSIStatL( poDS->osPrjFilename, &sStatBuf );

		if( nRet != 0 && VSIIsCaseSensitiveFS(poDS->osPrjFilename) )
		{
			poDS->osPrjFilename = CPLFormFilename( pszDirname, pszBasename, "PRJ" );
			nRet = VSIStatL( poDS->osPrjFilename, &sStatBuf );
		}

		if( nRet == 0 )
		{
			OGRSpatialReference     oSRS;

			poDS->papszPrj = CSLLoad( poDS->osPrjFilename );

			CPLDebug( "CNSDTF Grid", "Loaded SRS from %s", 
				poDS->osPrjFilename.c_str() );

			if( oSRS.importFromESRI( poDS->papszPrj ) == OGRERR_NONE )
			{
				// If geographic values are in seconds, we must transform. 
				// Is there a code for minutes too? 
				if( oSRS.IsGeographic() 
					&& EQUAL(OSR_GDS( poDS->papszPrj, "Units", ""), "DS") )
				{
					poDS->adfGeoTransform[0] /= 3600.0;
					poDS->adfGeoTransform[1] /= 3600.0;
					poDS->adfGeoTransform[2] /= 3600.0;
					poDS->adfGeoTransform[3] /= 3600.0;
					poDS->adfGeoTransform[4] /= 3600.0;
					poDS->adfGeoTransform[5] /= 3600.0;
				}

				CPLFree( poDS->pszProjection );
				oSRS.exportToWkt( &(poDS->pszProjection) );
			}
		}

		CPLFree( pszDirname );
		CPLFree( pszBasename );	
	}

	/* -------------------------------------------------------------------- */
	/*      Initialize any PAM information.                                 */
	/* -------------------------------------------------------------------- */
	poDS->SetDescription( poOpenInfo->pszFilename );
	poDS->TryLoadXML();

	/* -------------------------------------------------------------------- */
	/*      Check for external overviews.                                   */
	/* -------------------------------------------------------------------- */
	poDS->oOvManager.Initialize( poDS, poOpenInfo->pszFilename );

	return( poDS );
}

/************************************************************************/
/*                          ParseHeader()                               */
/************************************************************************/

int CNSDTFDataset::ParseHeader(const char* pszHeader)
{
	int i, j;
	char** papszTokens = CSLTokenizeString2( pszHeader,  " \n\r\t:£º" , 0 );
	int nTokens = CSLCount(papszTokens);

	if ( (i = CSLFindString( papszTokens, "DataMark" )) < 0 || i + 1 >= nTokens)
	{
		CSLDestroy( papszTokens );
		return FALSE;
	}
	osDataMark = papszTokens[i + 1];

	if ( (i = CSLFindString( papszTokens, "Version" )) < 0 || i + 1 >= nTokens)
	{
		CSLDestroy( papszTokens );
		return FALSE;
	}
	osDataVersion = papszTokens[i + 1];

	if ( (i = CSLFindString( papszTokens, "Alpha" )) < 0 || i + 1 >= nTokens)
	{
		CSLDestroy( papszTokens );
		return FALSE;
	}
	adfAlphaValue = CPLAtofM(papszTokens[i + 1]);

	if ( (i = CSLFindString( papszTokens, "Compress" )) < 0 || i + 1 >= nTokens)
	{
		CSLDestroy( papszTokens );
		return FALSE;
	}
	bIsCompress = atoi(papszTokens[i + 1]);

	if ( (i = CSLFindString( papszTokens, "Hzoom" )) < 0 ||
		i + 1 >= nTokens)
	{
		CSLDestroy( papszTokens );
		return FALSE;
	}
	nHZoom = atoi(papszTokens[i + 1]);

	if ( (i = CSLFindString( papszTokens, "Col" )) < 0 || i + 1 >= nTokens)
	{
		CSLDestroy( papszTokens );
		return FALSE;
	}

	nRasterXSize = atoi(papszTokens[i + 1]);

	if ( (i = CSLFindString( papszTokens, "Row" )) < 0 || i + 1 >= nTokens)
	{
		CSLDestroy( papszTokens );
		return FALSE;
	}

	nRasterYSize = atoi(papszTokens[i + 1]);

	if (!GDALCheckDatasetDimensions(nRasterXSize, nRasterYSize))
	{
		CSLDestroy( papszTokens );
		return FALSE;
	}

	double dfCellDX = 0;
	double dfCellDY = 0;
	if ( (i = CSLFindString( papszTokens, "CELLSIZE" )) < 0 )
	{
		int iDX, iDY;
		if( (iDX = CSLFindString(papszTokens,"DX")) < 0
			|| (iDY = CSLFindString(papszTokens,"DY")) < 0
			|| iDX+1 >= nTokens
			|| iDY+1 >= nTokens)
		{
			CSLDestroy( papszTokens );
			return FALSE;
		}

		dfCellDX = CPLAtofM( papszTokens[iDX+1] );
		dfCellDY = CPLAtofM( papszTokens[iDY+1] );
	}
	else
	{
		if (i + 1 >= nTokens)
		{
			CSLDestroy( papszTokens );
			return FALSE;
		}
		dfCellDX = dfCellDY = CPLAtofM( papszTokens[i + 1] );
	}

	if ((i = CSLFindString( papszTokens, "X0" )) >= 0 &&
		(j = CSLFindString( papszTokens, "Y0" )) >= 0 &&
		i + 1 < nTokens && j + 1 < nTokens)
	{
		adfGeoTransform[0] = CPLAtofM( papszTokens[i + 1] );
		adfGeoTransform[1] = dfCellDX;
		adfGeoTransform[2] = 0.0;
		adfGeoTransform[3] = CPLAtofM( papszTokens[j + 1] );
		adfGeoTransform[4] = 0.0;
		adfGeoTransform[5] = - dfCellDY;
	}
	else
	{
		adfGeoTransform[0] = 0.0;
		adfGeoTransform[1] = dfCellDX;
		adfGeoTransform[2] = 0.0;
		adfGeoTransform[3] = 0.0;
		adfGeoTransform[4] = 0.0;
		adfGeoTransform[5] = - dfCellDY;
	}

	if ( (i = CSLFindString( papszTokens, "ValueType" )) < 0 || i + 1 >= nTokens)
	{
		CSLDestroy( papszTokens );
		return FALSE;
	}

	osValueType = papszTokens[i + 1];
	if (EQUAL(osValueType, "Integer"))
	{
		eDataType = GDT_Int32;
		bNoDataSet = TRUE;
		dfNoDataValue = -99999;
	}
	else if (EQUAL(osValueType, "Char"))
	{
		eDataType = GDT_Byte;
	}
	else
	{
		CSLDestroy( papszTokens );
		return FALSE;
	}

	if( (i = CSLFindString( papszTokens, "NODATA_value" )) >= 0 && i + 1 < nTokens)
	{
		const char* pszNoData = papszTokens[i + 1];

		bNoDataSet = TRUE;
		dfNoDataValue = CPLAtofM(pszNoData);
		if((strchr( pszNoData, '.' ) != NULL ||
			strchr( pszNoData, ',' ) != NULL ||
			INT_MIN > dfNoDataValue || dfNoDataValue > INT_MAX) )
		{
			eDataType = GDT_Float32;
		}

		if( eDataType == GDT_Float32 )
		{
			dfNoDataValue = (double) (float) dfNoDataValue;
		}
	}

	if ( (i = CSLFindString( papszTokens, "MinV" )) >= 0 && i + 1 < nTokens)
		dfMin = CPLAtofM(papszTokens[i + 1]);

	if ( (i = CSLFindString( papszTokens, "MaxV" )) >= 0 && i + 1 < nTokens)
		dfMax = CPLAtofM(papszTokens[i + 1]);

	if ( (i = CSLFindString( papszTokens, "Unit" )) >= 0 && i + 1 < nTokens)
		osUnitType = papszTokens[i + 1];

	if ( (i = CSLFindString( papszTokens, "ZUnit" )) >= 0 && i + 1 < nTokens)
		osUnitType = papszTokens[i + 1];

	CSLDestroy( papszTokens );
	return TRUE;
}


/************************************************************************/
/*                          GetGeoTransform()                           */
/************************************************************************/

CPLErr CNSDTFDataset::GetGeoTransform( double * padfTransform )

{
	memcpy( padfTransform, adfGeoTransform, sizeof(double) * 6 );
	return( CE_None );
}

/************************************************************************/
/*                          GetProjectionRef()                          */
/************************************************************************/

const char *CNSDTFDataset::GetProjectionRef()

{
	return pszProjection;
}

/************************************************************************/
/*                          CreateCopy()                                */
/************************************************************************/

GDALDataset * CNSDTFDataset::CreateCopy(
										const char * pszFilename, GDALDataset *poSrcDS,
										int bStrict, char ** papszOptions, 
										GDALProgressFunc pfnProgress, void * pProgressData )

{
	int  nBands = poSrcDS->GetRasterCount();
	int  nXSize = poSrcDS->GetRasterXSize();
	int  nYSize = poSrcDS->GetRasterYSize();

	/* -------------------------------------------------------------------- */
	/*      Some rudimentary checks                                         */
	/* -------------------------------------------------------------------- */
	if( nBands != 1 )
	{
		CPLError( CE_Failure, CPLE_NotSupported, 
			"CNSDTF Grid driver doesn't support %d bands.  Must be 1 band.\n",
			nBands );

		return NULL;
	}

	if( !pfnProgress( 0.0, NULL, pProgressData ) )
		return NULL;

	/* -------------------------------------------------------------------- */
	/*      Create the dataset.                                             */
	/* -------------------------------------------------------------------- */
	VSILFILE        *fpImage;

	fpImage = VSIFOpenL( pszFilename, "wt" );
	if( fpImage == NULL )
	{
		CPLError( CE_Failure, CPLE_OpenFailed, 
			"Unable to create file %s.\n", pszFilename );
		return NULL;
	}

	/* -------------------------------------------------------------------- */
	/*      Write CNSDTF Grid file header                                   */
	/* -------------------------------------------------------------------- */
	char szHeader[2000];	
	const char *pszForceRaster = CSLFetchNameValue( papszOptions, "FORCE_RASTER" );
	char* pszHeaderMark = "DataMark:CNSDTF-DEM";
	if(pszForceRaster != NULL && CSLTestBoolean(pszForceRaster))
		pszHeaderMark = "DataMark:CNSDTF-RAS";

	double adfGeoTransform[6];
	poSrcDS->GetGeoTransform( adfGeoTransform );

	sprintf( szHeader, 
		"%s\n"
		"Version:GB/T 17798-2007\n"
		"Alpha:0.0\n"
		"Compress:0\n"
		"X0:%.12f\n"
		"Y0:%.12f\n"
		"DX:%.12f\n"
		"DY:%.12f\n"
		"Row:%d\n"
		"Col:%d\n" 
		"ValueType:Integer\n",	//Integer
		pszHeaderMark,
		adfGeoTransform[0], 
		adfGeoTransform[3],
		ABS(adfGeoTransform[1]), 
		ABS(adfGeoTransform[5]),
		nYSize, nXSize);

	/* -------------------------------------------------------------------- */
	/*      Try to write projection file.                                   */
	/* -------------------------------------------------------------------- */
	const char  *pszOriginalProjection = poSrcDS->GetProjectionRef();
	if( !EQUAL( pszOriginalProjection, "" ) )
	{
		char                    *pszDirname, *pszBasename;
		char                    *pszPrjFilename;
		char                    *pszESRIProjection = NULL;
		VSILFILE                *fp;
		OGRSpatialReference     oSRS;

		pszDirname = CPLStrdup( CPLGetPath(pszFilename) );
		pszBasename = CPLStrdup( CPLGetBasename(pszFilename) );

		pszPrjFilename = CPLStrdup( CPLFormFilename( pszDirname, pszBasename, "prj" ) );
		fp = VSIFOpenL( pszPrjFilename, "wt" );
		if (fp != NULL)
		{
			oSRS.importFromWkt( (char **) &pszOriginalProjection );
			oSRS.morphToESRI();
			oSRS.exportToWkt( &pszESRIProjection );
			VSIFWriteL( pszESRIProjection, 1, strlen(pszESRIProjection), fp );

			VSIFCloseL( fp );
			CPLFree( pszESRIProjection );
		}
		else
		{
			CPLError( CE_Failure, CPLE_FileIO, 
				"Unable to create file %s.\n", pszPrjFilename );
		}
		CPLFree( pszDirname );
		CPLFree( pszBasename );
		CPLFree( pszPrjFilename );
	}

	GDALRasterBand * poBand = poSrcDS->GetRasterBand( 1 );
	double dfNoData, dfScale, dfMin, dfMax;
	int bSuccess;

	const char* pszUnitType = poBand->GetUnitType();
	if(pszUnitType != NULL)
		sprintf( szHeader+strlen(szHeader), "ZUnit:%s\n", pszUnitType );

	// Write `nodata' value to header if it is exists in source dataset
	dfNoData = poBand->GetNoDataValue( &bSuccess );
	if ( bSuccess )
		sprintf( szHeader+strlen(szHeader), "NODATA_value:%6.20g\n", dfNoData );

	// Write `HZoom' value to header if it is exists in source dataset
	dfScale = poBand->GetScale( &bSuccess );
	if ( !bSuccess )
		dfScale = 1.0;
	sprintf( szHeader+strlen(szHeader), "HZoom:%.20g\n", dfScale );

	// Write `minmax' value to header if it is exists in source dataset
	dfMin = poBand->GetMinimum( &bSuccess );
	if ( bSuccess )
		sprintf( szHeader+strlen(szHeader), "MinV:%.20g\n", dfMin );

	dfMax = poBand->GetMaximum( &bSuccess );
	if ( bSuccess )
		sprintf( szHeader+strlen(szHeader), "MaxV:%.20g\n", dfMax );

	VSIFWriteL( szHeader, 1, strlen(szHeader), fpImage );

	/* -------------------------------------------------------------------- */
	/*     Builds the format string used for printing float values.         */
	/* -------------------------------------------------------------------- */
	char szFormatFloat[32];
	strcpy(szFormatFloat, " %.20g");
	const char *pszDecimalPrecision = CSLFetchNameValue( papszOptions, "DECIMAL_PRECISION" );
	if (pszDecimalPrecision)
	{
		int nDecimal = atoi(pszDecimalPrecision);
		if (nDecimal >= 0)
			sprintf(szFormatFloat, " %%.%dg", nDecimal);
	}

	/* -------------------------------------------------------------------- */
	/*      Loop over image, copying image data.                            */
	/* -------------------------------------------------------------------- */
	int         *panScanline = NULL;
	double      *padfScanline = NULL;
	int         bReadAsInt;
	int         iLine, iPixel;
	CPLErr      eErr = CE_None;

	bReadAsInt = ( poBand->GetRasterDataType() == GDT_Byte 
		|| poBand->GetRasterDataType() == GDT_Int16
		|| poBand->GetRasterDataType() == GDT_UInt16
		|| poBand->GetRasterDataType() == GDT_Int32 );

	// Write scanlines to output file
	if (bReadAsInt)
		panScanline = (int *) CPLMalloc( nXSize * GDALGetDataTypeSize(GDT_Int32) / 8 );
	else
		padfScanline = (double *) CPLMalloc( nXSize * GDALGetDataTypeSize(GDT_Float64) / 8 );

	for( iLine = 0; eErr == CE_None && iLine < nYSize; iLine++ )
	{
		CPLString osBuf;
		eErr = poBand->RasterIO( GF_Read, 0, iLine, nXSize, 1, 
			(bReadAsInt) ? (void*)panScanline : (void*)padfScanline,
			nXSize, 1, (bReadAsInt) ? GDT_Int32 : GDT_Float64, 0, 0, NULL );

		if( bReadAsInt )
		{
			for ( iPixel = 0; iPixel < nXSize; iPixel++ )
			{
				sprintf( szHeader, "%d ", panScanline[iPixel] );
				if(iPixel % 10 == 9)
					sprintf( szHeader+strlen(szHeader), "\n" );

				osBuf += szHeader;
				if( (iPixel & 1023) == 0 || iPixel == nXSize - 1 )
				{
					if ( VSIFWriteL( osBuf, (int)osBuf.size(), 1, fpImage ) != 1 )
					{
						eErr = CE_Failure;
						CPLError( CE_Failure, CPLE_AppDefined, "Write failed, disk full?\n" );
						break;
					}
					osBuf = "";
				}
			}
		}
		else
		{
			for ( iPixel = 0; iPixel < nXSize; iPixel++ )
			{
				sprintf( szHeader, szFormatFloat, padfScanline[iPixel] );
				if(iPixel % 10 == 9)
					sprintf( szHeader+strlen(szHeader), "\n" );

				osBuf += szHeader;
				if( (iPixel & 1023) == 0 || iPixel == nXSize - 1 )
				{
					if ( VSIFWriteL( osBuf, (int)osBuf.size(), 1, fpImage ) != 1 )
					{
						eErr = CE_Failure;
						CPLError( CE_Failure, CPLE_AppDefined, "Write failed, disk full?\n" );
						break;
					}
					osBuf = "";
				}
			}
		}
		VSIFWriteL( (void *) "\n", 1, 1, fpImage );

		if( eErr == CE_None &&
			!pfnProgress((iLine + 1) / ((double) nYSize), NULL, pProgressData) )
		{
			eErr = CE_Failure;
			CPLError( CE_Failure, CPLE_UserInterrupt, "User terminated CreateCopy()" );
		}
	}

	CPLFree( panScanline );
	CPLFree( padfScanline );
	VSIFCloseL( fpImage );

	if( eErr != CE_None )
		return NULL;

	/* -------------------------------------------------------------------- */
	/*      Re-open dataset, and copy any auxilary pam information.         */
	/* -------------------------------------------------------------------- */

	/* If outputing to stdout, we can't reopen it, so we'll return */
	/* a fake dataset to make the caller happy */
	CPLPushErrorHandler(CPLQuietErrorHandler);
	GDALPamDataset* poDS = (GDALPamDataset*) GDALOpen(pszFilename, GA_ReadOnly);
	CPLPopErrorHandler();
	if (poDS)
	{
		poDS->CloneInfo( poSrcDS, GCIF_PAM_DEFAULT );
		return poDS;
	}

	CPLErrorReset();

	CNSDTFDataset* poCnsdtfDS = new CNSDTFDataset();
	poCnsdtfDS->nRasterXSize = nXSize;
	poCnsdtfDS->nRasterYSize = nYSize;
	poCnsdtfDS->nBands = 1;
	poCnsdtfDS->SetBand( 1, new CNSDTFRasterBand( poCnsdtfDS, 1 ) );
	return poCnsdtfDS;
}

/************************************************************************/
/*                              OSR_GDS()                               */
/************************************************************************/

static CPLString OSR_GDS( char **papszNV, const char * pszField, 
						 const char *pszDefaultValue )

{
	int         iLine;

	if( papszNV == NULL || papszNV[0] == NULL )
		return pszDefaultValue;

	for( iLine = 0; 
		papszNV[iLine] != NULL && 
		!EQUALN(papszNV[iLine],pszField,strlen(pszField));
	iLine++ ) {}

	if( papszNV[iLine] == NULL )
		return pszDefaultValue;
	else
	{
		CPLString osResult;
		char    **papszTokens;

		papszTokens = CSLTokenizeString(papszNV[iLine]);

		if( CSLCount(papszTokens) > 1 )
			osResult = papszTokens[1];
		else
			osResult = pszDefaultValue;

		CSLDestroy( papszTokens );
		return osResult;
	}
}

/************************************************************************/
/*                       GDALRegister_CNSDTF()                          */
/************************************************************************/

void GDALRegister_CNSDTF()

{
	if( GDALGetDriverByName( "CNSDTF" ) != NULL )
		return;

	GDALDriver  *poDriver = new GDALDriver();

	poDriver->SetDescription( "CNSDTF" );
	poDriver->SetMetadataItem( GDAL_DCAP_RASTER, "YES" );
	poDriver->SetMetadataItem( GDAL_DCAP_VECTOR, "YES");
	poDriver->SetMetadataItem( GDAL_DMD_LONGNAME, "China Geospatial Data Transfer Forma");
	poDriver->SetMetadataItem( GDAL_DMD_HELPTOPIC, "frmt_cnsdtf.html" );
	poDriver->SetMetadataItem( GDAL_DCAP_VIRTUALIO, "YES" );
	poDriver->SetMetadataItem( GDAL_DMD_EXTENSION, "grd,vct" );
	poDriver->SetMetadataItem( GDAL_DMD_CREATIONDATATYPES, "Byte UInt16 Int16 Int32" );

	poDriver->SetMetadataItem( GDAL_DMD_CREATIONOPTIONLIST, 
		"<CreationOptionList>\n"
		"   <Option name='FORCE_RASTER' type='boolean' description='Force use of RASTER, default is FALSE(DEM).'/>\n"
		"   <Option name='DECIMAL_PRECISION' type='int' description='Number of decimal when writing floating-point numbers.'/>\n"
		"</CreationOptionList>\n" );

	poDriver->pfnIdentify = CNSDTFDataset::Identify;
	poDriver->pfnOpen = CNSDTFDataset::Open;
	poDriver->pfnCreateCopy = CNSDTFDataset::CreateCopy;

	GetGDALDriverManager()->RegisterDriver( poDriver );
}