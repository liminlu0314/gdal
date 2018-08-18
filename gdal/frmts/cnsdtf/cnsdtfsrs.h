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

#ifndef _CNSDTFSRS_H_INCLUDED
#define _CNSDTFSRS_H_INCLUDED

#include "ogr_spatialref.h"
#include "cpl_string.h"

int GetDataBuffer(VSILFILE* fp, const char* pszBegin, const char* pszEnd, char** papszBuf);
CPLString ParseSpatialReference(const char* pszHeader);
int ParseOSR2Header(const char* pszProjection, char** papszHeader);

#endif /* _CNSDTFSRS_H_INCLUDED */
