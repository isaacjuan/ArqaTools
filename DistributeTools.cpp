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

    // Spacing and first-slot offset for each mode (count N items over distance D):
    //   mode 0  Linear  - items on both endpoints:  spacing = D/(N-1), pos[i] = start + u*spacing*i
    //   mode 1  Between - endpoints excluded:       spacing = D/(N+1), pos[i] = start + u*spacing*(i+1)
    //   mode 2  Equal   - half a gap at each end:   spacing = D/N,     pos[i] = start + u*spacing*(i+0.5)
    // The ATDIST* commands are Lua (LuaCommands/ATDISTRIBUTE.lua) on top of these cores.
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

} // namespace DistributeTools
