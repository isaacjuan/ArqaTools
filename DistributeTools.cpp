// DistributeTools.cpp - Object Distribution Tools Implementation

#include "StdAfx.h"
#include "ArqaTools.h"
#include "DistributeTools.h"
#include "CommonTools.h"

namespace DistributeTools
{

    using CommonTools::GetEntityReferencePoint;
    using CommonTools::CopyEntityTo;

    int MinCount(int mode) { return mode == 0 ? 2 : 1; }

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
                          int mode, double* spacingOut)
    {
        AcGeVector3d v = endPt - startPt;
        double totalDistance = v.length();
        if (totalDistance < 0.001) return -1;
        AcGeVector3d unitVector = v.normal();

        // One item per object or group; a group is placed by the reference
        // point of its first selected member.
        std::vector<CommonTools::GroupUnit> units;
        std::vector<AcGePoint3d>            refPoints;
        for (CommonTools::GroupUnit& u : CommonTools::GroupUnits(ids))
        {
            AcGePoint3d refPt;
            if (!GetEntityReferencePoint(u.picked, refPt)) continue;
            units.push_back(std::move(u));
            refPoints.push_back(refPt);
        }

        int numObjects = static_cast<int>(units.size());
        if (numObjects < MinCount(mode)) return -1;

        double spacing, offset0;
        ModeSpacing(mode, numObjects, totalDistance, spacing, offset0);

        for (int i = 0; i < numObjects; i++)
        {
            AcGePoint3d target = startPt + unitVector * (spacing * (i + offset0));
            CommonTools::TranslateEntities(units[i].members, target - refPoints[i]);
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
        if (total < 0.001 || count < MinCount(mode)) return newIds;

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
