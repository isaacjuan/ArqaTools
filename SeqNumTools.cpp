// SeqNumTools.cpp - Sequence Number Tools Implementation

#include "StdAfx.h"
#include "ArqaTools.h"
#include "SeqNumTools.h"
#include "CommonTools.h"

namespace SeqNumTools
{
    // -------------------------------------------------------------------------
    // NextSeqGroupNum
    // Returns the next unique SEQNUM group number using a session-persistent
    // counter.  Seeded lazily on first call by scanning the group dictionary
    // once (O(G)); all subsequent calls are O(1).
    // -------------------------------------------------------------------------
    static int s_seqGroupCounter = -1; // -1 = uninitialized

    static int NextSeqGroupNum(AcDbDatabase* pDb)
    {
        if (s_seqGroupCounter < 0)
        {
            s_seqGroupCounter = 0;
            AcDbDictionary* pGroupDictRaw;
            if (pDb->getGroupDictionary(pGroupDictRaw, AcDb::kForRead) == Acad::eOk)
            {
                CommonTools::AcDbDictionaryGuard pGroupDict(pGroupDictRaw);
                CommonTools::AcDbIteratorGuard<AcDbDictionaryIterator> iter(pGroupDict->newIterator());
                for (; !iter->done(); iter->next())
                {
                    const TCHAR* name = iter->name();
                    if (_tcsstr(name, _T("SEQNUM_")) == name)
                    {
                        int num = _ttoi(name + 7);
                        if (num > s_seqGroupCounter)
                            s_seqGroupCounter = num;
                    }
                }
            }
        }
        return ++s_seqGroupCounter;
    }

    // Helper: Create and add circle entity to model space, returns ObjectId
    static AcDbObjectId CreateCircle(const AcGePoint3d& center, double radius, AcDbBlockTableRecord* pModelSpace)
    {
        AcDbCircle* pCircle = new AcDbCircle(center, AcGeVector3d::kZAxis, radius);
        pCircle->setColorIndex(2); // Yellow
        
        return CommonTools::AppendEntity(pModelSpace, pCircle);
    }

    // Helper: Create and add centered text entity to model space, returns ObjectId
    // If circleRadius > 0, adjusts widthFactor to ensure text width <= 80% of circle diameter
    static AcDbObjectId CreateCenteredText(const AcGePoint3d& position, const TCHAR* text, 
                                    double height, double circleRadius, AcDbBlockTableRecord* pModelSpace)
    {
        AcDbText* pText = new AcDbText();
        pText->setPosition(position);
        pText->setTextString(text);
        pText->setHeight(height);
        pText->setWidthFactor(1.0); // Start with standard width
        pText->setColorIndex(7); // White
        pText->setHorizontalMode(AcDb::kTextCenter);
        pText->setVerticalMode(AcDb::kTextVertMid);
        pText->setAlignmentPoint(position);
        
        // If we have a circle, measure actual text width and adjust if needed
        if (circleRadius > 0.0)
        {
            AcDbExtents extents;
            if (pText->getGeomExtents(extents) == Acad::eOk)
            {
                // Calculate actual text width from bounding box
                double actualWidth = extents.maxPoint().x - extents.minPoint().x;
                
                // Maximum allowed width is 80% of circle diameter
                const double MAX_WIDTH_RATIO = 0.8;
                double maxWidth = circleRadius * 2.0 * MAX_WIDTH_RATIO;

                // If text is too wide, compress it
                if (actualWidth > maxWidth)
                    pText->setWidthFactor(maxWidth / actualWidth);
            }
        }

        return CommonTools::AppendEntity(pModelSpace, pText);
    }

    // Helper: Create a group containing circle and text
    static void CreateNumberGroup(AcDbObjectId circleId, AcDbObjectId textId)
    {
        AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
        AcDbDictionary* pGroupDictRaw;
        if (pDb->getGroupDictionary(pGroupDictRaw, AcDb::kForWrite) != Acad::eOk)
            return;
        CommonTools::AcDbDictionaryGuard pGroupDict(pGroupDictRaw);

        TCHAR groupName[50];
        _stprintf_s(groupName, 50, _T("SEQNUM_%d"), NextSeqGroupNum(pDb));
        
        // Create the group
        AcDbGroup* pGroup = new AcDbGroup(groupName);
        pGroup->setSelectable(true);
        
        // Add entities to group
        pGroup->append(circleId);
        pGroup->append(textId);
        
        // Add group to dictionary
        AcDbObjectId groupId;
        pGroupDict->setAt(groupName, pGroup, groupId);
        
        pGroup->close();
        // pGroupDict closed automatically by AcDbDictionaryGuard destructor
    }

    AcDbObjectId CreateSeqNumber(const AcGePoint3d& center, const CString& text,
                                 double height, bool withCircle, AcDbObjectId* circleId)
    {
        if (circleId) *circleId = AcDbObjectId::kNull;

        AcDbBlockTableRecord* pModelSpace = nullptr;
        if (CommonTools::GetModelSpace(pModelSpace) != Acad::eOk)
            return AcDbObjectId::kNull;

        // Golden-ratio sizing: circle radius = 1.618 x text height.
        const double GOLDEN_RATIO = 1.618;
        double circleRadius = height * GOLDEN_RATIO;

        AcDbObjectId cId;
        if (withCircle)
            cId = CreateCircle(center, circleRadius, pModelSpace);
        AcDbObjectId textId = CreateCenteredText(center, text, height,
                                                 withCircle ? circleRadius : 0.0, pModelSpace);
        pModelSpace->close();

        if (withCircle)
        {
            CreateNumberGroup(cId, textId);
            if (circleId) *circleId = cId;
        }
        return textId;
    }
}
