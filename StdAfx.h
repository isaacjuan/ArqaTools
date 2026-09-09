// StdAfx.h - Precompiled header for ArqaTools plugin

#if !defined(AFX_STDAFX_H__INCLUDED_)
#define AFX_STDAFX_H__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#pragma warning(disable: 4786)

//-----------------------------------------------------------------------------
#define STRICT

// ATL configuration — must be defined before any ATL/MFC headers
#define _ATL_APARTMENT_THREADED
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
#define _ATL_ALL_WARNINGS

#define _USE_MATH_DEFINES
#include <cmath>
#include <map>

// MFC core — must come before atlbase.h so MFC module state is initialized
#include <afxwin.h>     // MFC core and standard components
#include <afxext.h>     // MFC extensions
#include <afxcmn.h>     // MFC common controls (CListCtrl, CToolTipCtrl, CHeaderCtrl, CTabCtrl)
#include <afxrich.h>    // MFC rich edit (CRichEditCtrl)

#include <atlbase.h>
#include <atlcom.h>
#include <atlstr.h>

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
