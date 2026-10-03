// DistributeTools.cpp - Object Distribution Tools Implementation

#include "StdAfx.h"
#include "ArqaTools.h"
#include "DistributeTools.h"
#include "CommonTools.h"

namespace DistributeTools
{

    using CommonTools::GetEntityReferencePoint;
    using CommonTools::MoveEntityOrGroup;
    using CommonTools::CopyEntityTo;

    // -------------------------------------------------------------------------
    // PromptLineVector: prompt for start/end points, validate distance > 0.001,
    // and output the unit direction vector and total distance.
    // Returns false on cancellation or degenerate line.
    // -------------------------------------------------------------------------
    static bool PromptLineVector(AcGePoint3d& startPt, AcGeVector3d& unitVector,
                                 double& totalDistance)
    {
        ads_point pt1, pt2;
        if (acedGetPoint(NULL, _T("\nSpecify start point: "), pt1) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return false; }
        if (acedGetPoint(pt1, _T("\nSpecify end point: "), pt2) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return false; }

        startPt = AcGePoint3d(pt1[0], pt1[1], pt1[2]);
        AcGeVector3d v = AcGePoint3d(pt2[0], pt2[1], pt2[2]) - startPt;
        totalDistance  = v.length();

        if (totalDistance < 0.001)
        { acutPrintf(_T("\nStart and end points are too close.\n")); return false; }

        unitVector = v.normal();
        acutPrintf(_T("Distribution distance: %.2f\n"), totalDistance);
        return true;
    }

    // -------------------------------------------------------------------------
    // DistributeCommand: shared body for DISTLINE / DISTBETWEEN / DISTEQUAL.
    //
    //   mode 0  Linear  — objects land on endpoints:    spacing = D/(N-1),  pos[i] = start + u*spacing*i
    //   mode 1  Between — objects skip endpoints:       spacing = D/(N+1),  pos[i] = start + u*spacing*(i+1)
    //   mode 2  Equal   — half-space at each end:       spacing = D/N,      pos[i] = start + u*spacing*(i+0.5)
    //
    // CC=5  CogC=5  Nesting=2
    // -------------------------------------------------------------------------
    static void DistributeCommand(int mode)
    {
        static const TCHAR* kHeaders[] = {
            _T("\n=== DISTRIBUTE ALONG LINE ===\n"),
            _T("\n=== DISTRIBUTE BETWEEN POINTS ===\n"),
            _T("\n=== DISTRIBUTE EQUAL SPACING ===\n") };
        acutPrintf(kHeaders[mode]);

        CommonTools::SelectionSetGuard ssGuard;
        if (!ssGuard.Get()) { acutPrintf(CommonTools::MSG_NO_SELECTION); return; }

        Adesk::Int32 length;
        acedSSLength(ssGuard.ss, &length);
        int minSelect = (mode == 0) ? 2 : 1;
        if (length < minSelect)
        { acutPrintf(_T("\nNeed at least %d object(s) to distribute.\n"), minSelect); return; }
        acutPrintf(_T("Selected %d objects.\n"), length);

        AcGePoint3d  startPt;
        AcGeVector3d unitVector;
        double       totalDistance;
        if (!PromptLineVector(startPt, unitVector, totalDistance)) return;

        std::vector<AcDbObjectId> ids;
        CommonTools::ForEachSsEntity(ssGuard.ss, length, [&](AcDbObjectId objId) { ids.push_back(objId); });

        double spacing = 0.0;
        int numObjects = DistributeObjects(ids, startPt, startPt + unitVector * totalDistance,
                                           mode, true, &spacing);
        if (numObjects < 0)
        { acutPrintf(_T("\nNeed at least %d valid object(s).\n"), minSelect); return; }

        if (mode == 2)
            acutPrintf(_T("\nDone! Equal spacing %.2f (%.2f from ends).\n"), spacing, spacing * 0.5);
        else if (mode == 1)
            acutPrintf(_T("\nDone! Spacing %.2f between points.\n"), spacing);
        else
            acutPrintf(_T("\nDone! Spacing %.2f.\n"), spacing);
    }

