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

GIntBig GetFilePosition(VSILFILE* fp, const char* pszMark, int bBegin = TRUE)
{
	const char* pszBuf = CPLReadLineL( fp );
	if(pszBuf == NULL)
		return -1;

	do 
	{
		if(EQUAL(pszBuf, pszMark))
			return static_cast<GIntBig>(VSIFTellL(fp) - (bBegin ? strlen(pszMark) : 0)) - 2;

		pszBuf = CPLReadLineL( fp );
	} while (pszBuf != NULL);

	return -1;
}

int GetDataBuffer(VSILFILE* fp, const char* pszBegin, const char* pszEnd, char** papszBuf)
{
	GIntBig nBegin = GetFilePosition(fp, pszBegin, TRUE);
	GIntBig nEnd = GetFilePosition(fp, pszEnd, FALSE);

	if(nBegin < 0 || nEnd <0 || nEnd <= nBegin)
		return -1;

	VSIFSeekL(fp, nBegin, SEEK_SET);

	int nSize =  static_cast<int>(nEnd - nBegin + 2);
	*papszBuf = (char *) CPLCalloc(nSize, 1);
	memset(*papszBuf, 0, nSize);

	int nBufferBytes = (int) VSIFReadL( *papszBuf, 1, nSize-1, fp );
	if (*papszBuf == NULL || nBufferBytes <= 0)
		return nBufferBytes;

	return nBufferBytes;
}

static char *papszProjectionName[18] = {
	"地理坐标系",
	"高斯-克吕格",
	"兰勃特正形割圆锥",
	"兰勃特正形切圆锥",
	"兰勃特等积方位",
	"亚尔勃斯等积割圆锥",
	"亚尔勃斯等积切圆锥",
	"通用横轴墨卡托",
	"墨卡托正轴等角切圆柱",
	"墨卡托正轴等角割圆柱",
	"波斯托等距切方位",
	"彭纳等积伪圆锥",
	"等积正轴切圆柱",
	"等积正轴割圆柱",
	"等距正轴切圆锥",
	"等距正轴割圆锥",
	"等积斜轴切方位",
	NULL
};

static const int nProjectionParametersIndex[18] = {
	0,		//0000000000	"地理坐标系"
	543,	//1000011111	"高斯-克吕格"
	972,	//1111001100	"兰勃特正形割圆锥"
	780,	//1100001100	"兰勃特正形切圆锥"
	796,	//1100011100	"兰勃特等积方位"
	543,	//1111001100	"亚尔勃斯等积割圆锥"
	972,	//1111001100	"亚尔勃斯等积切圆锥"
	525,	//1000001101	"通用横轴墨卡托"
	540,	//1000011100	"墨卡托正轴等角切圆柱"
	796,	//1100011100	"墨卡托正轴等角割圆柱"
	780,	//1100001100	"波斯托等距切方位"
	780,	//1100001100	"彭纳等积伪圆锥"
	524,	//1000001100	"等积正轴切圆柱"
	780,	//1100001100	"等积正轴割圆柱"
	780,	//1100001100	"等距正轴切圆锥"
	972,	//1111001100	"等距正轴割圆锥"
	796,	//1100011100	"等积斜轴切方位"
	0
};

/************************************************************************/
/*                    ParseSpatialReference()                           */
/************************************************************************/

