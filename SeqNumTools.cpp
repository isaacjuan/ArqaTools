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

    // Helper: Get real number input with default value
    static bool GetRealWithDefault(const TCHAR* prompt, double& value, double defaultValue)
    {
        int result = acedGetReal(prompt, &value);
        
        if (result == RTNONE)
        {
            value = defaultValue;
            return true;
        }
        
        return (result == RTNORM);
    }

    // Helper: Format number as string (considers step value for format)
    static void FormatNumber(double number, double step, TCHAR* buffer, size_t bufferSize)
    {
        // If step is a float (not an integer), always show 2 decimal places
        // This ensures consistent formatting (e.g., "1.00" when step is 0.5)
        if (step != floor(step))
        {
            _stprintf_s(buffer, bufferSize, _T("%.2f"), number);
        }
        else
        {
            // Step is integer, format based on actual number value
            if (number == floor(number))
                _stprintf_s(buffer, bufferSize, _T("%.0f"), number);
            else
                _stprintf_s(buffer, bufferSize, _T("%.2f"), number);
        }
    }

    // Helper: Create and add circle entity to model space, returns ObjectId
    static AcDbObjectId CreateCircle(const AcGePoint3d& center, double radius, AcDbBlockTableRecord* pModelSpace)
    {
        AcDbCircle* pCircle = new AcDbCircle(center, AcGeVector3d::kZAxis, radius);
        pCircle->setColorIndex(2); // Yellow
        
        AcDbObjectId circleId;
        pModelSpace->appendAcDbEntity(circleId, pCircle);
        pCircle->close();
        
        return circleId;
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
                
                acutPrintf(_T("  [WIDTH CHECK] Text='%s' ActualWidth=%.2f MaxWidth=%.2f (%.0f%% of dia)\n"), 
                           text, actualWidth, maxWidth, MAX_WIDTH_RATIO * 100.0);
                
                // If text is too wide, compress it
                if (actualWidth > maxWidth)
                {
                    double newWidthFactor = maxWidth / actualWidth;
                    pText->setWidthFactor(newWidthFactor);
                    acutPrintf(_T("  [COMPRESS] WidthFactor adjusted: 1.0 -> %.3f\n"), newWidthFactor);
                }
                else
                {
                    acutPrintf(_T("  [OK] Text fits within circle (no compression needed)\n"));
                }
            }
        }
        
        acutPrintf(_T("  [TEXT CREATED] Text='%s' Height=%.2f WidthFactor=%.3f\n"), 
                   text, height, pText->widthFactor());
        
        AcDbObjectId textId;
        pModelSpace->appendAcDbEntity(textId, pText);
        pText->close();
        
        return textId;
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

        acutPrintf(_T("  [GROUP] Created group '%s' with circle and text\n"), groupName);
    }

    // SEQNUM command - Create sequence of numbers at specified points
    void sequenceNumberCommand()
    {
        acutPrintf(_T("\n=== SEQUENCE NUMBERS ===\n"));
        
        // Get user input parameters
        double startNum, step, textHeight;
        
        if (!GetRealWithDefault(_T("\nEnter starting number <1>: "), startNum, 1.0) ||
            !GetRealWithDefault(_T("\nEnter step value <1>: "), step, 1.0))
        {
            acutPrintf(_T("\nCommand cancelled.\n"));
            return;
        }
        
        // Get text height - user can type a value or pick two points on screen
        acedInitGet(RSG_NONEG, NULL);
        int heightResult = acedGetDist(NULL, _T("\nSpecify text height <2.5>: "), &textHeight);
        
        if (heightResult == RTNONE)
        {
            // User pressed Enter - use default
            textHeight = 2.5;
            acutPrintf(_T("Using default height: %.2f\n"), textHeight);
        }
        else if (heightResult != RTNORM)
        {
            acutPrintf(_T("\nCommand cancelled.\n"));
            return;
        }
        else
        {
            acutPrintf(_T("Text height set to: %.2f\n"), textHeight);
        }
        
        // Check if user wants circles
        acedInitGet(0, _T("Yes No"));
        TCHAR circleOption[10];
        int result = acedGetKword(_T("\nAdd circles around numbers? [Yes/No] <No>: "), circleOption);
        
        bool addCircles = (result == RTNORM && _tcsicmp(circleOption, _T("Yes")) == 0);
        
        // Circle radius is automatically calculated using golden ratio (1.618) relative to text height
        const double GOLDEN_RATIO = 1.618;
        double circleRadius = textHeight * GOLDEN_RATIO;
        
        if (addCircles)
        {
            acutPrintf(_T("  Circles will be created with radius: %.2f (%.2f * %.3f)\n"), 
                       circleRadius, textHeight, GOLDEN_RATIO);
        }
        
        // Get model space
        AcDbBlockTableRecord* pModelSpace = nullptr;
        Acad::ErrorStatus es = CommonTools::GetModelSpace(pModelSpace);
        
        if (es != Acad::eOk)
        {
            acutPrintf(_T("\nError: Could not access model space.\n"));
            return;
        }
        
        // Main loop: Get points and create entities
        double currentNum = startNum;
        int count = 0;
        ads_point pt;
        
        acutPrintf(_T("\nClick points for numbers (press ESC to finish)...\n"));
        
        while (true)
        {
            TCHAR numStr[50];
            TCHAR prompt[150];
            FormatNumber(currentNum, step, numStr, 50);
            _stprintf_s(prompt, 150, _T("\nSpecify point for number %s (or press ENTER to finish): "), numStr);
            
            if (acedGetPoint(NULL, prompt, pt) != RTNORM)
                break;
            
            AcGePoint3d centerPt(pt[0], pt[1], pt[2]);
            
            // Create circle and text, then group them
            if (addCircles)
            {
                AcDbObjectId circleId = CreateCircle(centerPt, circleRadius, pModelSpace);
                AcDbObjectId textId = CreateCenteredText(centerPt, numStr, textHeight, circleRadius, pModelSpace);
                
                // Group circle and text together
                CreateNumberGroup(circleId, textId);
            }
            else
            {
                // Text only, no grouping needed
                CreateCenteredText(centerPt, numStr, textHeight, 0.0, pModelSpace);
            }
            
            acutPrintf(_T("  Created text: %s%s at (%.2f, %.2f)\n"), 
                       numStr, addCircles ? _T(" in circle") : _T(""), pt[0], pt[1]);
            
            currentNum += step;
            count++;
        }
        
        pModelSpace->close();
        acutPrintf(_T("\nCreated %d sequential numbers.\n"), count);
    }
}