    // Spacing divisor and first-slot offset for each mode (see DistributeCommand).
    static void ModeSpacing(int mode, int count, double total, double& spacing, double& offset0)
    {
        double divisor = (mode == 0) ? (count - 1)
                       : (mode == 1) ? (count + 1)
                       :                count;
        offset0 = (mode == 1) ? 1.0 : (mode == 2) ? 0.5 : 0.0;
        spacing = total / divisor;
    }

    int DistributeObjects(const std::vector<AcDbObjectId>& ids,
                          const AcGePoint3d& startPt, const AcGePoint3d& endPt,
                          int mode, bool verbose, double* spacingOut)
    {
        AcGeVector3d v = endPt - startPt;
        double totalDistance = v.length();
        if (totalDistance < 0.001) return -1;
        AcGeVector3d unitVector = v.normal();

        auto groupMap = CommonTools::BuildEntityGroupMap(
            acdbHostApplicationServices()->workingDatabase());

        // Deduplicate by group, keep (id, reference point) pairs.
        AcArray<AcDbObjectId> objectIds;
        AcArray<AcGePoint3d>  refPoints;
        AcDbObjectIdArray seenGroups;
        for (AcDbObjectId objId : ids)
        {
            auto it = groupMap.find(objId);
            if (it != groupMap.end())
            {
                if (seenGroups.contains(it->second)) continue;
                seenGroups.append(it->second);
            }
            AcGePoint3d refPt;
            if (GetEntityReferencePoint(objId, refPt))
            { objectIds.append(objId); refPoints.append(refPt); }
        }

        int numObjects = objectIds.length();
        int minSelect = (mode == 0) ? 2 : 1;
        if (numObjects < minSelect) return -1;
        if (verbose) acutPrintf(_T("Distributing %d objects...\n"), numObjects);

        double spacing, offset0;
        ModeSpacing(mode, numObjects, totalDistance, spacing, offset0);

        AcDbObjectIdArray processedGroups;
        for (int i = 0; i < numObjects; i++)
        {
            AcGePoint3d target = startPt + unitVector * (spacing * (i + offset0));
            MoveEntityOrGroup(objectIds[i], target - refPoints[i], processedGroups, groupMap);
            if (verbose) acutPrintf(_T("  [%d] Moved to %.2f, %.2f\n"), i + 1, target.x, target.y);
        }
        if (spacingOut) *spacingOut = spacing;
        return numObjects;
    }

    std::vector<AcDbObjectId> DistributeCopies(AcDbObjectId srcId, int count,
                                               const AcGePoint3d& startPt, const AcGePoint3d& endPt,
                                               int mode, double* spacingOut)
    {
        std::vector<AcDbObjectId> newIds;
        double total = startPt.distanceTo(endPt);
        int minCount = (mode == 0) ? 2 : 1;
        if (total < 0.001 || count < minCount) return newIds;

        AcGeVector3d dir = (endPt - startPt).normal();
        double spacing, offset0;
        ModeSpacing(mode, count, total, spacing, offset0);

        for (int i = 0; i < count; i++)
        {
            AcDbObjectId id = CopyEntityTo(srcId, startPt + dir * (spacing * (i + offset0)));
            if (!id.isNull()) newIds.push_back(id);
        }
        if (spacingOut) *spacingOut = spacing;
        return newIds;
    }

    void distributeLinearCommand()  { DistributeCommand(0); }
    void distributeBetweenCommand() { DistributeCommand(1); }
    void distributeEqualCommand()   { DistributeCommand(2); }