CPLString ParseSpatialReference(const char* pszHeader)
{
	char** papszTokens = CSLTokenizeString2( pszHeader,  " \n\r\t:：", 0 );
	int nTokens = CSLCount(papszTokens);

	int iCST = CSLFindString( papszTokens, "CoordinateSystemType" );
	if ( iCST<0 || iCST+1 >= nTokens)
	{
		CSLDestroy( papszTokens );
		CPLDebug( "CNSDTF Grid", "Can't find SRS");
		return "";
	}

	char *pszCoordinateSystemType = papszTokens[iCST + 1];
	if (EQUAL(pszCoordinateSystemType, "C"))
	{
		CSLDestroy( papszTokens );
		CPLDebug( "CNSDTF Grid", "Can't find SRS");
		return "";
	}

	if ( (!EQUAL(pszCoordinateSystemType, "D")) && (!EQUAL(pszCoordinateSystemType, "P")))
	{
		CSLDestroy( papszTokens );
		CPLDebug( "CNSDTF Grid", "Can't find SRS");
		return "";
	}

	int iSpheroid = CSLFindString( papszTokens, "Spheroid" );
	if ( iSpheroid<0 || iSpheroid+1 >= nTokens)
	{
		CSLDestroy( papszTokens );
		CPLDebug( "CNSDTF Grid", "Can't find Spheroid, but the CoordinateSystemType is %s, this file header maybe is wrong.", pszCoordinateSystemType);
		return "";
	}

	char* pszSpheroid = papszTokens[iSpheroid + 1];
	char** papszSpheroidTokens = CSLTokenizeString2( pszSpheroid,  ",，", 0 );
	int nSpheroidTokens = CSLCount(papszSpheroidTokens);
	if(nSpheroidTokens != 3)
	{
		CSLDestroy( papszSpheroidTokens );
		CSLDestroy( papszTokens );
		CPLDebug( "CNSDTF Grid", "The Spheroid value is %s, maybe is wrong.", pszSpheroid);
		return "";
	}

	char *pszPrimeMeridian = "Greenwich";
	double dPrimeMeridian = 0.0;
	int iPrimeMeridian = CSLFindString( papszTokens, "PrimeMeridian" );
	if ( iPrimeMeridian>=0 && iPrimeMeridian+1 < nTokens)
	{
		char* pszPrimeMeridianValue = papszTokens[iPrimeMeridian + 1];
		if (!EQUAL(pszPrimeMeridian, pszPrimeMeridianValue))
		{
			char** papszPrimeMeridianTokens = CSLTokenizeString2( pszPrimeMeridianValue,  ",，", 0 );
			int nPrimeMeridianTokens = CSLCount(papszPrimeMeridianTokens);
			if(nPrimeMeridianTokens == 2)
			{
				pszPrimeMeridian = papszPrimeMeridianTokens[0];
				dPrimeMeridian = CPLAtofM(papszPrimeMeridianTokens[1]);
			}

			CSLDestroy( papszPrimeMeridianTokens );
		}
	}

	double dSemiMajor = CPLAtofM(papszSpheroidTokens[1]);
	double dInvFlattening = CPLAtofM(papszSpheroidTokens[2]);
	if (dSemiMajor < 6400)	//unit is km
		dSemiMajor *= 1000;

	OGRSpatialReference oSRS;
	oSRS.SetGeogCS(papszSpheroidTokens[0],
		"unknown", 
		papszSpheroidTokens[0], 
		dSemiMajor, dInvFlattening,
		pszPrimeMeridian, dPrimeMeridian, 
		"degree", 0.0174532925199433 );

	char* pszWkt = NULL;

	if (EQUAL(pszCoordinateSystemType, "D"))
	{
		oSRS.exportToWkt( &pszWkt );
		CSLDestroy( papszTokens );
		return CPLString(pszWkt);
	}

	int iProjection = CSLFindString( papszTokens, "Projection" );
	if ( iProjection<0 || iProjection+1 >= nTokens)
	{
		CSLDestroy( papszTokens );
		CPLDebug( "CNSDTF Grid", "Can't find Projection");
		return "";
	}

	char* pszProjection = papszTokens[iProjection+1];
	if(EQUAL(pszProjection, "地理坐标系"))
	{
		oSRS.exportToWkt( &pszWkt );
		CSLDestroy( papszTokens );
		return CPLString(pszWkt);
	}

	int iProjectionType = CSLFindString( papszProjectionName, pszProjection );
	if (iProjectionType<=0)
	{
		oSRS.exportToWkt( &pszWkt );
		CSLDestroy( papszTokens );
		return CPLString(pszWkt);
	}

	int iParametersIndex = nProjectionParametersIndex[iProjectionType];

	int iParameters = CSLFindString( papszTokens, "Parameters" );
	if ( iParameters<0 || iParameters+1 >= nTokens)
	{
		CSLDestroy( papszTokens );
		CPLDebug( "CNSDTF Grid", "Can't find Projection Parameters");
		return "";
	}

	char* pszParameters = papszTokens[iParameters+1];
	char** papszParametersTokens = CSLTokenizeString2( pszParameters,  ",，", CSLT_ALLOWEMPTYTOKENS);
	int nParametersTokens = CSLCount(papszParametersTokens);
	if(nParametersTokens != 10)
	{
		CSLDestroy( papszParametersTokens );
		CSLDestroy( papszTokens );
		CPLDebug( "CNSDTF Grid", "Parse projection parameters error, the count should be 10, but now is %d", nParametersTokens);
		return "";
	}

	double dParmeters[10] = {0.0};
	for (int i=0; i<10; i++)
		dParmeters[i] = CPLAtofM(papszParametersTokens[i]);

	switch(iProjectionType)
	{
	case 1:	//高斯-克吕格
		oSRS.SetTM(0.0, dParmeters[0], dParmeters[5], dParmeters[6], dParmeters[7]);
		break;
	case 2:	//兰勃特正形割圆锥
		oSRS.SetLCC(dParmeters[2], dParmeters[3], dParmeters[1], dParmeters[0], dParmeters[6], dParmeters[7]);
		break;
	case 3:	//兰勃特正形切圆锥
		oSRS.SetLCC(dParmeters[1], dParmeters[1], dParmeters[1], dParmeters[0], dParmeters[6], dParmeters[7]);
		break;
	case 4:	//兰勃特等积方位
		oSRS.SetLAEA(dParmeters[1], dParmeters[0], dParmeters[6], dParmeters[7]);
		break;
	case 5:	//亚尔勃斯等积割圆锥
		oSRS.SetACEA(dParmeters[2], dParmeters[3], dParmeters[1], dParmeters[0], dParmeters[6], dParmeters[7]);
		break;
	case 6:	//亚尔勃斯等积切圆锥
		oSRS.SetACEA(dParmeters[1], dParmeters[1], dParmeters[1], dParmeters[0], dParmeters[6], dParmeters[7]);
		break;
	case 7:	//通用横轴墨卡托
		{
			int dZone = ((int)(dParmeters[0]) + 183) / 6;
			int bIsNorth = TRUE;
			if(dParmeters[7] >= 10000000)
				bIsNorth = FALSE;

			oSRS.SetUTM(dZone, bIsNorth);
		}
		break;
	case 8:	//墨卡托正轴等角切圆柱
		oSRS.SetMercator(0.0, dParmeters[0], dParmeters[5], dParmeters[6], dParmeters[7]);
		break;
	case 9:	//墨卡托正轴等角割圆柱
		oSRS.SetMercator(dParmeters[1], dParmeters[0], dParmeters[5], dParmeters[6], dParmeters[7]);
		break;
	case 10:	//波斯托等距切方位
		oSRS.SetAE(dParmeters[1], dParmeters[0], dParmeters[6], dParmeters[7]);
		break;
	case 11:	//彭纳等积伪圆锥
		// dont know which one is match
		break;
	case 12:	//等积正轴切圆柱
		oSRS.SetCEA(0.0, dParmeters[0], dParmeters[6], dParmeters[7]);
		break;
	case 13:	//等积正轴割圆柱
		oSRS.SetCEA(dParmeters[1], dParmeters[0], dParmeters[6], dParmeters[7]);
		break;
	case 14:	//等距正轴切圆锥
		oSRS.SetMC(dParmeters[1], dParmeters[0], dParmeters[6], dParmeters[7]);
		break;
	case 15:	//等距正轴割圆锥
		oSRS.SetEC(dParmeters[2], dParmeters[3], dParmeters[1], dParmeters[0], dParmeters[6], dParmeters[7]);
		break;
	case 16:	//等积斜轴切方位
		// dont know which one is match
		break;
	default:
		{
			CSLDestroy( papszParametersTokens );
			CSLDestroy( papszTokens );
			CPLDebug( "CNSDTF Grid", "Can not support this projection %s", papszProjectionName);
			return "";
		}
	}

	CSLDestroy( papszParametersTokens );

	oSRS.exportToWkt( &pszWkt );
	CSLDestroy( papszTokens );
	return CPLString(pszWkt);
}

/************************************************************************/
/*                        ParseOSR2Header()                             */
/************************************************************************/

int ParseOSR2Header(const char* pszProjection, char** papszHeader)
{
	return TRUE;
}
