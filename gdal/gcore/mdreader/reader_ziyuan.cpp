/******************************************************************************
 *
 * Project:  GDAL Core
 * Purpose:  Read metadata from ZiYuan imagery.
 * Author:   Minlu Li, liminlu0314@gmail.com
 *
 ******************************************************************************
 * Copyright (c) 2017 Minlu Li <liminlu0314@gmail.com>
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

#include "reader_ziyuan.h"

#include <cstdio>
#include <cstdlib>

#include <string>

#include "cpl_conv.h"
#include "cpl_error.h"
#include "cpl_string.h"
#include "gdal_mdreader.h"

CPL_CVSID("$Id$");

/**
 * GDALMDReaderZiYuan3()
 */
GDALMDReaderZiYuan3::GDALMDReaderZiYuan3(const char *pszPath,
	char **papszSiblingFiles) :
	GDALMDReaderBase(pszPath, papszSiblingFiles),
	m_osXMLSourceFilename(GDALFindAssociatedFile(pszPath, "xml", papszSiblingFiles, 0)),
	m_osRPBSourceFilename(GDALFindAssociatedFile(pszPath, "rpb", papszSiblingFiles, 0)),
	m_osRPCSourceFilename(GDALFindAssociatedFile(pszPath, "_rpc.txt", papszSiblingFiles, 0))
{
	if (!m_osXMLSourceFilename.empty())
		CPLDebug("MDReaderZiYuan", "IMD Filename: %s", m_osXMLSourceFilename.c_str());
	if (!m_osRPBSourceFilename.empty())
		CPLDebug("MDReaderZiYuan", "RPB Filename: %s", m_osRPBSourceFilename.c_str());
	if (!m_osRPCSourceFilename.empty())
		CPLDebug("MDReaderZiYuan", "RPC Filename: %s", m_osRPCSourceFilename.c_str());
}

/**
 * ~GDALMDReaderZiYuan3()
 */
GDALMDReaderZiYuan3::~GDALMDReaderZiYuan3()
{
}

/**
 * HasRequiredFiles()
 */
bool GDALMDReaderZiYuan3::HasRequiredFiles() const
{
	if (m_osXMLSourceFilename.empty())
		return false;

	if (m_osRPBSourceFilename.empty() && m_osRPCSourceFilename.empty())
		return false;

	if (GDALCheckFileHeader(m_osXMLSourceFilename, "<SatelliteID>ZY3"))
		return true;

	return false;
}

/**
 * GetMetadataFiles()
 */
char** GDALMDReaderZiYuan3::GetMetadataFiles() const
{
	char **papszFileList = NULL;
	if (!m_osXMLSourceFilename.empty())
		papszFileList = CSLAddString(papszFileList, m_osXMLSourceFilename);
	if (!m_osRPBSourceFilename.empty())
		papszFileList = CSLAddString(papszFileList, m_osRPBSourceFilename);
	if (!m_osRPCSourceFilename.empty())
		papszFileList = CSLAddString(papszFileList, m_osRPCSourceFilename);

	return papszFileList;
}

/**
 * LoadMetadata()
 */