    // -------------------------------------------------------------------------
    // CopyDistributeCommand: shared body for DISTCOPYLINE / DISTCOPYBETWEEN /
    // DISTCOPYEQUAL. Same mode encoding as DistributeCommand.
    // CC=4  CogC=4  Nesting=2
    // -------------------------------------------------------------------------
    static void CopyDistributeCommand(int mode)
    {
        static const TCHAR* kHeaders[] = {
            _T("\n=== COPY & DISTRIBUTE ALONG LINE ==="),
            _T("\n=== COPY & DISTRIBUTE BETWEEN POINTS ==="),
            _T("\n=== COPY & DISTRIBUTE EQUAL SPACING ===") };
        acutPrintf(kHeaders[mode]);

        ads_name ent; ads_point pt;
        if (acedEntSel(_T("\nSelect source object: "), ent, pt) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return; }
        AcDbObjectId srcId;
        acdbGetObjectId(srcId, ent);

        int minCount   = (mode == 0) ? 2 : 1;
        int count      = minCount;
        const TCHAR* countPrompt = (mode == 0) ? _T("\nNumber of copies (total): ")
                                               : _T("\nNumber of copies: ");
        if (acedGetInt(countPrompt, &count) != RTNORM || count < minCount)
        { acutPrintf(_T("\nNeed at least %d."), minCount); return; }

        AcGePoint3d  startPt;
        AcGeVector3d unitVector;
        double       total;
        if (!PromptLineVector(startPt, unitVector, total)) return;

        double spacing = 0.0;
        DistributeCopies(srcId, count, startPt, startPt + unitVector * total, mode, &spacing);

        if (mode == 2)
            acutPrintf(_T("\n%d copies placed, spacing %.2f (%.2f from ends)."), count, spacing, spacing * 0.5);
        else
            acutPrintf(_T("\n%d copies placed, spacing %.2f."), count, spacing);
    }

    void distributeCopyLinearCommand()  { CopyDistributeCommand(0); }
    void distributeCopyBetweenCommand() { CopyDistributeCommand(1); }
    void distributeCopyEqualCommand()   { CopyDistributeCommand(2); }

    // -------------------------------------------------------------------------
    // DISTTOLINE - Copy N objects distributed along a picked line entity.
    // Internally parametrized by mode; no further factoring needed.
    // -------------------------------------------------------------------------
    void alignToLineCommand()
    {
        acutPrintf(_T("\n=== ALIGN TO LINE ==="));

        ads_name ent; ads_point pt;
        if (acedEntSel(_T("\nSelect source object to copy: "), ent, pt) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return; }
        AcDbObjectId srcId;
        acdbGetObjectId(srcId, ent);

        ads_name lineEnt; ads_point linePt;
        if (acedEntSel(_T("\nSelect line to align along: "), lineEnt, linePt) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return; }
        AcDbObjectId lineId;
        acdbGetObjectId(lineId, lineEnt);

        CommonTools::AcDbObjectGuard<AcDbCurve> axis(lineId);
        if (!axis)
        { acutPrintf(_T("\nError: Cannot open line entity.")); return; }
        AcGePoint3d startPt, endPt;
        axis->getStartPoint(startPt);
        axis->getEndPoint(endPt);

        TCHAR modeBuf[16] = _T("L");
        int modeResult = acedGetString(0, _T("\nMode [L]inear/[B]etween/[E]qual <L>: "), modeBuf);
        if (modeResult != RTNORM && modeResult != RTNONE)
        { acutPrintf(CommonTools::MSG_CANCELLED); return; }
        CString mode(modeBuf); mode.MakeUpper();
        if (mode.IsEmpty()) mode = _T("L");

        int count = 2;
        if (acedGetInt(_T("\nNumber of copies: "), &count) != RTNORM || count < 1)
        { acutPrintf(_T("\nNeed at least 1 copy.")); return; }

        double spacing = 0.0;
        if (mode == _T("B"))
        {
            DistributeCopies(srcId, count, startPt, endPt, 1, &spacing);
            acutPrintf(_T("\n%d copies placed between endpoints, spacing %.2f."), count, spacing);
        }
        else if (mode == _T("E"))
        {
            DistributeCopies(srcId, count, startPt, endPt, 2, &spacing);
            acutPrintf(_T("\n%d copies, equal spacing %.2f (%.2f from ends)."), count, spacing, spacing * 0.5);
        }
        else
        {
            if (count < 2) { acutPrintf(_T("\nLinear mode needs at least 2 copies.")); return; }
            DistributeCopies(srcId, count, startPt, endPt, 0, &spacing);
            acutPrintf(_T("\n%d copies from start to end, spacing %.2f."), count, spacing);
        }
    }

} // namespace DistributeTools
