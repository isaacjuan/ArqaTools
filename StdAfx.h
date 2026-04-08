// StdAfx.h - Precompiled header for HelloWorld plugin

#if !defined(AFX_STDAFX_H__INCLUDED_)
#define AFX_STDAFX_H__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#pragma warning(disable: 4786)

//-----------------------------------------------------------------------------
#define STRICT

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN			//----- Exclude rarely-used stuff from Windows headers
#endif

#pragma warning(suppress: 4467)
#include <afxwin.h>
#include <afxext.h>
#include <afxcmn.h>

#define _USE_MATH_DEFINES
#include <cmath>

// ObjectARX headers for AutoCAD 2025
#include "rxobject.h"
#include "rxregsvc.h"
#include "aced.h"
#include "adslib.h"
#include "dbmain.h"
#include "dbents.h"
#include "dbsymtb.h"
#include "acestext.h"
#include "acutads.h"
#include "geassign.h"
#include "acgi.h"
#include "dbapserv.h"
#include "arxHeaders.h"      // OMF support (includes acdbabb.h)

//-----------------------------------------------------------------------------
#endif // !defined(AFX_STDAFX_H__INCLUDED_)
