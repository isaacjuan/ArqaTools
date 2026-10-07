// SeqNumTools.h - Sequence Number Tools Header

#pragma once

namespace SeqNumTools
{
    // Non-interactive core of ATSEQNUM (Lua: LuaCommands/ATSEQNUM.lua, via
    // at.seqNumber): one centered number at `center`. With
    // withCircle, also draws a golden-ratio circle and groups the two
    // (SEQNUM_n). Returns the text id; circleId receives the circle (or kNull).
    AcDbObjectId CreateSeqNumber(const AcGePoint3d& center, const CString& text,
                                 double height, bool withCircle,
                                 AcDbObjectId* circleId = nullptr);
}
