// SeqNumTools.h - Sequence Number Tools Header

#pragma once

namespace SeqNumTools
{
    // Command: Create sequence of numbers at specified points
    void sequenceNumberCommand();

    // Non-interactive core of ATSEQNUM: one centered number at `center`. With
    // withCircle, also draws a golden-ratio circle and groups the two
    // (SEQNUM_n). Returns the text id; circleId receives the circle (or kNull).
    AcDbObjectId CreateSeqNumber(const AcGePoint3d& center, const CString& text,
                                 double height, bool withCircle,
                                 AcDbObjectId* circleId = nullptr,
                                 bool verbose = true);
}
