/******************************************************************************
 * $Id$
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

#ifndef READER_ZIYUAN_H_INCLUDED
#define READER_ZIYUAN_H_INCLUDED

#include "../gdal_mdreader.h"


/**
Metadata reader for ZY3		   ZY3_NAD_E126.2_N48.4_20150826_L1A0003204233

TIFF filename:      ZY3_NAD|FWD|BWD|MUX_sssssssssssssss-pppppppp.tif or
Metadata filename:  ZY3_NAD|FWD|BWD|MUX_sssssssssssssss-pppppppp.xml
RPC filename:       TIFF_BASENAME.rpb

Common metadata (from metadata filename):
AcquisitionDateTime: CenterTime
SatelliteId:         SatelliteID
CloudCover:          CloudPercent
*/
class GDALMDReaderZiYuan3 : public GDALMDReaderBase
{
public:
	GDALMDReaderZiYuan3(const char *pszPath, char **papszSiblingFiles);
	virtual ~GDALMDReaderZiYuan3();
    virtual bool HasRequiredFiles() const override;
    virtual char** GetMetadataFiles() const override;
protected:
    virtual void LoadMetadata() override;
    virtual time_t GetAcquisitionTimeFromString(const char* pszDateTime) override;
private:
	time_t GetAcqisitionTimeFromString1(const char* pszDateTime);
protected:
    CPLString m_osXMLSourceFilename;
	CPLString m_osRPBSourceFilename;
	CPLString m_osRPCSourceFilename;
};

/**
Metadata reader for ZY02C

TIFF filename:      ZY02C_PMS_sssssssssssssss-pppppppp_(PAN|MUX).tiff or
ZY02C_HRC_sssssssssssssss-pppppppp_(HR1|HR2).tiff
Metadata filename:  ZY02C_HRC_sssssssssssssss-pppppppp.xml
RPC filename:       TIFF_BASENAME.rpb

Common metadata (from metadata filename):
AcquisitionDateTime: CenterTime
SatelliteId:         SatelliteID
CloudCover:          CloudPercent
*/
class GDALMDReaderZiYuan02C : public GDALMDReaderBase
{
public:
	GDALMDReaderZiYuan02C(const char *pszPath, char **papszSiblingFiles);
	virtual ~GDALMDReaderZiYuan02C();
	virtual bool HasRequiredFiles() const override;
	virtual char** GetMetadataFiles() const override;
protected:
	virtual void LoadMetadata() override;
	virtual time_t GetAcquisitionTimeFromString(const char* pszDateTime) override;
private:
	time_t GetAcqisitionTimeFromString(const char* pszDateTime);
protected:
	CPLString m_osXMLSourceFilename;
	CPLString m_osRPBSourceFilename;
};

#endif // READER_ZIYUAN_H_INCLUDED
