/******************************************************************************
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

#include "reader_common.h"

#include <cstdio>
#include <cstdlib>

#include <string>

#include "cpl_conv.h"
#include "cpl_error.h"
#include "cpl_string.h"
#include "gdal_mdreader.h"

CPL_CVSID("$Id$");

/**
 * GDALMDReaderCommon()
 */
GDALMDReaderCommon::GDALMDReaderCommon(const char *pszPath,
    char **papszSiblingFiles) :
    GDALMDReaderBase(pszPath, papszSiblingFiles),
    m_osRPBSourceFilename(GDALFindAssociatedFile(pszPath, "RPB",
        papszSiblingFiles, 0))
{
    const char* pszBaseName = CPLGetBasename(pszPath);
    const char* pszDirName = CPLGetDirname(pszPath);

    // get _rpc.txt file
    CPLString osRPCSourceFilename = CPLFormFilename(pszDirName,
        CPLSPrintf("%s_rpc", pszBaseName), "txt");

    if (CPLCheckForFile(&osRPCSourceFilename[0], papszSiblingFiles))
    {
        m_osRPCSourceFilename = osRPCSourceFilename;
    }
    else
    {
        osRPCSourceFilename = CPLFormFilename(pszDirName,
            CPLSPrintf("%s_RPC", pszBaseName), "TXT");
        if (CPLCheckForFile(&osRPCSourceFilename[0], papszSiblingFiles))
        {
            m_osRPCSourceFilename = osRPCSourceFilename;
        }
    }

    if (!m_osRPCSourceFilename.empty())
        CPLDebug("MDReaderTripleSat", "RPC Filename: %s",
            m_osRPCSourceFilename.c_str());
    if (!m_osRPBSourceFilename.empty())
        CPLDebug("MDReaderTripleSat", "RPB Filename: %s",
            m_osRPBSourceFilename.c_str());
}

/**
 * ~GDALMDReaderCommon()
 */
GDALMDReaderCommon::~GDALMDReaderCommon()
{
}

/**
 * HasRequiredFiles()
 */
bool GDALMDReaderCommon::HasRequiredFiles() const
{
    if (!m_osRPCSourceFilename.empty() || !m_osRPBSourceFilename.empty())
        return true;

    return false;
}

/**
 * GetMetadataFiles()
 */
char** GDALMDReaderCommon::GetMetadataFiles() const
{
    char **papszFileList = NULL;
    if (!m_osRPCSourceFilename.empty())
        papszFileList = CSLAddString(papszFileList, m_osRPCSourceFilename);
    if (!m_osRPBSourceFilename.empty())
        papszFileList = CSLAddString(papszFileList, m_osRPBSourceFilename);

    return papszFileList;
}

/**
 * LoadMetadata()
 */
void GDALMDReaderCommon::LoadMetadata()
{
    if (m_bIsMetadataLoad)
        return;

    m_papszDEFAULTMD = CSLAddNameValue(m_papszDEFAULTMD, MD_NAME_MDTYPE, "Common");

    if (!m_osRPBSourceFilename.empty())
        m_papszRPCMD = GDALLoadRPBFile(m_osRPBSourceFilename.c_str());
    if (!m_osRPCSourceFilename.empty())
        m_papszRPCMD = GDALLoadRPCFile(m_osRPCSourceFilename.c_str());

    m_bIsMetadataLoad = true;
}
