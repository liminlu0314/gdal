/******************************************************************************
 * $Id$
 *
 * Project:  GDAL Core
 * Purpose:  Read metadata from common imagery.
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

#ifndef READER_COMMON_H_INCLUDED
#define READER_COMMON_H_INCLUDED

#include "../gdal_mdreader.h"

 /**
 Metadata reader for Common

 TIFF filename:      *.tiff
 RPC filename:       *_rpc.txt
 RPB filename:       *.rpb
 */

class GDALMDReaderCommon : public GDALMDReaderBase
{
public:
    GDALMDReaderCommon(const char *pszPath, char **papszSiblingFiles);
    virtual ~GDALMDReaderCommon();
    virtual bool HasRequiredFiles() const override;
    virtual char** GetMetadataFiles() const override;
protected:
    virtual void LoadMetadata() override;
protected:
    CPLString m_osRPCSourceFilename;
    CPLString m_osRPBSourceFilename;
};

#endif // READER_COMMON_H_INCLUDED