void GDALMDReaderZiYuan3::LoadMetadata()
{
	if (m_bIsMetadataLoad)
		return;

	CPLXMLNode* psNode = CPLParseXMLFile(m_osXMLSourceFilename);

	if (psNode != NULL)
	{
		CPLXMLNode* pRootNode = CPLSearchXMLNode(psNode, "=sensor_corrected_metadata");
		if (pRootNode != NULL)
		{
			m_papszIMDMD = ReadXMLToList(pRootNode->psChild, m_papszIMDMD);
		}
		CPLDestroyXMLNode(psNode);
	}

	m_papszDEFAULTMD = CSLAddNameValue(m_papszDEFAULTMD, MD_NAME_MDTYPE, "ZiYuan");

	if (!m_osRPBSourceFilename.empty())
		m_papszRPCMD = GDALLoadRPBFile(m_osRPBSourceFilename.c_str());
	if (!m_osRPCSourceFilename.empty())
		m_papszRPCMD = GDALLoadRPCFile(m_osRPCSourceFilename.c_str());

	m_bIsMetadataLoad = true;

	const char* pszSatId = CSLFetchNameValue(m_papszIMDMD, "ProductInfo.SatelliteID");
	if (NULL != pszSatId)
	{
		m_papszIMAGERYMD = CSLAddNameValue(m_papszIMAGERYMD, MD_NAME_SATELLITE, pszSatId);
	}

	const char* pszCloudCover = CSLFetchNameValue(m_papszIMDMD, "ProductInfo.CloudPercent");
	if (NULL != pszCloudCover)
	{
		int nCC = atoi(pszCloudCover);
		if (nCC >= 99)
		{
			m_papszIMAGERYMD = CSLAddNameValue(m_papszIMAGERYMD, MD_NAME_CLOUDCOVER,
				MD_CLOUDCOVER_NA);
		}
		else
		{
			m_papszIMAGERYMD = CSLAddNameValue(m_papszIMAGERYMD,
				MD_NAME_CLOUDCOVER, CPLSPrintf("%d", nCC));
		}
	}

	const char* pszDate = CSLFetchNameValue(m_papszIMDMD, "ProductInfo.AcquistionTime");
	if (NULL != pszDate)
	{
		char buffer[40];
		time_t timeMid = GetAcquisitionTimeFromString(pszDate);
		strftime(buffer, 40, MD_DATETIMEFORMAT, localtime(&timeMid));
		m_papszIMAGERYMD = CSLAddNameValue(m_papszIMAGERYMD,
			MD_NAME_ACQDATETIME, buffer);
	}
	else
	{
		pszDate = CSLFetchNameValue(m_papszIMDMD, "ProductInfo.TimeStamp.CenterTime");
		if (NULL != pszDate)
		{
			char buffer[40];
			time_t timeMid = GetAcqisitionTimeFromString1(pszDate);
			strftime(buffer, 40, MD_DATETIMEFORMAT, localtime(&timeMid));
			m_papszIMAGERYMD = CSLAddNameValue(m_papszIMAGERYMD,
				MD_NAME_ACQDATETIME, buffer);
		}
	}
}

/**
 * GetAcqisitionTimeFromString()
 */
time_t GDALMDReaderZiYuan3::GetAcquisitionTimeFromString(
	const char* pszDateTime)
{
	if (NULL == pszDateTime)
		return 0;

	int iYear;
	int iMonth;
	int iDay;
	int iHours;
	int iMin;
	int iSec;

	int r = sscanf(pszDateTime, "%4d-%2d-%2d %d:%d:%d",
		&iYear, &iMonth, &iDay, &iHours, &iMin, &iSec);

	if (r != 6)
		return 0;

	struct tm tmDateTime;
	tmDateTime.tm_sec = iSec;
	tmDateTime.tm_min = iMin;
	tmDateTime.tm_hour = iHours;
	tmDateTime.tm_mday = iDay;
	tmDateTime.tm_mon = iMonth - 1;
	tmDateTime.tm_year = iYear - 1900;
	tmDateTime.tm_isdst = -1;

	return mktime(&tmDateTime);
}

/**
* GetAcqisitionTimeFromString1()
*/
time_t GDALMDReaderZiYuan3::GetAcqisitionTimeFromString1(const char* pszDateTime)
{
	if (NULL == pszDateTime)
		return 0;

	int iYear;
	int iMonth;
	int iDay;
	int iHours;
	int iMin;
	int iSec;

	int r = sscanf(pszDateTime, "%4d%2d%2d%2d%2d%2d.%*d",
		&iYear, &iMonth, &iDay, &iHours, &iMin, &iSec);

	if (r != 6)
		return 0;

	struct tm tmDateTime;
	tmDateTime.tm_sec = iSec;
	tmDateTime.tm_min = iMin;
	tmDateTime.tm_hour = iHours;
	tmDateTime.tm_mday = iDay;
	tmDateTime.tm_mon = iMonth - 1;
	tmDateTime.tm_year = iYear - 1900;
	tmDateTime.tm_isdst = -1;

	return mktime(&tmDateTime);

}


GDALMDReaderZiYuan02C::GDALMDReaderZiYuan02C(const char *pszPath,
	char **papszSiblingFiles) :
	GDALMDReaderBase(pszPath, papszSiblingFiles),
	m_osXMLSourceFilename(GDALFindAssociatedFile(pszPath, "xml", papszSiblingFiles, 0)),
	m_osRPBSourceFilename(GDALFindAssociatedFile(pszPath, "rpb", papszSiblingFiles, 0))
{
	if (m_osXMLSourceFilename.empty())
	{
		CPLString strBasename = CPLGetBasename(pszPath);
		strBasename = strBasename.substr(0, strBasename.rfind("-HR"));
		strBasename = CPLFormFilename(CPLGetDirname(pszPath), strBasename.c_str(), ".xml");
		m_osXMLSourceFilename = GDALFindAssociatedFile(strBasename.c_str(), "xml", papszSiblingFiles, 0);
	}
}

