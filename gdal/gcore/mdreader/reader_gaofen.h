/******************************************************************************
 * $Id$
 *
 * Project:  GDAL Core
 * Purpose:  Read metadata from GaoFen imagery.
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

#ifndef READER_GAOFEN_H_INCLUDED
#define READER_GAOFEN_H_INCLUDED

<<<<<<< HEAD
#include "../gdal_mdreader.h"
=======
#include "reader_pleiades.h"
>>>>>>> 51b6d7e63e (添加对中国卫星数据元数据的支持，同时将加载RPC和保存RPC相关函数导出)

/**
Metadata reader for GaoFen

TIFF filename:      GF(1|2)_PMS1_sssssssssssssss-pppppppp_(PAN1|MSS1).tiff or
                    GF(1|2)_PMS2_sssssssssssssss-pppppppp_(PAN2|MSS2).tiff or
                    GF1_WFV(1|2|3|4)_sssssssssssssss-pppppppp.tiff
Metadata filename:  TIFF_BASENAME.xml
RPC filename:       TIFF_BASENAME.rpb

Common metadata (from metadata filename):
    AcquisitionDateTime: CenterTime
    SatelliteId:         SatelliteID
    CloudCover:          CloudPercent
*/

class GDALMDReaderGaoFen: public GDALMDReaderBase
{
public:
    GDALMDReaderGaoFen(const char *pszPath, char **papszSiblingFiles);
    virtual ~GDALMDReaderGaoFen();
    virtual bool HasRequiredFiles() const override;
    virtual char** GetMetadataFiles() const override;
protected:
    virtual void LoadMetadata() override;
    virtual time_t GetAcquisitionTimeFromString(const char* pszDateTime) override;
protected:
    CPLString m_osXMLSourceFilename;
    CPLString m_osRPBSourceFilename;
};

#endif // READER_GAOFEN_H_INCLUDED