GDALMDReaderZiYuan02C::~GDALMDReaderZiYuan02C()
{

}

bool GDALMDReaderZiYuan02C::HasRequiredFiles() const
{
	if (m_osXMLSourceFilename.empty() || m_osRPBSourceFilename.empty())
		return false;

	if (GDALCheckFileHeader(m_osXMLSourceFilename, "<SatelliteID>ZY02C</SatelliteID>"))
		return true;

	return false;
}

char** GDALMDReaderZiYuan02C::GetMetadataFiles() const
{
	char **papszFileList = NULL;
	if (!m_osXMLSourceFilename.empty())
		papszFileList = CSLAddString(papszFileList, m_osXMLSourceFilename);
	if (!m_osRPBSourceFilename.empty())
		papszFileList = CSLAddString(papszFileList, m_osRPBSourceFilename);

	return papszFileList;
}

void GDALMDReaderZiYuan02C::LoadMetadata()
{
	if (m_bIsMetadataLoad)
		return;

	CPLXMLNode* psNode = CPLParseXMLFile(m_osXMLSourceFilename);

	if (psNode != NULL)
	{
		CPLXMLNode* pRootNode = CPLSearchXMLNode(psNode, "=ProductMetaData");
		if (pRootNode != NULL)
		{
			m_papszIMDMD = ReadXMLToList(pRootNode->psChild, m_papszIMDMD);
		}
		CPLDestroyXMLNode(psNode);
	}

	m_papszDEFAULTMD = CSLAddNameValue(m_papszDEFAULTMD, MD_NAME_MDTYPE, "ZiYuan02C");

	m_papszRPCMD = GDALLoadRPBFile(m_osRPBSourceFilename.c_str());

	m_bIsMetadataLoad = true;

	const char* pszSatId = CSLFetchNameValue(m_papszIMDMD, "SatelliteID");
	if (NULL != pszSatId)
	{
		m_papszIMAGERYMD = CSLAddNameValue(m_papszIMAGERYMD, MD_NAME_SATELLITE, pszSatId);
	}

	const char* pszCloudCover = CSLFetchNameValue(m_papszIMDMD, "CloudPercent");
	if (NULL != pszCloudCover)
	{
		int nCC = atoi(pszCloudCover);
		if (nCC >= 99)
		{
			m_papszIMAGERYMD = CSLAddNameValue(m_papszIMAGERYMD, MD_NAME_CLOUDCOVER,
				MD_CLOUDCOVER_NA);
		}
		else
		{
			m_papszIMAGERYMD = CSLAddNameValue(m_papszIMAGERYMD,
				MD_NAME_CLOUDCOVER, CPLSPrintf("%d", nCC));
		}
	}

	const char* pszDate = CSLFetchNameValue(m_papszIMDMD, "CenterTime");

	if (NULL != pszDate)
	{
		char buffer[40];
		time_t timeMid = GetAcquisitionTimeFromString(pszDate);
		strftime(buffer, 40, MD_DATETIMEFORMAT, localtime(&timeMid));
		m_papszIMAGERYMD = CSLAddNameValue(m_papszIMAGERYMD,
			MD_NAME_ACQDATETIME, buffer);
	}
}

time_t GDALMDReaderZiYuan02C::GetAcquisitionTimeFromString(
	const char* pszDateTime)
{
	if (NULL == pszDateTime)
		return 0;

	int iYear;
	int iMonth;
	int iDay;
	int iHours;
	int iMin;
	int iSec;

	int r = sscanf(pszDateTime, "%4d-%2d-%2d %d:%d:%d",
		&iYear, &iMonth, &iDay, &iHours, &iMin, &iSec);

	if (r != 6)
		return 0;

	struct tm tmDateTime;
	tmDateTime.tm_sec = iSec;
	tmDateTime.tm_min = iMin;
	tmDateTime.tm_hour = iHours;
	tmDateTime.tm_mday = iDay;
	tmDateTime.tm_mon = iMonth - 1;
	tmDateTime.tm_year = iYear - 1900;
	tmDateTime.tm_isdst = -1;

	return mktime(&tmDateTime);
}
